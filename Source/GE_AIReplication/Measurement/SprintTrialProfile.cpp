#include "SprintTrialProfile.h"

#if WITH_EDITOR && DO_ENABLE_NET_TEST

#include "SprintTrialCharacter.h"
#include "SprintTrialReport.h"
#include "Dom/JsonObject.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "EngineGlobals.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Net/Core/Trace/NetTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "Trace/Trace.h"
#include "UnrealClient.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/NetProfiler.h"

namespace
{
	constexpr float ProfileFps = 60.f;
	const TCHAR* TraceChannels[] = {TEXT("Cpu"), TEXT("Frame"), TEXT("Bookmark"), TEXT("Net")};
	const TCHAR* PhaseNames[] = {TEXT("InitialWalk"), TEXT("TransitionWindow"), TEXT("FinalWalk"), TEXT("Idle"), TEXT("Drain")};
	const TCHAR* TimingNames[] = {TEXT("Prediction"), TEXT("PredictionPhysics"), TEXT("Replay"), TEXT("ServerMove"), TEXT("StateRpc")};

	bool WriteFile(const FString& Path, const FString& Value)
	{
		return FFileHelper::SaveStringToFile(Value, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	struct FMetric
	{
		TArray<double> Samples;
		double Total = 0.;

		void Add(double Sample)
		{
			Samples.Add(Sample);
			Total += Sample;
		}

		FString Percentile(double Fraction)
		{
			if (Samples.IsEmpty()) { return FString(); }
			Samples.Sort();
			return FString::Printf(TEXT("%.6f"), Samples[FMath::CeilToInt(Fraction * Samples.Num()) - 1]);
		}
	};

	struct FTraffic
	{
		uint64 Bits = 0;
		int32 Count = 0;

		FString ToCsv(bool bValid, double Duration) const
		{
			if (!bValid || Duration <= 0.) { return TEXT(",,,,"); }
			const FString PerCall = Count ? FString::Printf(TEXT("%.6f"), double(Bits) / Count) : FString();
			return FString::Printf(TEXT(",%llu,%d,%.6f,%s"), Bits, Count, Bits / Duration, *PerCall);
		}
	};

	struct FPhaseStats
	{
		FTraffic Move, State, Response, Ack, Correction, UnknownResponse, NewMove, PendingMove, OldMove, UnmatchedMove;
		uint64 UnattributedResendBits = 0, ServerUnattributedResendBits = 0, MoveFramingBits = 0;
		int32 UnmatchedMoveWrites = 0, UnmatchedResponseWrites = 0;
		FMetric Cpu[UE_ARRAY_COUNT(TimingNames)];
		FMetric Intervals, WorldDeltas;
		int32 Corrections = 0, ReplayMoves = 0, Frames = 0, BackgroundFrames = 0;
	};

	struct FProfileCell
	{
		FString Id, Variant, Network, Key, WalkKey, Scenario;
		int32 Block = 0;
		uint32 Instance = 0, Connection = 0;
		uint64 NetId = 0;
		uint32 ServerInstance = 0, ServerConnection = 0;
		uint64 ServerNetId = 0;
		bool bOwnerPacked = false, bServerPacked = false, bDownlink = false;
		FString DownlinkStatus = TEXT("Unavailable");
		TArray<double> PhaseTimes;
		FPhaseStats Stats[UE_ARRAY_COUNT(PhaseNames) + 1];
		int32 Dropped = 0;
		bool bTimingComplete = true;
		bool bCompleted = false;

		int32 FindPhase(double Time) const
		{
			for (int32 Index = 0; Index + 1 < PhaseTimes.Num(); ++Index)
			{
				if (Time >= PhaseTimes[Index] && Time < PhaseTimes[Index + 1]) { return Index; }
			}
			return INDEX_NONE;
		}

		bool Load(const FString& Directory)
		{
			FString Input;
			TSharedPtr<FJsonObject> Manifest;
			if (!FFileHelper::LoadFileToString(Input, *FPaths::Combine(Directory, TEXT("manifest.json")))
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Manifest)
				|| !Manifest || !Manifest->HasField(TEXT("ProfileSchema")))
			{
				return false;
			}
			Id = Manifest->GetStringField(TEXT("RunId"));
			Variant = Manifest->GetStringField(TEXT("Variant"));
			Scenario = Manifest->GetStringField(TEXT("Scenario"));
			Network = Manifest->GetStringField(TEXT("Network"));
			Key = Manifest->GetStringField(TEXT("ProfileCondition"));
			WalkKey = Manifest->GetStringField(TEXT("ProfileWalkCondition"));
			Block = Manifest->GetIntegerField(TEXT("Block"));
			Instance = Manifest->GetNumberField(TEXT("ProfileInstance"));
			Connection = Manifest->GetNumberField(TEXT("ProfileConnection"));
			NetId = FCString::Strtoui64(*Manifest->GetStringField(TEXT("ProfileNetId")), nullptr, 10);
			if (Manifest->HasField(TEXT("ProfileServerNetId")))
			{
				ServerNetId = FCString::Strtoui64(*Manifest->GetStringField(TEXT("ProfileServerNetId")), nullptr, 10);
				ServerInstance = Manifest->GetNumberField(TEXT("ProfileServerInstance"));
				ServerConnection = Manifest->GetNumberField(TEXT("ProfileServerConnection"));
			}
			bCompleted = Manifest->GetStringField(TEXT("Outcome")) == TEXT("Completed");
			const TArray<TSharedPtr<FJsonValue>>* Windows = nullptr;
			if (Manifest->TryGetArrayField(TEXT("ProfileWindows"), Windows))
			{
				for (const auto& Value : *Windows) { PhaseTimes.Add(Value->AsNumber()); }
			}
			return true;
		}

		void LoadTiming(const FString& Directory)
		{
			for (const TCHAR* Role : {TEXT("owner"), TEXT("server"), TEXT("observer")})
			{
				const FString RoleDirectory = FPaths::Combine(Directory, Role);
				FString Lost;
				if (FFileHelper::LoadFileToString(Lost, *FPaths::Combine(RoleDirectory, TEXT("profile_dropped.txt"))))
				{
					Dropped += FCString::Atoi(*Lost);
				}
				else { bTimingComplete = false; }
				if (FCString::Strcmp(Role, TEXT("observer")) == 0) { continue; }

				TArray<FString> Lines;
				if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(RoleDirectory, TEXT("timing.csv"))))
				{
					bTimingComplete = false;
					continue;
				}
				for (int32 Row = 1; Row < Lines.Num(); ++Row)
				{
					TArray<FString> Fields;
					Lines[Row].ParseIntoArray(Fields, TEXT(","), false);
					if (Fields.Num() != 7) { bTimingComplete = false; continue; }
					const double Start = FCString::Atod(*Fields[2]);
					const int32 Phase = FindPhase(Start);
					if (Phase == INDEX_NONE) { continue; }
					for (FPhaseStats* Scope : {&Stats[Phase], &Stats[UE_ARRAY_COUNT(PhaseNames)]})
					{
						if (Fields[0] == TEXT("Correction")) { ++Scope->Corrections; continue; }
						for (int32 Metric = 0; Metric < UE_ARRAY_COUNT(TimingNames); ++Metric)
						{
							if (Fields[0] != TimingNames[Metric]) { continue; }
							Scope->Cpu[Metric].Add((FCString::Atod(*Fields[3]) - Start) * 1000.);
							if (Fields[0] == TEXT("Replay")) { Scope->ReplayMoves += FCString::Atoi(*Fields[6]); }
							break;
						}
					}
				}
			}

			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Directory, TEXT("frames.csv"))))
			{
				bTimingComplete = false;
				return;
			}
			for (int32 Row = 1; Row < Lines.Num(); ++Row)
			{
				TArray<FString> Fields;
				Lines[Row].ParseIntoArray(Fields, TEXT(","), false);
				if (Fields.Num() != 6) { bTimingComplete = false; continue; }
				const int32 Phase = FCString::Atoi(*Fields[2]);
				if (Phase < 0 || Phase >= UE_ARRAY_COUNT(PhaseNames)) { continue; }
				for (FPhaseStats* Scope : {&Stats[Phase], &Stats[UE_ARRAY_COUNT(PhaseNames)]})
				{
					++Scope->Frames;
					Scope->BackgroundFrames += Fields[5] == TEXT("0");
					Scope->WorldDeltas.Add(FCString::Atod(*Fields[3]));
					if (!Fields[4].IsEmpty()) { Scope->Intervals.Add(FCString::Atod(*Fields[4])); }
				}
			}
		}
	};

	FString PhaseBookmark(const FString& Id, int32 Phase)
	{
		return FString::Printf(TEXT("SprintProfile %s %d"), *Id, Phase);
	}

	struct FRpcRange
	{
		int32 Phase;
		double Seconds;
		uint32 Packet, FirstBit, EndBit;
		uint64 Object;
		FString Event;
		const TCHAR* Delivery;

		uint32 Bits() const { return EndBit - FirstBit; }
	};

	struct FPackedWrite
	{
		uint32 Serial;
		int32 Packet;
		double Time;
		int32 Bits, Ack, NewBits, PendingBits, OldBits;
		bool bMatched = false;
	};

	bool LoadPackedWrites(const FString& Source, const TCHAR* Role, TArray<FPackedWrite>& Writes)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Source, Role, TEXT("packed.csv")))) { return false; }
		for (int32 Row = 1; Row < Lines.Num(); ++Row)
		{
			TArray<FString> Fields;
			Lines[Row].ParseIntoArray(Fields, TEXT(","), false);
			if (Fields.Num() != 23) { Writes.Reset(); return false; }
			const auto Int = [&](int32 Index) { return FCString::Atoi(*Fields[Index]); };
			Writes.Add({uint32(Int(0)), Int(1), FCString::Atod(*Fields[3]), Int(5), Int(6),
				Int(12) - Int(11), Int(17) - Int(16), Int(22) - Int(21)});
		}
		return true;
	}

	template<typename ArrayType>
	bool ScanDirection(const TraceServices::INetProfilerProvider& Net, const FProfileCell& Cell, const ArrayType& Bounds,
		bool bDownlink, TArray<FRpcRange>& Out)
	{
		using namespace TraceServices;
		const bool bKnownEndpoint = !bDownlink || Cell.ServerNetId != 0;
		const uint32 InstanceId = bDownlink ? Cell.ServerInstance : Cell.Instance;
		const uint32 ConnectionId = bDownlink ? Cell.ServerConnection : Cell.Connection;
		// A legacy NetGUID assigned by the server is also used by the client replica.
		const uint64 NetId = bDownlink && bKnownEndpoint ? Cell.ServerNetId : Cell.NetId;
		int32 MatchingConnections = 0;
		Net.ReadGameInstances([&](const FNetProfilerGameInstance& Instance)
		{
			if (Instance.bIsServer != bDownlink || (bKnownEndpoint && Instance.GameInstanceId != InstanceId)) { return; }
			Net.ReadConnections(Instance.GameInstanceIndex, [&](const FNetProfilerConnection& Connection)
			{
				if (bKnownEndpoint && Connection.ConnectionId != ConnectionId) { return; }
				const uint32 Index = Connection.ConnectionIndex;
				const uint32 Count = Net.GetPacketCount(Index, Outgoing);
				if (!Count) { return; }

				TArray<FRpcRange> Ranges;
				bool bFoundSubjectRpc = false;
				Net.EnumeratePackets(Index, Outgoing, 0, Count - 1, [&](const FNetProfilerPacket& Packet)
				{
					if (!Packet.EventCount || Packet.TimeStamp < Bounds[0] || Packet.TimeStamp >= Bounds[UE_ARRAY_COUNT(PhaseNames)]) { return; }
					int32 Phase = 0;
					while (Packet.TimeStamp >= Bounds[Phase + 1]) { ++Phase; }
					uint64 Objects[256]{};
					TSet<uint64, DefaultKeyFuncs<uint64>, TInlineSetAllocator<8>> Seen;
					Net.EnumeratePacketContentEventsByIndex(Index, Outgoing, Packet.StartEventIndex,
						Packet.StartEventIndex + Packet.EventCount - 1, [&](const FNetProfilerContentEvent& Event)
					{
						uint64 ObjectId = Event.Level ? Objects[Event.Level - 1] : 0;
						if (Event.ObjectInstanceIndex)
						{
							Net.ReadObject(Instance.GameInstanceIndex, Event.ObjectInstanceIndex,
								[&](const FNetProfilerObjectInstance& Object) { ObjectId = Object.NetObjectId; });
						}
						Objects[Event.Level] = ObjectId;
						if (Event.EndPos <= Event.StartPos) { return; }
						Net.ReadName(Event.NameIndex, [&](const FNetProfilerName& Name)
						{
							if (!Name.Name) { return; }
							const FStringView Label(Name.Name);
							const bool bResend = Label == TEXT("ResendBunch");
							const bool bRelevant = bDownlink ? Label == TEXT("ClientMoveResponsePacked")
								: Label == TEXT("ServerRequestSprint") || Label.StartsWith(TEXT("ServerMove"));
							if (!bResend && (ObjectId != NetId || !bRelevant)) { return; }
							const uint64 RangeKey = (uint64(Event.StartPos) << 32) | Event.EndPos;
							if (Seen.Contains(RangeKey)) { return; }
							Seen.Add(RangeKey);
							bFoundSubjectRpc |= !bResend;
							const TCHAR* Delivery = Packet.DeliveryStatus == ENetProfilerDeliveryStatus::Delivered ? TEXT("Delivered")
								: Packet.DeliveryStatus == ENetProfilerDeliveryStatus::Dropped ? TEXT("Dropped") : TEXT("Unknown");
							Ranges.Add({Phase, Packet.TimeStamp, Packet.SequenceNumber, uint32(Event.StartPos), uint32(Event.EndPos),
								ObjectId, FString(Name.Name), Delivery});
						});
					});
				});
				if (bFoundSubjectRpc)
				{
					++MatchingConnections;
					Out = MoveTemp(Ranges);
				}
			});
		});
		if (MatchingConnections != 1)
		{
			Out.Reset();
			return false;
		}
		return true;
	}

	void SummarizeDirection(FProfileCell& Cell, const TArray<FRpcRange>& Ranges, TArray<FPackedWrite>& Writes,
		bool bDownlink, double ClockOffset, FString& Raw)
	{
		const TCHAR* PackedRpc = bDownlink ? TEXT("ClientMoveResponsePacked") : TEXT("ServerMovePacked");
		TMap<uint32, TArray<int32>> WritesByPacket, RangesByPacket;
		for (int32 Index = 0; Index < Writes.Num(); ++Index)
		{
			const FPackedWrite& Write = Writes[Index];
			if (Write.Packet != INDEX_NONE) { WritesByPacket.FindOrAdd(uint32(Write.Packet)).Add(Index); }
		}
		for (int32 Index = 0; Index < Ranges.Num(); ++Index)
		{
			if (Ranges[Index].Event == PackedRpc) { RangesByPacket.FindOrAdd(Ranges[Index].Packet).Add(Index); }
		}
		TMap<int32, int32> Matches;
		for (auto& [Packet, InPacket] : RangesByPacket)
		{
			const TArray<int32>* Candidates = WritesByPacket.Find(Packet);
			// The packet number sampled at serialization is a candidate until the bunch is flushed.
			// Reject partial or ambiguous matches, including overflow to a later packet.
			if (!Candidates || InPacket.Num() != Candidates->Num()) { continue; }
			InPacket.Sort([&](int32 A, int32 B) { return Ranges[A].FirstBit < Ranges[B].FirstBit; });
			bool bValid = true;
			for (int32 Index = 0; Index < InPacket.Num(); ++Index)
			{
				const FRpcRange& Rpc = Ranges[InPacket[Index]];
				const FPackedWrite& Write = Writes[(*Candidates)[Index]];
				bValid &= Write.Time + ClockOffset <= Rpc.Seconds + .001
					&& Write.Bits > 0 && uint32(Write.Bits) <= Rpc.Bits()
					&& (bDownlink ? Write.Ack == 0 || Write.Ack == 1 : Write.NewBits > 0
						&& Write.NewBits + Write.PendingBits + Write.OldBits <= Write.Bits);
			}
			if (!bValid) { continue; }
			for (int32 Index = 0; Index < InPacket.Num(); ++Index)
			{
				Matches.Add(InPacket[Index], (*Candidates)[Index]);
				Writes[(*Candidates)[Index]].bMatched = true;
			}
		}

		constexpr int32 TotalIndex = UE_ARRAY_COUNT(PhaseNames);
		const auto Add = [](FTraffic& Traffic, uint32 Bits) { Traffic.Bits += Bits; ++Traffic.Count; };
		for (int32 Index = 0; Index < Ranges.Num(); ++Index)
		{
			const FRpcRange& Rpc = Ranges[Index];
			const bool bResend = Rpc.Event == TEXT("ResendBunch");
			const bool bState = Rpc.Event == TEXT("ServerRequestSprint");
			const int32* WriteIndex = Matches.Find(Index);
			const FPackedWrite* Write = WriteIndex ? &Writes[*WriteIndex] : nullptr;
			const TCHAR* Kind = bResend ? TEXT("UnattributedResend") : bDownlink
				? (Write ? (Write->Ack ? TEXT("Ack") : TEXT("Correction")) : TEXT("UnknownResponse")) : bState ? TEXT("State") : TEXT("Move");
			for (FPhaseStats* Scope : {&Cell.Stats[Rpc.Phase], &Cell.Stats[TotalIndex]})
			{
				if (bResend)
				{
					(bDownlink ? Scope->ServerUnattributedResendBits : Scope->UnattributedResendBits) += Rpc.Bits();
				}
				else if (bDownlink)
				{
					Add(Scope->Response, Rpc.Bits());
					Add(Write ? (Write->Ack ? Scope->Ack : Scope->Correction) : Scope->UnknownResponse, Rpc.Bits());
				}
				else if (bState)
				{
					Add(Scope->State, Rpc.Bits());
				}
				else
				{
					Add(Scope->Move, Rpc.Bits());
					if (Write)
					{
						Add(Scope->NewMove, Write->NewBits);
						if (Write->PendingBits) { Add(Scope->PendingMove, Write->PendingBits); }
						if (Write->OldBits) { Add(Scope->OldMove, Write->OldBits); }
						Scope->MoveFramingBits += Rpc.Bits() - Write->NewBits - Write->PendingBits - Write->OldBits;
					}
					else { Add(Scope->UnmatchedMove, Rpc.Bits()); }
				}
			}
			const FString Serial = Write ? LexToString(Write->Serial) : FString();
			Raw += FString::Printf(TEXT("%s,%.9f,%u,%s,%llu,%s,%u,%u,%u,%s,%s,%s\n"),
				PhaseNames[Rpc.Phase], Rpc.Seconds, Rpc.Packet, Rpc.Delivery, Rpc.Object, *Rpc.Event, Rpc.FirstBit, Rpc.EndBit,
				Rpc.Bits(), bDownlink ? TEXT("ServerToOwner") : TEXT("OwnerToServer"), Kind, *Serial);
		}
		for (const FPackedWrite& Write : Writes)
		{
			const int32 Phase = Cell.FindPhase(Write.Time);
			if (Write.bMatched || Phase == INDEX_NONE) { continue; }
			for (FPhaseStats* Scope : {&Cell.Stats[Phase], &Cell.Stats[TotalIndex]})
			{
				++(bDownlink ? Scope->UnmatchedResponseWrites : Scope->UnmatchedMoveWrites);
			}
		}
	}

	bool CollectDirection(const TraceServices::IAnalysisSession& Session, FProfileCell& Cell,
		const FString& Source, bool bServer, FString& Raw)
	{
		using namespace TraceServices;
		const INetProfilerProvider* Net = ReadNetProfilerProvider(Session);
		if (!Net || !Net->GetNetTraceVersion() || !(bServer ? Cell.ServerNetId : Cell.NetId)) { return false; }

		TStaticArray<double, UE_ARRAY_COUNT(PhaseNames) + 1> Bounds;
		for (double& Bound : Bounds) { Bound = -1.; }
		const FString Prefix = TEXT("SprintProfile ") + Cell.Id + TEXT(" ");
		ReadBookmarkProvider(Session).EnumerateBookmarks(0., Session.GetDurationSeconds(), [&](const FBookmark& Bookmark)
		{
			const FString Name(Bookmark.Text);
			if (!Name.StartsWith(Prefix)) { return; }
			int32 Phase;
			if (LexTryParseString(Phase, *Name.RightChop(Prefix.Len())) && Phase >= 0
				&& Phase < UE_ARRAY_COUNT(PhaseNames) + 1 && Name == PhaseBookmark(Cell.Id, Phase))
			{
				Bounds[Phase] = Bookmark.Time;
			}
		});
		for (int32 Phase = 0; Phase < UE_ARRAY_COUNT(PhaseNames); ++Phase)
		{
			if (Bounds[Phase] < 0. || Bounds[Phase + 1] <= Bounds[Phase]) { return false; }
		}

		TArray<FRpcRange> Ranges;
		TArray<FPackedWrite> Writes;
		const double ClockOffset = Cell.PhaseTimes.Num() ? Bounds[0] - Cell.PhaseTimes[0] : 0.;
		const bool bEndpoint = ScanDirection(*Net, Cell, Bounds, bServer, Ranges);
		(bServer ? Cell.bServerPacked : Cell.bOwnerPacked) = LoadPackedWrites(Source, bServer ? TEXT("server") : TEXT("owner"), Writes);
		if (bEndpoint) { SummarizeDirection(Cell, Ranges, Writes, bServer, ClockOffset, Raw); }
		return bEndpoint;
	}

	const TCHAR* RpcHeader = TEXT("Phase,TraceSeconds,PacketSequence,PacketStatus,NetId,Event,StartBit,EndBit,Bits,Direction,Kind,PackedSerial\n");

	bool CollectTraffic(const TraceServices::IAnalysisSession& Session, FProfileCell& Cell,
		const FString& Source, const FString& Directory)
	{
		FString Raw = RpcHeader;
		const bool bUplink = CollectDirection(Session, Cell, Source, false, Raw);
		Cell.bDownlink = CollectDirection(Session, Cell, Source, true, Raw);
		Cell.DownlinkStatus = Cell.bDownlink ? TEXT("Complete") : TEXT("ResponseEndpointMissingOrAmbiguous");
		const bool bWritten = WriteFile(FPaths::Combine(Directory, TEXT("rpc_bits.csv")), Raw);
		if (!bWritten) { Cell.bDownlink = false; Cell.DownlinkStatus = TEXT("WriteFailed"); }
		const FPhaseStats& Total = Cell.Stats[UE_ARRAY_COUNT(PhaseNames)];
		return bWritten && bUplink && Total.Move.Count > 0
			&& (Cell.Scenario == TEXT("TapSweep") || (Total.State.Count > 0) == (Cell.Variant == TEXT("SeparateRpc")));
	}

	bool SaveSummary(const FString& Directory, FProfileCell& Cell, bool bTrafficOk, const TCHAR* Status)
	{
		FString Csv = TEXT("RunId,Variant,Block,Network,Condition,Phase,Seconds,Status,MoveBits,MoveCalls,MoveBitsPerSec,MoveBitsPerCall,")
			TEXT("StateBits,StateCalls,StateBitsPerSec,StateBitsPerCall,UnattributedResendBits,Frames,BackgroundFrames,")
			TEXT("FrameP50Ms,FrameP95Ms,FrameMaxMs,WorldDeltaP50Ms,WorldDeltaP95Ms,WorldDeltaMaxMs,Corrections,ReplayMoves,ReplayMsPerMove,Dropped");
		for (const TCHAR* Name : TimingNames)
		{
			Csv += FString::Printf(TEXT(",%sCalls,%sTotalMs,%sMsPerSec,%sP50Ms,%sP95Ms"), Name, Name, Name, Name, Name);
		}
		Csv += TEXT(",Scenario,TrafficSchema,DownlinkStatus,UplinkBitsPerSec,BidirectionalBitsPerSec,")
			TEXT("ServerUnattributedResendBits,MoveFramingBits,UnmatchedMoveWrites,UnmatchedResponseWrites");
		for (const TCHAR* Name : {TEXT("Response"), TEXT("Ack"), TEXT("CorrectionResponse"), TEXT("UnknownResponse"),
			TEXT("NewMove"), TEXT("PendingMove"), TEXT("OldMove"), TEXT("UnmatchedMove")})
		{
			Csv += FString::Printf(TEXT(",%sBits,%sCalls,%sBitsPerSec,%sBitsPerCall"), Name, Name, Name, Name);
		}
		Csv += TEXT("\n");

		const int32 TotalIndex = UE_ARRAY_COUNT(PhaseNames);
		for (int32 Index = 0; Index <= TotalIndex; ++Index)
		{
			if (Index != TotalIndex && !Cell.PhaseTimes.IsValidIndex(Index + 1)) { continue; }
			const bool bTotal = Index == TotalIndex;
			const double Duration = Cell.PhaseTimes.Num() > 1
				? (bTotal ? Cell.PhaseTimes.Last() - Cell.PhaseTimes[0] : Cell.PhaseTimes[Index + 1] - Cell.PhaseTimes[Index]) : 0.;
			FPhaseStats& Stats = Cell.Stats[Index];
			Csv += FString::Printf(TEXT("%s,%s,%d,%s,%s,%s,%.9f,%s"), *Cell.Id, *Cell.Variant, Cell.Block, *Cell.Network,
				Index == 0 ? *Cell.WalkKey : *Cell.Key, bTotal ? TEXT("Total") : PhaseNames[Index], Duration, Status);
			Csv += Stats.Move.ToCsv(bTrafficOk, Duration) + Stats.State.ToCsv(bTrafficOk, Duration);
			Csv += bTrafficOk ? FString::Printf(TEXT(",%llu"), Stats.UnattributedResendBits) : TEXT(",");
			const FString PerReplayMove = Stats.ReplayMoves
				? FString::Printf(TEXT("%.6f"), Stats.Cpu[2].Total / Stats.ReplayMoves) : FString();
			Csv += FString::Printf(TEXT(",%d,%d,%s,%s,%s,%s,%s,%s,%d,%d,%s,%d"), Stats.Frames, Stats.BackgroundFrames,
				*Stats.Intervals.Percentile(.50), *Stats.Intervals.Percentile(.95), *Stats.Intervals.Percentile(1.),
				*Stats.WorldDeltas.Percentile(.50), *Stats.WorldDeltas.Percentile(.95), *Stats.WorldDeltas.Percentile(1.),
				Stats.Corrections, Stats.ReplayMoves, *PerReplayMove, Cell.Dropped);
			for (FMetric& Metric : Stats.Cpu)
			{
				Csv += FString::Printf(TEXT(",%d,%.6f,%.6f,%s,%s"), Metric.Samples.Num(), Metric.Total,
					Duration > 0. ? Metric.Total / Duration : 0., *Metric.Percentile(.50), *Metric.Percentile(.95));
			}
			Csv += FString::Printf(TEXT(",%s,2,%s"), *Cell.Scenario, *Cell.DownlinkStatus);
			const uint64 Uplink = Stats.Move.Bits + Stats.State.Bits;
			Csv += bTrafficOk && Duration > 0. ? FString::Printf(TEXT(",%.6f"), Uplink / Duration) : TEXT(",");
			Csv += bTrafficOk && Cell.bDownlink && Duration > 0.
				? FString::Printf(TEXT(",%.6f"), (Uplink + Stats.Response.Bits) / Duration) : TEXT(",");
			Csv += Cell.bDownlink ? FString::Printf(TEXT(",%llu"), Stats.ServerUnattributedResendBits) : TEXT(",");
			Csv += bTrafficOk && Cell.bOwnerPacked ? FString::Printf(TEXT(",%llu,%d"), Stats.MoveFramingBits, Stats.UnmatchedMoveWrites) : TEXT(",,");
			Csv += Cell.bDownlink && Cell.bServerPacked ? FString::Printf(TEXT(",%d"), Stats.UnmatchedResponseWrites) : TEXT(",");
			Csv += Stats.Response.ToCsv(Cell.bDownlink, Duration);
			Csv += Stats.Ack.ToCsv(Cell.bDownlink && Cell.bServerPacked, Duration);
			Csv += Stats.Correction.ToCsv(Cell.bDownlink && Cell.bServerPacked, Duration);
			Csv += Stats.UnknownResponse.ToCsv(Cell.bDownlink, Duration);
			Csv += Stats.NewMove.ToCsv(bTrafficOk && Cell.bOwnerPacked, Duration);
			Csv += Stats.PendingMove.ToCsv(bTrafficOk && Cell.bOwnerPacked, Duration);
			Csv += Stats.OldMove.ToCsv(bTrafficOk && Cell.bOwnerPacked, Duration);
			Csv += Stats.UnmatchedMove.ToCsv(bTrafficOk, Duration);
			Csv += TEXT("\n");
		}
		return WriteFile(FPaths::Combine(Directory, TEXT("profile.csv")), Csv);
	}
}

bool FSprintTrialProfile::Start(const FString& Batch, FString& Error)
{
#if UE_NET_TRACE_ENABLED && UE_TRACE_ENABLED
	if (UE::Trace::IsTracing() || FTraceAuxiliary::IsConnected() || FTraceAuxiliary::IsPaused() || FNetTrace::GetTraceVerbosity())
	{
		Error = TEXT("Stop the existing trace or NetTrace session before starting SprintTrial profiling.");
		return false;
	}
	if (GEngine->bUseFixedFrameRate || FApp::UseFixedTimeStep() || FApp::IsBenchmarking())
	{
		Error = TEXT("Disable fixed frame rate or fixed time step before SprintTrial profiling.");
		return false;
	}
	TracePath = FPaths::Combine(Batch, TEXT("profile.utrace"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TraceChannels); ++Index)
	{
		const UE::Trace::FChannel* Channel = UE::Trace::FindChannel(TraceChannels[Index]);
		ChannelWasEnabled[Index] = Channel && Channel->IsEnabled();
	}
	FTraceAuxiliary::FOptions Options;
	Options.bExcludeTail = true;
	if (!FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TracePath, TEXT("cpu,frame,bookmark"), &Options))
	{
		Error = TEXT("Could not start the profile.utrace recording.");
		return false;
	}
	bOwnsTrace = true;
	PreviousVerbosity = FNetTrace::GetTraceVerbosity();
	FNetTrace::SetTraceVerbosity(static_cast<uint32>(ENetTraceVerbosity::Trace));
	if (FNetTrace::GetTraceVerbosity() < static_cast<uint32>(ENetTraceVerbosity::Trace))
	{
		Error = TEXT("This editor build does not expose RPC-level NetTrace.");
		Stop();
		return false;
	}
	PreviousMaxFps = GEngine->GetMaxFPS();
	GEngine->SetMaxFPS(ProfileFps);
	bOwnsCap = true;
	return true;
#else
	Error = TEXT("This editor build does not support NetTrace.");
	return false;
#endif
}

void FSprintTrialProfile::Stop()
{
#if UE_NET_TRACE_ENABLED && UE_TRACE_ENABLED
	if (bOwnsTrace)
	{
		if (FPaths::IsSamePath(FTraceAuxiliary::GetTraceDestinationString(), TracePath)) { FTraceAuxiliary::Stop(); }
		FNetTrace::SetTraceVerbosity(PreviousVerbosity);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(TraceChannels); ++Index)
		{
			UE::Trace::ToggleChannel(TraceChannels[Index], ChannelWasEnabled[Index]);
		}
		bOwnsTrace = false;
	}
#endif
	if (bOwnsCap)
	{
		GEngine->SetMaxFPS(PreviousMaxFps);
		bOwnsCap = false;
	}
}

void FSprintTrialProfile::ResetCell(const FGuid& InTrialId)
{
	TrialId = InTrialId;
	Frames.Reset(2048);
	Phases.Reset();
	LastFrame = 0.;
	NetId = 0;
	InstanceId = ConnectionId = 0;
	ServerNetId = 0;
	ServerInstanceId = ServerConnectionId = 0;
}

void FSprintTrialProfile::BindEndpoints(const UNetDriver& Driver, const ASprintTrialCharacter& Subject,
	const UNetDriver& ServerDriver, const ASprintTrialCharacter& ServerSubject)
{
	BindEndpoint(Driver, Subject, false);
	BindEndpoint(ServerDriver, ServerSubject, true);
}

void FSprintTrialProfile::BindEndpoint(const UNetDriver& Driver, const ASprintTrialCharacter& Subject, bool bServer)
{
#if UE_NET_TRACE_ENABLED
	(bServer ? ServerInstanceId : InstanceId) = Driver.GetNetTraceId();
	const UNetConnection* Connection = bServer ? Subject.GetNetConnection() : Driver.ServerConnection.Get();
	if (Connection) { (bServer ? ServerConnectionId : ConnectionId) = Connection->GetConnectionHandle().GetParentConnectionId(); }
	if (const auto& Cache = Driver.GetNetGuidCache())
	{
		(bServer ? ServerNetId : NetId) = Cache->GetNetGUID(&Subject).ObjectId;
	}
#endif
}

void FSprintTrialProfile::MarkPhase(int32 Phase, double Now)
{
	Phases.Emplace(Phase, Now);
	TRACE_BOOKMARK(TEXT("SprintProfile %s %d"), *TrialId.ToString(EGuidFormats::Digits), Phase);
}

void FSprintTrialProfile::RecordFrame(int32 Phase, double Now, float WorldDeltaTime)
{
	if (Frames.Num() < 16384)
	{
		Frames.Add({GFrameCounter, Now, LastFrame > 0. ? (Now - LastFrame) * 1000. : -1.,
			WorldDeltaTime * 1000.f, Phase, FApp::HasFocus()});
	}
	LastFrame = Now;
}

bool FSprintTrialProfile::SaveCell(const FString& Directory, double Now, FJsonObject& Manifest)
{
	MarkPhase(UE_ARRAY_COUNT(PhaseNames), Now);
	TArray<TSharedPtr<FJsonValue>> Times;
	for (const auto& Phase : Phases) { Times.Add(MakeShared<FJsonValueNumber>(Phase.Value)); }
	Manifest.SetArrayField(TEXT("ProfileWindows"), MoveTemp(Times));
	Manifest.SetNumberField(TEXT("ProfileInstance"), InstanceId);
	Manifest.SetNumberField(TEXT("ProfileConnection"), ConnectionId);
	Manifest.SetStringField(TEXT("ProfileNetId"), LexToString(NetId));
	Manifest.SetNumberField(TEXT("ProfileServerInstance"), ServerInstanceId);
	Manifest.SetNumberField(TEXT("ProfileServerConnection"), ServerConnectionId);
	Manifest.SetStringField(TEXT("ProfileServerNetId"), LexToString(ServerNetId));
	FString Csv = TEXT("Frame,PlatformSeconds,Phase,WorldDeltaMs,IntervalMs,Foreground\n");
	for (const FFrameSample& Frame : Frames)
	{
		const FString Interval = Frame.IntervalMs >= 0. ? FString::Printf(TEXT("%.6f"), Frame.IntervalMs) : FString();
		Csv += FString::Printf(TEXT("%llu,%.9f,%d,%.6f,%s,%d\n"), Frame.Frame, Frame.Time, Frame.Phase,
			Frame.WorldDeltaMs, *Interval, Frame.bForeground ? 1 : 0);
	}
	return WriteFile(FPaths::Combine(Directory, TEXT("frames.csv")), Csv);
}

void FSprintTrialProfile::DescribeEnvironment(const UWorld& World, FJsonObject& Manifest) const
{
	const FWorldContext* Context = GEngine->GetWorldContextFromWorld(&World);
	const FIntPoint Viewport = Context && Context->GameViewport && Context->GameViewport->Viewport
		? Context->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	const auto ReadCVar = [](const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : -1;
	};
	const FString Environment = FString::Printf(TEXT("fps=%g|pie=%d|viewport=%dx%d|vsync=%d|quality=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d|cpu=%s|gpu=%s|rhi=%s|throttle=%d|dilation=%g|trace=cpu,frame,bookmark,net"),
		GEngine->GetMaxFPS(), Context ? Context->PIEInstance : -1, Viewport.X, Viewport.Y, ReadCVar(TEXT("r.VSync")),
		ReadCVar(TEXT("sg.ResolutionQuality")), ReadCVar(TEXT("sg.ViewDistanceQuality")), ReadCVar(TEXT("sg.AntiAliasingQuality")),
		ReadCVar(TEXT("sg.ShadowQuality")), ReadCVar(TEXT("sg.GlobalIlluminationQuality")), ReadCVar(TEXT("sg.ReflectionQuality")),
		ReadCVar(TEXT("sg.PostProcessQuality")), ReadCVar(TEXT("sg.TextureQuality")), ReadCVar(TEXT("sg.EffectsQuality")),
		ReadCVar(TEXT("sg.FoliageQuality")), *FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand(),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), GetDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground,
		World.GetWorldSettings()->GetEffectiveTimeDilation());
	Manifest.SetNumberField(TEXT("ProfileSchema"), 1);
	Manifest.SetStringField(TEXT("ProfileEnvironment"), Environment);
	const FString Condition = FString::Printf(TEXT("1|%s|%s|%s"), *Manifest.GetStringField(TEXT("ConditionHash")),
		*Environment, TEXT("Walk350|Run650|Forward0-6"));
	Manifest.SetStringField(TEXT("ProfileCondition"), FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Condition)));
	const FString WalkCondition = FString::Printf(TEXT("1|%s|%s|Walk350|Forward0-2"), *Manifest.GetStringField(TEXT("ProfileWalkCondition")), *Environment);
	Manifest.SetStringField(TEXT("ProfileWalkCondition"), FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*WalkCondition)));
}

bool FSprintTrialProfile::AnalyzeBatch(const FString& Directory, FString& Message, bool bReanalysis)
{
	const FString Path = FPaths::Combine(Directory, TEXT("profile.utrace"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		Message = TEXT("profile.utrace is missing.");
		return false;
	}
	FString Output = Directory;
	FString Readme;
	if (bReanalysis)
	{
		const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
		Output = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tasks"),
			FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-SprintProfileReport-") + Id));
		Readme = FString::Printf(TEXT("# Sprint profile reanalysis\n\n")
			TEXT("Command: SprintTrial.Run ProfileReport %s\nCreated (UTC): %s\nSource batch: %s\nTraffic schema: 2\n\n")
			TEXT("Compare this output folder using CompareSprintTrials.ps1. batch.csv indexes the original cells. ")
			TEXT("Each cell contains its original summary and a new profile.csv and rpc_bits.csv. ")
			TEXT("Keep the source trace, manifests and raw CSVs to reproduce the report.\n"),
			*FPaths::GetCleanFilename(Directory), *FDateTime::UtcNow().ToIso8601(), *Directory);
		if (!IFileManager::Get().MakeDirectory(*Output, true) ||
			!WriteFile(FPaths::Combine(Output, TEXT("README.md")), Readme + TEXT("\nOutcome: Preparing\n")))
		{
			Message = TEXT("Could not prepare the reanalysis output folder.");
			return false;
		}
	}
	auto& Module = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const auto Session = Module.GetAnalysisService()->Analyze(*Path);
	if (Session) { Session->Wait(); }

	TArray<FString> Subdirectories;
	IFileManager::Get().FindFiles(Subdirectories, *FPaths::Combine(Directory, TEXT("Jitter*")), false, true);
	Subdirectories.Sort();
	int32 Cells = 0, Complete = 0, Bidirectional = 0;
	bool bSaved = true;
	if (bReanalysis)
	{
		bSaved = IFileManager::Get().Copy(*FPaths::Combine(Output, TEXT("batch.csv")),
			*FPaths::Combine(Directory, TEXT("batch.csv"))) == COPY_OK;
	}
	FString Index = TEXT("Cell,ProfileStatus,DownlinkStatus\n");
	for (const FString& Name : Subdirectories)
	{
		const FString CellDirectory = FPaths::Combine(Directory, Name);
		FProfileCell Cell;
		if (!Cell.Load(CellDirectory)) { continue; }
		++Cells;
		const FString CellOutput = FPaths::Combine(Output, Name);
		if (bReanalysis)
		{
			bSaved &= IFileManager::Get().MakeDirectory(*CellOutput, true)
				&& IFileManager::Get().Copy(*FPaths::Combine(CellOutput, TEXT("summary.csv")),
					*FPaths::Combine(CellDirectory, TEXT("summary.csv"))) == COPY_OK;
		}
		Cell.LoadTiming(CellDirectory);
		const bool bTrafficOk = Cell.bCompleted && Session && [&]()
		{
			TraceServices::FAnalysisSessionReadScope Scope(*Session);
			return CollectTraffic(*Session, Cell, CellDirectory, CellOutput);
		}();
		const bool bCpuOk = Cell.bTimingComplete && Cell.Dropped == 0 && Cell.Stats[0].Cpu[0].Samples.Num() > 0
			&& Cell.Stats[0].Cpu[3].Samples.Num() > 0 && Cell.PhaseTimes.Num() == UE_ARRAY_COUNT(PhaseNames) + 1;
		const TCHAR* Status = !Cell.bCompleted ? TEXT("TrialFailed") : !bTrafficOk ? TEXT("RpcTraceUnavailable")
			: !bCpuOk ? TEXT("TimingIncomplete") : TEXT("Complete");
		Complete += Cell.bCompleted && bTrafficOk && bCpuOk;
		Bidirectional += bTrafficOk && Cell.bDownlink;
		bSaved &= SaveSummary(CellOutput, Cell, bTrafficOk, Status);
		bSaved &= SprintTrialReport::WriteDiagnostics(CellDirectory, CellOutput);
		Index += FString::Printf(TEXT("%s,%s,%s\n"), *Name, Status, *Cell.DownlinkStatus);
	}
	bSaved &= WriteFile(FPaths::Combine(Output, TEXT("profile_status.csv")), Index);
	Message = FString::Printf(TEXT("Profile data complete for %d/%d recorded cells. Bidirectional RPC data for %d/%d. Results: %s"),
		Complete, Cells, Bidirectional, Cells, *Output);
	if (!bSaved) { Message += TEXT(" Some profile reports could not be written."); }
	if (bReanalysis) { bSaved &= WriteFile(FPaths::Combine(Output, TEXT("README.md")), Readme + TEXT("\nOutcome: ") + Message + TEXT("\n")); }
	return bSaved && Cells > 0 && Cells == Complete && Cells == Bidirectional;
}

bool FSprintTrialProfile::AnalyzeSplit(const FString& Directory)
{
	FProfileCell Cell;
	if (!Cell.Load(Directory) || !Cell.bCompleted) { return false; }
	Cell.LoadTiming(Directory);
	FString EndpointJson;
	TSharedPtr<FJsonObject> Endpoint;
	const TArray<TSharedPtr<FJsonValue>>* Windows = nullptr;
	if (!FFileHelper::LoadFileToString(EndpointJson, *FPaths::Combine(Directory, TEXT("server/endpoint.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EndpointJson), Endpoint)
		|| !Endpoint || !Endpoint->TryGetArrayField(TEXT("ProfileWindows"), Windows)
		|| Windows->Num() != UE_ARRAY_COUNT(PhaseNames) + 1)
	{
		return false;
	}
	TArray<double> ServerTimes;
	for (const auto& Window : *Windows) { ServerTimes.Add(Window->AsNumber()); }
	FString Raw = RpcHeader;
	auto& Module = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));
	const auto Analyze = [&](const TCHAR* Role, bool bServer)
	{
		const auto Session = Module.GetAnalysisService()->Analyze(*FPaths::Combine(Directory, Role, TEXT("profile.utrace")));
		if (!Session) { return false; }
		Session->Wait();
		TraceServices::FAnalysisSessionReadScope Scope(*Session);
		return CollectDirection(*Session, Cell, Directory, bServer, Raw);
	};
	const bool bUplink = Analyze(TEXT("owner"), false);
	{
		TGuardValue<TArray<double>> SenderTimes(Cell.PhaseTimes, ServerTimes);
		Cell.bDownlink = Analyze(TEXT("server"), true);
	}
	Cell.DownlinkStatus = Cell.bDownlink ? TEXT("Complete") : TEXT("ResponseEndpointMissingOrAmbiguous");
	const bool bTraffic = bUplink && Cell.Stats[UE_ARRAY_COUNT(PhaseNames)].Move.Count > 0;
	const TCHAR* Status = bTraffic && Cell.bDownlink ? (Cell.Dropped ? TEXT("TimingIncomplete") : TEXT("Complete")) : TEXT("RpcTraceUnavailable");
	return WriteFile(FPaths::Combine(Directory, TEXT("rpc_bits.csv")), Raw)
		&& SaveSummary(Directory, Cell, bTraffic, Status);
}

#endif
