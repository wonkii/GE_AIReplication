#include "SprintTrialReport.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"
#include "SprintTrialCharacter.h"
#include "GameFramework/GameNetworkManager.h"

namespace
{
	struct FTrialEvent
	{
		FString Name;
		double WorldSeconds = 0.;
		FString Time;
		int32 Epoch = 0;
		int32 Phase = INDEX_NONE;
		FVector Position;
		FVector Other;
		double Speed = 0.;
		double MaxSpeed = 0.;
		double MeshOffset = 0.;
		double Value = 0.;
		double PlatformSeconds = -1., Delta = 0., AppliedCap = -1.;
		int32 Intent = -1, Mode = -1, ModeBefore = -1, PeerMode = -1, Forced = -1, RawRow = 0, Packet = -1;

		double Cap() const { return AppliedCap >= 0. ? AppliedCap : MaxSpeed; }
		double End() const { return FCString::Atod(*Time); }
		double Begin() const { return End() - Delta; }
	};

	TArray<FTrialEvent> LoadEvents(const FString& Path)
	{
		TArray<FString> Lines;
		TArray<FTrialEvent> Events;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			return Events;
		}
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			TArray<FString> Columns;
			Lines[Index].ParseIntoArray(Columns, TEXT(","), false);
			if (Columns.Num() < 15)
			{
				return {};
			}
			auto Number = [&Columns](int32 Column) { return FCString::Atod(*Columns[Column]); };
			Events.Add({Columns[0], Number(1), Columns[2], FCString::Atoi(*Columns[3]), FCString::Atoi(*Columns[4]),
				FVector(Number(5), Number(6), Number(7)), FVector(Number(8), Number(9), Number(10)),
				Number(11), Number(12), Number(13), Number(14)});
			auto& Event = Events.Last();
			Event.RawRow = Index + 1;
			if (Columns.Num() >= 23)
			{
				Event.PlatformSeconds = Number(15);
				Event.Delta = Number(16);
				Event.Intent = Number(17);
				Event.Mode = Number(18);
				Event.ModeBefore = Number(19);
				Event.PeerMode = Number(20);
				Event.Forced = Number(21);
				Event.AppliedCap = Number(22);
			}
			if (Columns.Num() >= 24) { Event.Packet = Number(23); }
		}
		return Events;
	}

	FString Percentile(TArray<double> Values, double Fraction = .95)
	{
		if (Values.IsEmpty())
		{
			return FString();
		}
		Values.Sort();
		return FString::Printf(TEXT("%.9g"), Values[FMath::CeilToInt(Values.Num() * Fraction) - 1]);
	}

	struct FTransitionWindow
	{
		const FTrialEvent* FirstMove;
		bool bPress;
		int32 OwnerMoves = 0;
		int32 MatchedMoves = 0;
	};

	struct FTransitionStats
	{
		TArray<double> Errors;
		int32 Moves = 0;
		int32 Matched = 0;
		int32 CapMismatches = 0;
		int32 Transitions = 0;
		int32 MissingTransitions = 0;
	};

	FString MoveKey(const FTrialEvent& Event)
	{
		return FString::Printf(TEXT("%d|%s"), Event.Epoch, *Event.Time);
	}

	struct FSummary
	{
		TArray<FString> Fields;
		TArray<FString> Values;

		void Add(const TCHAR* Field, const FString& Value)
		{
			Fields.Add(Field);
			Values.Add(FSharpRecordingFiles::Csv(Value));
		}

		void Add(const TCHAR* Field, double Value) { Add(Field, FString::Printf(TEXT("%.9g"), Value)); }

		explicit FSummary(const FJsonObject& Manifest)
		{
			for (const TCHAR* Field : {TEXT("RunId"), TEXT("Variant"), TEXT("Scenario"), TEXT("Network"), TEXT("ConditionHash"), TEXT("Instrumentation")})
			{
				Add(Field, Manifest.GetStringField(Field));
			}
			Add(TEXT("Block"), Manifest.GetNumberField(TEXT("Block")));
			if (Manifest.HasField(TEXT("MatrixId")))
			{
				Add(TEXT("MatrixId"), Manifest.GetStringField(TEXT("MatrixId")));
				Add(TEXT("PktJitterMs"), Manifest.GetNumberField(TEXT("PktJitterMs")));
				Add(TEXT("LossPerDirection"), Manifest.GetNumberField(TEXT("OwnerLossPercentPerDirection")));
			}
		}

		bool Save(const FString& Directory) const
		{
			const FString Csv = FString::Join(Fields, TEXT(",")) + TEXT("\n") + FString::Join(Values, TEXT(",")) + TEXT("\n");
			return FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, TEXT("summary.csv")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	};
}

namespace
{
	struct FAppliedWindow
	{
		const FTrialEvent* Press;
		const FTrialEvent* Release = nullptr;
		double Distance = 0.;
		int32 Moves = 0;
	};

	bool DiagnosticCsv(const FString& Root, const TCHAR* Name, const FString& Csv)
	{
		return FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Root, Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString DiagnosticNumber(double Value) { return FString::Printf(TEXT("%.9g"), Value); }

	TArray<FAppliedWindow> AppliedWindows(const TArray<FTrialEvent>& Events, const TCHAR* Name, bool bOwner)
	{
		TArray<FAppliedWindow> Windows;
		bool bActive = false;
		for (const auto& Event : Events)
		{
			if (Event.Name != Name) { continue; }
			const bool bSprint = bOwner ? (Event.Intent >= 0 ? Event.Intent != 0 : Event.Value != 0.)
				: FMath::IsNearlyEqual(Event.Cap(), double(USprintTrialPolicy::RunSpeed));
			if (bSprint && !bActive) { Windows.Add({&Event}); }
			if (bSprint)
			{
				++Windows.Last().Moves;
				if (!bOwner) { Windows.Last().Distance += Event.Value; }
			}
			else if (bActive) { Windows.Last().Release = &Event; }
			bActive = bSprint;
		}
		return Windows;
	}

	const FTrialEvent* FindSerial(const TArray<FTrialEvent>& Rows, const TCHAR* Name, int32 Serial)
	{
		return Rows.FindByPredicate([&](const FTrialEvent& Event) { return Event.Name == Name && Event.Value == Serial; });
	}

	FString InputDiagnostics(const TArray<FTrialEvent>& Owner, const TArray<FTrialEvent>& Server)
	{
		FString Csv = TEXT("TrialTap,PlannedPressRow,PlannedReleaseRow,PlannedHeldMs,OwnerPressRow,OwnerReleaseRow,ServerFirstMoveRow,ServerReleaseMoveRow,")
			TEXT("OwnerHeldMs,StartErrorMs,ReleaseErrorMs,DurationErrorMs,ServerSprintDistanceCm,ServerSprintMoves,OverlapMs,Outcome,")
			TEXT("RpcPressSentRow,RpcReleaseSentRow,RpcPressReceivedRow,RpcReleaseReceivedRow,RpcPressAcceptedRow,RpcReleaseAcceptedRow,RpcServerIntervalMs,")
			TEXT("RpcPressToHandlerMs,RpcReleaseToHandlerMs,ServerAppliedHeldMs,OpenWindowLowerBoundMs,OverrunMs,OverrunLowerBoundMs\n");
		const bool bNew = Owner.ContainsByPredicate([](const auto& Event) { return Event.Name == TEXT("OwnerAppliedMove"); });
		const auto Wanted = AppliedWindows(Owner, bNew ? TEXT("OwnerAppliedMove") : TEXT("OwnerMove"), true);
		const auto Actual = AppliedWindows(Server, TEXT("ServerAppliedMove"), false);
		TArray<const FTrialEvent*> Planned;
		for (const auto& Event : Owner)
		{
			if (Event.Name == TEXT("PlannedInput")) { Planned.Add(&Event); }
		}
		int32 NextActual = 0, NextWanted = 0;
		const int32 NumTaps = Planned.IsEmpty() ? Wanted.Num() : Planned.Num() / 2;
		for (int32 Index = 0; Index < NumTaps; ++Index)
		{
			const FTrialEvent* PlanPress = Planned.IsEmpty() ? nullptr : Planned[Index * 2];
			const FTrialEvent* PlanRelease = PlanPress ? Planned[Index * 2 + 1] : nullptr;
			const FAppliedWindow* Expected = Wanted.IsValidIndex(NextWanted) ? &Wanted[NextWanted] : nullptr;
			if (Expected && PlanRelease && (Expected->Press->PlatformSeconds < PlanPress->PlatformSeconds
				|| Expected->Press->PlatformSeconds < 0. || Expected->Press->PlatformSeconds >= PlanRelease->PlatformSeconds))
			{
				Expected = nullptr;
			}
			if (Expected) { ++NextWanted; }
			const auto* Start = Expected ? Expected->Press : nullptr;
			const auto* End = Expected ? Expected->Release : nullptr;
			const auto* Following = Wanted.IsValidIndex(NextWanted) ? Wanted[NextWanted].Press : nullptr;
			const FAppliedWindow* Matched = nullptr;
			int32 Overlapping = 0;
			bool bMerged = false;
			for (int32 Candidate = NextActual; Start && Actual.IsValidIndex(Candidate); ++Candidate)
			{
				const auto& Window = Actual[Candidate];
				if (Window.Press->Epoch < Start->Epoch || (Window.Release && Window.Release->Epoch == Start->Epoch
					&& Window.Release->Begin() <= Start->Begin())) { continue; }
				if (Window.Press->Epoch != Start->Epoch || (Following && Following->Epoch == Start->Epoch
					&& Window.Press->Begin() >= Following->Begin())) { break; }
				if (!Matched)
				{
					Matched = &Window;
					NextActual = Candidate + 1;
				}
				if (End && Window.Press->Begin() < End->Begin()) { ++Overlapping; NextActual = Candidate + 1; }
				bMerged |= Following && Following->Epoch == Start->Epoch &&
					(!Window.Release || (Window.Release->Epoch == Start->Epoch && Window.Release->Begin() > Following->Begin()));
			}
			const auto* ActualStart = Matched ? Matched->Press : nullptr;
			const auto* ActualEnd = Matched ? Matched->Release : nullptr;
			const bool bStartComparable = Start && ActualStart && Start->Epoch == ActualStart->Epoch && Start->Delta > 0. && ActualStart->Delta > 0.;
			const bool bEndComparable = End && ActualEnd && End->Epoch == ActualEnd->Epoch && End->Delta > 0. && ActualEnd->Delta > 0.;
			const bool bOwnerComplete = Start && End && End->Epoch == Start->Epoch;
			const bool bServerComplete = ActualStart && ActualEnd && ActualStart->Epoch == ActualEnd->Epoch;
			const TCHAR* Outcome = !Start ? TEXT("NoSavedPress") : !ActualStart ? TEXT("NoServerSprint") :
				!ActualEnd ? TEXT("ReleaseUnobserved") : bMerged ? TEXT("Merged") : Overlapping > 1 ? TEXT("Split") :
				Overlapping == 0 ? TEXT("Delayed") : TEXT("Overlap");
			const int32 PressIndex = Start && bNew ? Owner.FindLastByPredicate([&](const auto& Event)
				{ return Event.Name == TEXT("SprintRpcSent") && Event.Intent == 1 && Event.RawRow < Start->RawRow; }) : INDEX_NONE;
			const auto* PressSent = Owner.IsValidIndex(PressIndex) ? &Owner[PressIndex] : nullptr;
			const int32 Serial = PressSent ? int32(PressSent->Value) : bNew ? INDEX_NONE : Index * 2 + 1;
			const auto* ReleaseSent = Serial >= 0 ? FindSerial(Owner, TEXT("SprintRpcSent"), Serial + 1) : nullptr;
			const auto* PressReceived = FindSerial(Server, TEXT("SprintRpcReceived"), Serial);
			const auto* ReleaseReceived = FindSerial(Server, TEXT("SprintRpcReceived"), Serial + 1);
			const auto* PressAccepted = FindSerial(Server, TEXT("SprintRpc"), Serial);
			const auto* ReleaseAccepted = FindSerial(Server, TEXT("SprintRpc"), Serial + 1);
			const auto Row = [](const FTrialEvent* Event) { return Event ? LexToString(Event->RawRow) : FString(); };
			const auto Ms = [](bool bValid, double Sec) { return bValid ? DiagnosticNumber(Sec * 1000.) : FString(); };
			const auto Begin = [](const FTrialEvent* Event) { return Event ? Event->Begin() : 0.; };
			const double Held = Begin(End) - Begin(Start);
			const double ServerHeld = Begin(ActualEnd) - Begin(ActualStart);
			const auto HandlerDelay = [&](const FTrialEvent* A, const FTrialEvent* B)
			{
				return A && B && A->PlatformSeconds >= 0. && B->PlatformSeconds >= A->PlatformSeconds
					? Ms(true, B->PlatformSeconds - A->PlatformSeconds) : FString();
			};
			const int32 LastServerIndex = ActualStart && !ActualEnd ? Server.FindLastByPredicate([&](const auto& Event)
				{ return Event.Name == TEXT("ServerAppliedMove") && Event.Epoch == ActualStart->Epoch; }) : INDEX_NONE;
			const FTrialEvent* LastServerMove = Server.IsValidIndex(LastServerIndex) ? &Server[LastServerIndex] : nullptr;
			Csv += FString::Join(TArray<FString>{
				LexToString(Index + 1), Row(PlanPress), Row(PlanRelease), PlanPress ? Ms(true, PlanRelease->Delta - PlanPress->Delta) : FString(),
				Row(Start), Row(End), Row(ActualStart), Row(ActualEnd), Ms(bOwnerComplete && bNew, Held),
				Ms(bStartComparable, Begin(ActualStart) - Begin(Start)), Ms(bEndComparable, Begin(ActualEnd) - Begin(End)),
				Ms(bStartComparable && bEndComparable && bOwnerComplete && bServerComplete, ServerHeld - Held),
				Matched ? DiagnosticNumber(Matched->Distance) : FString(), Matched ? LexToString(Matched->Moves) : FString(),
				Ms(bStartComparable && bEndComparable && bOwnerComplete && bServerComplete,
					FMath::Max(0., FMath::Min(Begin(End), Begin(ActualEnd)) - FMath::Max(Begin(Start), Begin(ActualStart)))),
				Outcome, Row(PressSent), Row(ReleaseSent), Row(PressReceived), Row(ReleaseReceived), Row(PressAccepted), Row(ReleaseAccepted),
				PressAccepted && ReleaseAccepted ? Ms(true, ReleaseAccepted->WorldSeconds - PressAccepted->WorldSeconds) : FString(),
				HandlerDelay(PressSent, PressReceived), HandlerDelay(ReleaseSent, ReleaseReceived), Ms(bServerComplete && bStartComparable, ServerHeld),
				Ms(LastServerMove != nullptr, LastServerMove ? LastServerMove->End() - Begin(ActualStart) : 0.),
				Ms(bEndComparable, FMath::Max(0., Begin(ActualEnd) - Begin(End))),
				Ms(LastServerMove && End && LastServerMove->Epoch == End->Epoch && End->Delta > 0.,
					LastServerMove ? FMath::Max(0., LastServerMove->End() - Begin(End)) : 0.)
			}, TEXT(",")) + TEXT("\n");
		}
		return Csv;
	}

	FString RpcAttempts(const FString& Source, const FString& Output, const TArray<FTrialEvent>& Owner)
	{
		const TCHAR* Header = TEXT("TraceRawRow,PacketSequence,PacketStatus,Bits,OwnerSendRawRow,TransitionSerial,Evidence\n");
		FString Csv = Header;
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Output, TEXT("rpc_bits.csv"))))
		{
			FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Source, TEXT("rpc_bits.csv")));
		}
		TMap<int32, TArray<const FTrialEvent*>> Sends;
		for (const auto& Event : Owner)
		{
			if (Event.Name == TEXT("SprintRpcSent") && Event.Packet >= 0) { Sends.FindOrAdd(Event.Packet).Add(&Event); }
		}
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			TArray<FString> Columns;
			Lines[Index].ParseIntoArray(Columns, TEXT(","), false);
			if (Columns.Num() < 12 || Columns[10] != TEXT("State")) { continue; }
			const auto* Candidates = Sends.Find(FCString::Atoi(*Columns[2]));
			const auto* Match = Candidates && Candidates->Num() == 1 ? (*Candidates)[0] : nullptr;
			Csv += FString::Join(TArray<FString>{LexToString(Index + 1), Columns[2], Columns[3], Columns[8],
				Match ? LexToString(Match->RawRow) : FString(), Match ? DiagnosticNumber(Match->Value) : FString(),
				Match ? TEXT("PacketHint") : TEXT("Unattributed")}, TEXT(",")) + TEXT("\n");
		}
		return Csv;
	}
}

bool SprintTrialReport::WriteDiagnostics(const FString& Source, const FString& Output)
{
	const auto Owner = LoadEvents(FPaths::Combine(Source, TEXT("owner/events.csv")));
	const auto Server = LoadEvents(FPaths::Combine(Source, TEXT("server/events.csv")));
	double CostStart = -1., CostEnd = -1.;
	FString Text;
	TSharedPtr<FJsonObject> Manifest;
	if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Source, TEXT("manifest.json")))
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) && Manifest)
	{
		const TArray<TSharedPtr<FJsonValue>>* Times = nullptr;
		if (Manifest->TryGetArrayField(TEXT("ProfileWindows"), Times) && Times->Num() >= 2)
		{
			CostStart = (*Times)[0]->AsNumber();
			CostEnd = Times->Last()->AsNumber();
		}
	}
	TMap<FString, const FTrialEvent*> OwnerMoves, ServerMoves, Checks, Queued;
	TMap<FString, int32> EpisodeByMove;
	TSet<FString> Direct;
	FString Timeline = TEXT("Epoch,MoveTime,OwnerRawRow,ServerRawRow,CheckRawRow,OwnerIntent,ServerIntent,OwnerCap,ServerCap,OwnerMode,ServerMode,")
		TEXT("IntentMismatch,CapMismatch,MovementModeMismatch,PositionErrorCm,PositionThresholdExceeded,CheckReturnedError,ForceObserved,Episode,InCostWindow\n");
	FString Corrections = TEXT("Epoch,MoveTime,OwnerCorrectionRow,ServerCheckRow,Episode,FirstMismatchRow,Evidence,ReconciliationCm,InCostWindow,ServerResponseQueuedRow,ForceObserved,ReconciliationRawRow\n");
	for (const auto& Event : Owner)
	{
		const FString Key = MoveKey(Event);
		if (Event.Name == TEXT("OwnerMove") && !OwnerMoves.Contains(Key)) { OwnerMoves.Add(Key, &Event); }
		if (Event.Name == TEXT("OwnerAppliedMove")) { OwnerMoves.Add(Key, &Event); }
	}
	for (const auto& Event : Server)
	{
		if (Event.Name == TEXT("ServerAppliedMove")) { ServerMoves.Add(MoveKey(Event), &Event); }
		if (Event.Name == TEXT("ServerCheck")) { Checks.Add(MoveKey(Event), &Event); }
		if (Event.Name == TEXT("ServerResponseQueued")) { Queued.Add(MoveKey(Event), &Event); }
	}
	const auto Row = [](const FTrialEvent* E) { return E ? LexToString(E->RawRow) : FString(); };
	const auto Flag = [](bool bKnown, bool bValue) { return bKnown ? LexToString(int32(bValue)) : FString(); };
	const auto OptionalInt = [](int32 Value) { return Value >= 0 ? LexToString(Value) : FString(); };
	const auto InCostWindow = [&](const FTrialEvent& Event)
	{
		return Flag(CostStart >= 0. && Event.PlatformSeconds >= 0.,
			Event.PlatformSeconds >= CostStart && Event.PlatformSeconds < CostEnd);
	};
	TMap<int32, int32> FirstMismatchRows;
	int32 Episode = 0, ActiveEpisode = 0, PreviousEpoch = INDEX_NONE;
	int32 Matched = 0, Mismatched = 0, CorrectionCount = 0, Linked = 0;
	TSet<FString> Seen;
	const auto* NetManager = GetDefault<AGameNetworkManager>();
	for (const auto& Event : Server)
	{
		if (Event.Name != TEXT("ServerAppliedMove") && Event.Name != TEXT("ServerCheck")) { continue; }
		const FString Key = MoveKey(Event);
		if (Seen.Contains(Key)) { continue; }
		Seen.Add(Key);
		const auto* Local = OwnerMoves.FindRef(Key);
		const auto* Applied = ServerMoves.FindRef(Key);
		const auto* Check = Checks.FindRef(Key);
		if (PreviousEpoch != Event.Epoch) { ActiveEpisode = 0; }
		PreviousEpoch = Event.Epoch;
		const bool bCapMismatch = Local && Applied && !FMath::IsNearlyEqual(Local->Cap(), Applied->Cap());
		const int32 OwnerMode = Local ? Local->Mode : -1;
		const int32 ServerMode = Applied ? Applied->Mode : -1;
		const int32 OwnerIntent = Local ? (Local->Intent >= 0 ? Local->Intent : int32(Local->Value != 0.)) : -1;
		const int32 ServerIntent = Applied ? Applied->Intent : -1;
		const bool bModeKnown = Check && Check->PeerMode >= 0 && Check->Mode >= 0;
		const bool bModeMismatch = bModeKnown && Check->PeerMode != Check->Mode;
		const bool bThreshold = Check && NetManager->ExceedsAllowablePositionError(Check->Position - Check->Other);
		Matched += Local && Applied;
		Mismatched += bCapMismatch;
		if (bCapMismatch)
		{
			Direct.Add(Key);
			if (!ActiveEpisode) { ActiveEpisode = ++Episode; FirstMismatchRows.Add(Episode, Event.RawRow); }
		}
		if (ActiveEpisode) { EpisodeByMove.Add(Key, ActiveEpisode); }
		Timeline += FString::Join(TArray<FString>{LexToString(Event.Epoch), Event.Time, Row(Local), Row(Applied), Row(Check), OptionalInt(OwnerIntent),
			OptionalInt(ServerIntent), Local ? DiagnosticNumber(Local->Cap()) : FString(), Applied ? DiagnosticNumber(Applied->Cap()) : FString(),
			OptionalInt(OwnerMode), OptionalInt(ServerMode), Flag(OwnerIntent >= 0 && ServerIntent >= 0, OwnerIntent != ServerIntent),
			Flag(Local && Applied, bCapMismatch), Flag(bModeKnown, bModeMismatch),
			Check ? DiagnosticNumber(FVector::Dist(Check->Position, Check->Other)) : FString(),
			Flag(Check != nullptr, bThreshold), Flag(Check != nullptr, Check && Check->Value != 0.),
			OptionalInt(Check ? Check->Forced : -1), ActiveEpisode ? LexToString(ActiveEpisode) : FString(),
			InCostWindow(Check ? *Check : Event)}, TEXT(",")) + TEXT("\n");
		if (!bCapMismatch && Local && Applied && Check && !bModeMismatch && !bThreshold && Check->Value == 0.)
		{
			ActiveEpisode = 0;
		}
	}
	for (const auto& Entry : OwnerMoves)
	{
		if (Seen.Contains(Entry.Key)) { continue; }
		const auto& Event = *Entry.Value;
		TArray<FString> Columns;
		Columns.SetNum(19);
		Columns[0] = LexToString(Event.Epoch);
		Columns[1] = Event.Time;
		Columns[2] = Row(&Event);
		Columns[5] = OptionalInt(Event.Intent >= 0 ? Event.Intent : int32(Event.Value != 0.));
		Columns[7] = DiagnosticNumber(Event.Cap());
		Columns[9] = OptionalInt(Event.Mode);
		Columns.Last() = InCostWindow(Event);
		Timeline += FString::Join(Columns, TEXT(",")) + TEXT("\n");
	}
	TMap<FString, const FTrialEvent*> Reconciliation;
	for (const auto& Event : Owner)
	{
		if (Event.Name == TEXT("OwnerReplay") || Event.Name == TEXT("OwnerReconciled")) { Reconciliation.Add(MoveKey(Event), &Event); }
	}
	TSet<FString> ReconciledCorrections;
	for (const auto& Event : Owner)
	{
		if (Event.Name != TEXT("OwnerCorrection")) { continue; }
		++CorrectionCount;
		const FString Key = MoveKey(Event);
		const auto* Check = Checks.FindRef(Key);
		const auto* Response = Queued.FindRef(Key);
		const int32 Id = EpisodeByMove.FindRef(Key);
		Linked += Id != 0;
		const auto* Replay = ReconciledCorrections.Contains(Key) ? nullptr : Reconciliation.FindRef(Key);
		if (Replay) { ReconciledCorrections.Add(Key); }
		const TCHAR* Evidence = Direct.Contains(Key) ? TEXT("DirectCapMismatch") : Id ? TEXT("EarlierCapMismatch") :
			Response && Response->Forced == 1 ? TEXT("ForcedUpdateObserved") :
			Check && Check->Value != 0. ? TEXT("ServerErrorObserved") : TEXT("InsufficientEvidence");
		Corrections += FString::Join(TArray<FString>{LexToString(Event.Epoch), Event.Time, Row(&Event), Row(Check), OptionalInt(Id ? Id : -1),
			OptionalInt(Id ? FirstMismatchRows.FindRef(Id) : -1), Evidence,
			Replay ? DiagnosticNumber(FVector::Dist(Replay->Position, Replay->Other)) : FString(),
			InCostWindow(Event), Row(Response), OptionalInt(Response ? Response->Forced : -1), Row(Replay)}, TEXT(",")) + TEXT("\n");
	}
	int32 AckQueued = 0, CorrectionQueued = 0;
	for (const auto& Event : Server)
	{
		if (Event.Name != TEXT("ServerResponseQueued")) { continue; }
		if (Event.Value != 0.) { ++AckQueued; } else { ++CorrectionQueued; }
	}
	FString Loads = TEXT("OwnerEvents,ServerEvents,OwnerMoveKeys,ServerAppliedMoveKeys,MatchedMoves,CapMismatchMoves,OwnerCorrections,")
		TEXT("CorrectionsLinkedToCapMismatch,AcksQueued,CorrectionsQueued\n");
	Loads += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%s,%s\n"), Owner.Num(), Server.Num(), OwnerMoves.Num(), ServerMoves.Num(),
		Matched, Mismatched, CorrectionCount, Linked, *(Queued.IsEmpty() ? FString() : LexToString(AckQueued)),
		*(Queued.IsEmpty() ? FString() : LexToString(CorrectionQueued)));
	return DiagnosticCsv(Output, TEXT("move_timeline.csv"), Timeline)
		&& DiagnosticCsv(Output, TEXT("correction_episodes.csv"), Corrections)
		&& DiagnosticCsv(Output, TEXT("input_windows.csv"), InputDiagnostics(Owner, Server))
		&& DiagnosticCsv(Output, TEXT("rpc_attempts.csv"), RpcAttempts(Source, Output, Owner))
		&& DiagnosticCsv(Output, TEXT("diagnostic_coverage.csv"), Loads);
}

bool SprintTrialReport::WriteSummary(const FString& Directory, const FJsonObject& Manifest, FString& Message)
{
	const auto Owner = LoadEvents(FPaths::Combine(Directory, TEXT("owner/events.csv")));
	const auto Server = LoadEvents(FPaths::Combine(Directory, TEXT("server/events.csv")));
	const auto Observer = LoadEvents(FPaths::Combine(Directory, TEXT("observer/events.csv")));
	const bool bMatrix = Manifest.HasField(TEXT("MatrixId"));
	const bool bCmcWalk = Manifest.GetStringField(TEXT("Variant")) == TEXT("CmcWalk");
	FSummary Summary(Manifest);
	Summary.Add(TEXT("Outcome"), FString(TEXT("Completed")));

	TMap<FString, const FTrialEvent*> PredictedMoves;
	TMap<FString, int32> WindowByMove;
	TArray<FTransitionWindow> Windows;
	TArray<TPair<const FTrialEvent*, const FTrialEvent*>> OwnerSprintWindows;
	FTransitionStats TransitionStats, PressStats, ReleaseStats;
	bool bPreviousIntent = false;
	TMap<int32, FVector2D> MoveRanges;
	TArray<const FTrialEvent*> Transitions;
	TArray<double> OwnerSprintSpeeds;
	TOptional<FVector> PreviousSprintPosition;
	double OwnerDistance = 0.;
	bool bOwnerSprintCap = false;
	bool bOwnerWalk = false;
	double Start = -1., End = 0.;
	for (const auto& Event : Owner)
	{
		if (Event.Name == TEXT("Input") && (Event.Phase == 1 || Event.Phase == 2))
		{
			Transitions.Add(&Event);
		}
		else if (Event.Name == TEXT("OwnerMove"))
		{
			const FString Key = MoveKey(Event);
			PredictedMoves.Add(Key, &Event);
			const bool bIntent = Event.Value != 0.;
			if (bIntent != bPreviousIntent)
			{
				Windows.Add({&Event, bIntent});
				if (bIntent) { OwnerSprintWindows.Emplace(&Event, nullptr); }
				else if (!OwnerSprintWindows.IsEmpty()) { OwnerSprintWindows.Last().Value = &Event; }
				bPreviousIntent = bIntent;
			}
			if (!Windows.IsEmpty())
			{
				FTransitionWindow& Window = Windows.Last();
				const double Delta = FCString::Atod(*Event.Time) - FCString::Atod(*Window.FirstMove->Time);
				if (Window.FirstMove->Epoch == Event.Epoch && Delta >= 0. && Delta < .1)
				{
					WindowByMove.Add(Key, Windows.Num() - 1);
					++Window.OwnerMoves;
				}
			}
			const double Time = FCString::Atod(*Event.Time);
			MoveRanges.FindOrAdd(Event.Epoch, FVector2D(Time, Time)).Y = Time;
			if (Start < 0.) { Start = Event.WorldSeconds; }
			End = Event.WorldSeconds;
			bOwnerWalk |= Event.Phase == 0 && Event.Speed > 1. && Event.MaxSpeed == USprintTrialPolicy::WalkSpeed;
			if (!bCmcWalk && Event.Phase == 1)
			{
				bOwnerSprintCap |= Event.MaxSpeed == USprintTrialPolicy::RunSpeed;
				OwnerSprintSpeeds.Add(Event.Speed);
				if (PreviousSprintPosition)
				{
					OwnerDistance += FVector::Dist(PreviousSprintPosition.GetValue(), Event.Position);
				}
				PreviousSprintPosition = Event.Position;
			}
		}
	}

	TArray<double> RemoteIntervals, RemoteOffsets;
	double LastRemote = -1.;
	for (const auto& Event : Observer)
	{
		if (Event.Name == TEXT("RemoteMovement"))
		{
			if (LastRemote >= 0.) { RemoteIntervals.Add((Event.WorldSeconds - LastRemote) * 1000.); }
			LastRemote = Event.WorldSeconds;
		}
		else if (Event.Name == TEXT("RemoteSample"))
		{
			RemoteOffsets.Add(Event.MeshOffset);
		}
	}
	const bool bServerWalk = Server.ContainsByPredicate([](const FTrialEvent& Event)
	{
		return Event.Name == TEXT("ServerCheck") && Event.Speed > 1. && Event.MaxSpeed == USprintTrialPolicy::WalkSpeed;
	});
	const bool bTapSweep = Manifest.GetStringField(TEXT("Scenario")) == TEXT("TapSweep");
	if ((bCmcWalk ? (!bOwnerWalk || !bServerWalk) : (!bTapSweep && !bOwnerSprintCap)) || LastRemote < 0. || Server.IsEmpty())
	{
		Message = TEXT("Expected owner/server movement, observer replication or server capture was missing.");
		return false;
	}
	Summary.Add(TEXT("OwnerSprintPeakCmPerSec"), Percentile(OwnerSprintSpeeds, 1.));
	Summary.Add(TEXT("OwnerSprintDistanceCm"), OwnerDistance);
	Summary.Add(TEXT("ObserverUpdateP95Ms"), Percentile(RemoteIntervals));
	Summary.Add(TEXT("ObserverMeshOffsetP95Cm"), Percentile(RemoteOffsets));

	auto InWindow = [&MoveRanges](const FTrialEvent& Event)
	{
		const auto* Range = MoveRanges.Find(Event.Epoch);
		const double Time = FCString::Atod(*Event.Time);
		return Range && Time >= Range->X && Time <= Range->Y;
	};
	PreviousSprintPosition.Reset();
	TArray<double> Errors, TransitionErrors, SprintErrors, ServerSprintSpeeds;
	double ServerDistance = 0.;
	int32 Matched = 0;
	bool bServerSprintCap = false;
	bool bHasAppliedMoves = false, bPreviousServerSprint = false;
	int32 ServerSprintSegments = 0;
	double ServerAppliedDistance = 0.;
	TSet<int32> ReproducedWindows;
	TSet<FString> MatchedKeys;
	for (const auto& Event : Server)
	{
		if (Event.Name == TEXT("OwnerPingMs") && Event.Value >= 0.)
		{
			Summary.Add(TEXT("OwnerPingMs"), Event.Value);
		}
		if (Event.Name == TEXT("ServerAppliedMove") && InWindow(Event))
		{
			bHasAppliedMoves = true;
			const bool bSprint = FMath::IsNearlyEqual(Event.MaxSpeed, double(USprintTrialPolicy::RunSpeed));
			ServerSprintSegments += bSprint && !bPreviousServerSprint;
			bPreviousServerSprint = bSprint;
			if (bSprint)
			{
				ServerAppliedDistance += Event.Value;
				const double Time = FCString::Atod(*Event.Time);
				for (int32 Index = 0; Index < OwnerSprintWindows.Num(); ++Index)
				{
					const auto& Window = OwnerSprintWindows[Index];
					if (Event.Epoch == Window.Key->Epoch && Time >= FCString::Atod(*Window.Key->Time)
						&& (!Window.Value || Window.Value->Epoch != Event.Epoch || Time < FCString::Atod(*Window.Value->Time)))
					{
						ReproducedWindows.Add(Index);
					}
				}
			}
		}
		if (Event.Name != TEXT("ServerCheck") || !InWindow(Event))
		{
			continue;
		}
		const double Error = FVector::Dist(Event.Position, Event.Other);
		Errors.Add(Error);
		bServerSprintCap |= Event.MaxSpeed == USprintTrialPolicy::RunSpeed;
		const FString Key = MoveKey(Event);
		if (const FTrialEvent* const* Predicted = PredictedMoves.Find(Key); Predicted && !MatchedKeys.Contains(Key))
		{
			MatchedKeys.Add(Key);
			++Matched;
			if (const int32* WindowIndex = WindowByMove.Find(Key))
			{
				FTransitionWindow& Window = Windows[*WindowIndex];
				++Window.MatchedMoves;
				FTransitionStats& Edge = Window.bPress ? PressStats : ReleaseStats;
				for (FTransitionStats* Stats : {&TransitionStats, &Edge})
				{
					Stats->Errors.Add(Error);
					++Stats->Matched;
					Stats->CapMismatches += !FMath::IsNearlyEqual((*Predicted)->MaxSpeed, Event.MaxSpeed);
				}
			}
			if ((*Predicted)->Phase == 1)
			{
				SprintErrors.Add(Error);
				ServerSprintSpeeds.Add(Event.Speed);
				if (PreviousSprintPosition) { ServerDistance += FVector::Dist(PreviousSprintPosition.GetValue(), Event.Position); }
				PreviousSprintPosition = Event.Position;
			}
		}
		for (const FTrialEvent* Transition : Transitions)
		{
			const double Delta = FCString::Atod(*Event.Time) - FCString::Atod(*Transition->Time);
			if (Transition->Epoch == Event.Epoch && Delta > 0. && Delta <= .5)
			{
				TransitionErrors.Add(Error);
				break;
			}
		}
	}
	const bool bReachedSpeed = ServerSprintSpeeds.ContainsByPredicate([](double Speed) { return Speed >= USprintTrialPolicy::RunSpeed - 1.; });
	if (Errors.IsEmpty() || (!bMatrix && (!bServerSprintCap ||
		(Manifest.GetStringField(TEXT("Scenario")) != TEXT("Toggle") && !bReachedSpeed))))
	{
		Message = TEXT("Server samples or sprint speed validation failed.");
		return false;
	}
	int32 Corrections = 0;
	TArray<double> ReplayMs, Reconciliation;
	for (const auto& Event : Owner)
	{
		if (!InWindow(Event)) { continue; }
		if (Event.Name == TEXT("OwnerCorrection")) { ++Corrections; }
		if (Event.Name == TEXT("OwnerReplay")) { ReplayMs.Add(Event.Value); }
		if (Event.Name == TEXT("OwnerReplay") || Event.Name == TEXT("OwnerReconciled"))
		{
			Reconciliation.Add(FVector::Dist(Event.Position, Event.Other));
		}
	}
	const FString P95 = Percentile(Errors);
	Summary.Add(TEXT("Samples"), Errors.Num());
	Summary.Add(TEXT("MatchedSamples"), Matched);
	Summary.Add(TEXT("ErrorMedianCm"), Percentile(Errors, .5));
	Summary.Add(TEXT("ErrorP95Cm"), P95);
	Summary.Add(TEXT("ErrorMaxCm"), Percentile(Errors, 1.));
	Summary.Add(TEXT("TransitionP95Cm"), Percentile(TransitionErrors));
	Summary.Add(TEXT("OwnerPredictedMoves"), PredictedMoves.Num());
	Summary.Add(TEXT("UnmatchedOwnerMoves"), PredictedMoves.Num() - Matched);
	Summary.Add(TEXT("UnmatchedServerChecks"), Errors.Num() - Matched);
	Summary.Add(TEXT("ServerAppliedSprintCap"), bServerSprintCap ? 1. : 0.);
	for (const FTransitionWindow& Window : Windows)
	{
		FTransitionStats& Edge = Window.bPress ? PressStats : ReleaseStats;
		for (FTransitionStats* Stats : {&TransitionStats, &Edge})
		{
			Stats->Moves += Window.OwnerMoves;
			++Stats->Transitions;
			Stats->MissingTransitions += Window.MatchedMoves == 0;
		}
	}
	const auto AddTransitionStats = [&Summary](const TCHAR* Prefix, const FTransitionStats& Stats)
	{
		const FString Field(Prefix);
		Summary.Add(*(Field + TEXT("P95Cm")), Percentile(Stats.Errors));
		Summary.Add(*(Field + TEXT("Count")), Stats.Transitions);
		Summary.Add(*(Field + TEXT("UnmatchedCount")), Stats.MissingTransitions);
		Summary.Add(*(Field + TEXT("OwnerMoves")), Stats.Moves);
		Summary.Add(*(Field + TEXT("MatchedMoves")), Stats.Matched);
		Summary.Add(*(Field + TEXT("UnmatchedMoves")), Stats.Moves - Stats.Matched);
		Summary.Add(*(Field + TEXT("CapMismatches")), Stats.CapMismatches);
		Summary.Add(*(Field + TEXT("CapMismatchPct")), Stats.Matched
			? FString::Printf(TEXT("%.9g"), 100. * Stats.CapMismatches / Stats.Matched) : FString());
	};
	AddTransitionStats(TEXT("Transition100"), TransitionStats);
	AddTransitionStats(TEXT("Press100"), PressStats);
	AddTransitionStats(TEXT("Release100"), ReleaseStats);
	Summary.Add(TEXT("SprintP95Cm"), Percentile(SprintErrors));
	Summary.Add(TEXT("ServerSprintPeakCmPerSec"), Percentile(ServerSprintSpeeds, 1.));
	Summary.Add(TEXT("ServerSprintDistanceCm"), ServerDistance);
	if (bHasAppliedMoves)
	{
		Summary.Add(TEXT("OwnerSprintRequests"), OwnerSprintWindows.Num());
		Summary.Add(TEXT("ServerSprintSegments"), ServerSprintSegments);
		Summary.Add(TEXT("ServerAppliedSprintDistanceCm"), ServerAppliedDistance);
		Summary.Add(TEXT("OwnerSprintWindowsReproduced"), ReproducedWindows.Num());
		if (Manifest.GetStringField(TEXT("Scenario")) == TEXT("Tap"))
		{
			Summary.Add(TEXT("TapMissingInAppliedWindow"), OwnerSprintWindows.Num() - ReproducedWindows.Num());
		}
	}
	Summary.Add(TEXT("OwnerCorrections"), Corrections);
	if (End > Start) { Summary.Add(TEXT("OwnerCorrectionsPerMinute"), Corrections * 60. / (End - Start)); }
	Summary.Add(TEXT("OwnerReplayP95Ms"), Percentile(ReplayMs));
	Summary.Add(TEXT("OwnerReconciliationP95Cm"), Percentile(Reconciliation));
	Message = FString::Printf(TEXT("%d samples | p95 %.3f cm | %d corrections"), Errors.Num(), FCString::Atod(*P95), Corrections);

	TArray<FString> Network;
	if (!FFileHelper::LoadFileToStringArray(Network, *FPaths::Combine(Directory, TEXT("server/network.csv"))) || Network.Num() != 3)
	{
		Message = TEXT("Server network counters are missing.");
		return false;
	}
	for (int32 Index = 1; Index < Network.Num(); ++Index)
	{
		TArray<FString> Fields;
		Network[Index].ParseIntoArray(Fields, TEXT(","), false);
		if (Fields.Num() != 6 || FCString::Atod(*Fields[1]) <= 0.)
		{
			Message = TEXT("Server network counters are invalid.");
			return false;
		}
		const double Rate = (FCString::Atod(*Fields[2]) + FCString::Atod(*Fields[3])) / FCString::Atod(*Fields[1]);
		Summary.Add(Index == 1 ? TEXT("OwnerConnectionBytesPerSec") : TEXT("ObserverConnectionBytesPerSec"), Rate);
	}
	if (!Summary.Save(Directory))
	{
		Message = TEXT("Could not write summary.csv.");
		return false;
	}
	return true;
}

bool SprintTrialReport::WriteFailure(const FString& Directory, const FJsonObject& Manifest, const FString& Reason)
{
	FSummary Summary(Manifest);
	Summary.Add(TEXT("Outcome"), FString(TEXT("Failed")));
	Summary.Add(TEXT("ErrorP95Cm"), FString());
	Summary.Add(TEXT("Failure"), Reason);
	return Summary.Save(Directory);
}
