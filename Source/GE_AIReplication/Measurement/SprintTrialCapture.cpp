#include "SprintTrialCapture.h"

#include "SprintTrialCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

USprintTrialCapture* USprintTrialCapture::Find(const UWorld* World)
{
	return World ? World->GetSubsystem<USprintTrialCapture>() : nullptr;
}

bool USprintTrialCapture::Open(const FString& Root, const FString& Role, const FGuid& InTrialId, bool bProfile)
{
	bRecording = false;
	bProfiling = bProfile;
	Dropped = 0;
	Events.Reset();
	Timings.Reset();
	PackedSamples.Reset();
	if (bProfiling)
	{
		Events.Reserve(16384);
		Timings.Reserve(16384);
		PackedSamples.Reserve(4096);
	}
	TrialId = InTrialId;
	Directory = FPaths::Combine(Root, Role);
	return Files.Open(Directory, 64);
}

bool USprintTrialCapture::Signal(const TCHAR* Name, const FString& Text)
{
	if (Directory.IsEmpty())
	{
		return false;
	}
	const FString Path = FPaths::Combine(Directory, Name);
	const FString TempPath = Path + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Text, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& IFileManager::Get().Move(*Path, *TempPath);
}

bool USprintTrialCapture::Finish()
{
	bRecording = false;
	FlushProfile();
	if (!Files.Flush())
	{
		Fail(TEXT("Could not flush movement samples"));
		return false;
	}
	return Signal(TEXT("done.txt"), TEXT("Completed"));
}

void USprintTrialCapture::Fail(const FString& Reason)
{
	bRecording = false;
	FlushProfile();
	Files.Flush();
	Signal(TEXT("failed.txt"), Reason);
	UE_LOG(LogTemp, Error, TEXT("[SprintTrial] %s"), *Reason);
}

void USprintTrialCapture::Record(const ASprintTrialCharacter& Character, const TCHAR* Event, float MoveTime,
	int32 Epoch, const FVector& Other, double Value, int32 Phase, double Delta, int32 Intent, int32 ModeBefore,
	int32 PeerMode, int32 Forced, double AppliedCap, int32 Packet)
{
	if (!bRecording || Character.TrialId != TrialId)
	{
		return;
	}
	const auto* Movement = Character.GetCharacterMovement();
	const FVector Position = Character.GetActorLocation();
	const FVector Mesh = Character.GetMesh()->GetComponentLocation();
	const double MeshOffset = FVector::Dist(Mesh, Character.GetActorTransform().TransformPosition(Character.GetBaseTranslationOffset()));
	const FEvent Sample{Event, GetWorld()->GetTimeSeconds(), MoveTime, Epoch, Phase, Position, Other,
		Movement->Velocity.Size2D(), Movement->GetMaxSpeed(), MeshOffset, Value, FPlatformTime::Seconds(),
		Delta, AppliedCap, Intent, Movement->PackNetworkMovementMode(), ModeBefore, PeerMode, Forced, Packet};
	if (bProfiling)
	{
		if (Events.Num() < 16384) { Events.Add(Sample); }
		else { ++Dropped; }
		return;
	}
	WriteEvent(Sample);
}

void USprintTrialCapture::WriteEvent(const FEvent& Event)
{
	Files.Add(TEXT("events.csv"),
		TEXT("Event,WorldSeconds,MoveTime,Epoch,Phase,X,Y,Z,OtherX,OtherY,OtherZ,Speed,MaxSpeed,MeshOffset,Value,PlatformSeconds,Delta,Intent,Mode,ModeBefore,PeerMode,Forced,AppliedCap,PacketCandidate"),
		FString::Printf(TEXT("%s,%.9f,%.9g,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.9g,%.9f,%.9g,%d,%d,%d,%d,%d,%.6f,%d"),
			Event.Name, Event.WorldTime, Event.MoveTime, Event.Epoch, Event.Phase,
			Event.Position.X, Event.Position.Y, Event.Position.Z, Event.Other.X, Event.Other.Y, Event.Other.Z,
			Event.Speed, Event.MaxSpeed, Event.MeshOffset, Event.Value, Event.PlatformSeconds, Event.Delta,
			Event.Intent, Event.Mode, Event.ModeBefore, Event.PeerMode, Event.Forced, Event.AppliedCap, Event.Packet));
}

bool USprintTrialCapture::IsProfiling(const ASprintTrialCharacter& Character) const
{
	return bRecording && bProfiling && Character.TrialId == TrialId;
}

void USprintTrialCapture::RecordTiming(ESprintProfileEvent Event, double Start, double End, float MoveTime, int32 Epoch, int32 Moves)
{
	if (Timings.Num() < 16384) { Timings.Add({Event, GFrameCounter, Start, End, MoveTime, Epoch, Moves}); }
	else { ++Dropped; }
}

void USprintTrialCapture::RecordPacked(FSprintPackedSample Sample, int32 Packet)
{
	if (PackedSamples.Num() >= 16384) { ++Dropped; return; }
	Sample.Packet = Packet;
	Sample.Frame = GFrameCounter;
	Sample.Time = FPlatformTime::Seconds();
	PackedSamples.Add(Sample);
}

void USprintTrialCapture::FlushProfile()
{
	if (!bProfiling) { return; }
	bProfiling = false;
	for (const FEvent& Event : Events) { WriteEvent(Event); }
	static const TCHAR* Names[] = {TEXT("Prediction"), TEXT("PredictionPhysics"), TEXT("Replay"), TEXT("ServerMove"), TEXT("StateRpc"), TEXT("Correction")};
	for (const FTiming& Timing : Timings)
	{
		Files.Add(TEXT("timing.csv"), TEXT("Event,Frame,StartSeconds,EndSeconds,MoveTime,Epoch,Moves"),
			FString::Printf(TEXT("%s,%llu,%.9f,%.9f,%.9g,%d,%d"), Names[static_cast<int32>(Timing.Event)],
				Timing.Frame, Timing.Start, Timing.End, Timing.MoveTime, Timing.Epoch, Timing.Moves));
	}
	for (const FSprintPackedSample& Sample : PackedSamples)
	{
		FString Row = FString::Printf(TEXT("%u,%d,%llu,%.9f,%d,%lld,%d,%d"), Sample.Serial, Sample.Packet,
			Sample.Frame, Sample.Time, Sample.Epoch, Sample.Bits, Sample.Ack, Sample.AckFlags);
		for (const FSprintPackedMove* Move : {&Sample.NewMove, &Sample.Pending, &Sample.Old})
		{
			Row += FString::Printf(TEXT(",%.9g,%d,%d,%lld,%lld"), Move->Timestamp, Move->Flags, Move->bSprint, Move->FirstBit, Move->EndBit);
		}
		Files.Add(TEXT("packed.csv"),
			TEXT("Serial,PacketCandidate,Frame,PlatformSeconds,Epoch,PayloadBits,Ack,AckFlags,")
			TEXT("NewTime,NewFlags,NewSprint,NewStartBit,NewEndBit,")
			TEXT("PendingTime,PendingFlags,PendingSprint,PendingStartBit,PendingEndBit,OldTime,OldFlags,OldSprint,OldStartBit,OldEndBit"), Row);
	}
	if (!Signal(TEXT("profile_dropped.txt"), LexToString(Dropped))) { Files.MarkFailed(); }
	Events.Reset();
	Timings.Reset();
	PackedSamples.Reset();
}

void USprintTrialCapture::Deinitialize()
{
	Files.Close();
	Super::Deinitialize();
}
