#include "SharpMovementRecorderComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "Detector/SharpMovementSample.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "VisualLogger/VisualLogger.h"

namespace
{
	int32 VisualLoggerUsers = 0;
	bool bWasVisualLoggerRecording = false;
	const FString CommonHeader = TEXT("RunId,TrialId,Mode,ActorId,Generation,Segment,PathId,PathRevision,State,ServerTime,WallTime,Frame");
	double WallSeconds() { return static_cast<double>(FDateTime::UtcNow().GetTicks()) / ETimespan::TicksPerSecond; }
}

USharpMovementRecorderComponent::USharpMovementRecorderComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void USharpMovementRecorderComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy)
	{
		Enemy->OnCharacterMovementUpdated.AddUniqueDynamic(this, &USharpMovementRecorderComponent::ObserveMovement);
		AddTickPrerequisiteComponent(Enemy->GetCharacterMovement());
		AddTickPrerequisiteComponent(Enemy->VisualPredictor);
		HandleSessionChanged();
	}
}

FString USharpMovementRecorderComponent::CommonRow(double ServerTime) const
{
	return FString::Printf(TEXT("%s,%d,%s,%d,%d,%d,%s,%d,%s,%.9f,%.6f,%llu"),
		*FSharpRecordingFiles::Csv(Context.Identity.RunId), Context.Identity.TrialId,
		*StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Context.Identity.Mode)),
		RecordedActorId, Context.Generation, Context.Segment, *FSharpRecordingFiles::Csv(Context.PathId.ToString()),
		Context.PathRevision, *StaticEnum<ESharpEnemyTrialState>()->GetNameStringByValue(static_cast<int64>(Context.State)),
		ServerTime, WallSeconds(), static_cast<unsigned long long>(GFrameCounter));
}

void USharpMovementRecorderComponent::HandleSessionChanged()
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy) { return; }
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	const bool bSame = Context.Identity.RunId == Status.Identity.RunId && Context.Identity.TrialId == Status.Identity.TrialId &&
		Context.Generation == Status.Generation && Context.Segment == Status.Segment;
	if (bRecording && !bSame) { Finish(TEXT("ContextChanged")); }
	Context = Status;
	const bool bActive = Status.State == ESharpEnemyTrialState::Running || Status.State == ESharpEnemyTrialState::Suspended;
	if (bActive && !bRecording && Status.Recording.bEnabled && Status.Generation > 0 && Status.Segment > 0 && Enemy->ExperimentActorId > 0 &&
		(ClosedGeneration != Status.Generation || ClosedSegment != Status.Segment))
	{
		StartRecording();
	}
	if (bRecording)
	{
		RecordDiagnostic(TEXT("Control"), 0, Status.ServerEndTime >= 0 ? Status.ServerEndTime : Status.SegmentStartTime, 0, 0, UEnum::GetValueAsString(Status.State));
		if (!bActive)
		{
			for (const FPendingImpulse& Event : PendingImpulses) { WriteImpulse(Event, -1, TEXT("Cancelled")); }
			PendingImpulses.Reset();
		}
		if (bActive) { TailDeadline = -1; }
		else if (Status.State == ESharpEnemyTrialState::Idle || Status.State == ESharpEnemyTrialState::Ready) { Finish(TEXT("Released")); }
		else if (TailDeadline < 0) { TailDeadline = GetWorld()->GetTimeSeconds() + Context.Recording.PostTrialObservationSeconds; }
	}
}

void USharpMovementRecorderComponent::StartRecording()
{
	RecordedActorId = Enemy->ExperimentActorId; Manifest.Reset();
	const FString Directory = FPaths::Combine(FSharpRecordingFiles::RunDirectory(Context.Identity.RunId, Context.Identity.TrialId),
		FString::Printf(TEXT("Actor_%d/Generation_%d/Segment_%d"), Enemy->ExperimentActorId, Context.Generation, Context.Segment),
		FSharpRecordingFiles::ProcessRole(*GetWorld()));
	if (!Files.Open(Directory, Context.Recording.MaximumBufferedRows))
	{
		ClosedGeneration = Context.Generation; ClosedSegment = Context.Segment; return;
	}
	bRecording = true; TailDeadline = -1; FlushTime = GetWorld()->GetTimeSeconds();
	#if ENABLE_VISUAL_LOG
	if (Context.Recording.bVisualLogger)
	{
		if (VisualLoggerUsers++ == 0) { bWasVisualLoggerRecording = FVisualLogger::IsRecording(); FVisualLogger::Get().SetIsRecording(true); }
		bVisualLoggerLease = true;
	}
	#endif
	WriteManifest(TEXT("Recording"));
}

void USharpMovementRecorderComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!Enemy || !Enemy->HasAuthority() || !bRecording) { return; }
	const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	const FVector Position = Enemy->GetActorLocation(), Velocity = Movement->Velocity;
	const double Now = GetWorld()->GetTimeSeconds();
	Files.Add(TEXT("server_samples.csv"), CommonHeader + TEXT(",PosX,PosY,PosZ,VelX,VelY,VelZ,MovementMode,CustomMovementMode,DeltaSeconds"),
		CommonRow(Now) + FString::Printf(TEXT(",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%.6f"),
			Position.X, Position.Y, Position.Z, Velocity.X, Velocity.Y, Velocity.Z, static_cast<uint8>(Movement->MovementMode), Movement->CustomMovementMode, DeltaSeconds));
	for (int32 I = PendingImpulses.Num() - 1; I >= 0; --I)
	{
		FPendingImpulse& Event = PendingImpulses[I];
		if (Event.CompleteTime < 0 || Now < Event.CallTime) { continue; }
		if (Event.FirstSampleTime < 0) { Event.FirstSampleTime = Now; }
		// Overlap can enqueue an impulse during an already-running movement update.
		// Do not call its incidental velocity change an applied impulse before CMC consumes the forces.
		if (Event.AppliedTime >= 0 && (Velocity - Event.BeforeVelocity).Size() >= Context.Recording.ObservableVelocityChange)
		{
			WriteImpulse(Event, Now, TEXT("Observed")); PendingImpulses.RemoveAt(I);
		}
	}
}

void USharpMovementRecorderComponent::ObserveReceipt(double SourceTime)
{
	++ReceiptCount; LastReceiptSourceTime = SourceTime;
	RecordDiagnostic(TEXT("MovementReceipt"), 0, SourceTime, 0, 0, TEXT("CMC"));
}

void USharpMovementRecorderComponent::RecordDiagnostic(const FString& Kind, uint32 Sequence, double SourceTime,
	double Value, double CostMicroseconds, const FString& Reason)
{
	if (!bRecording || !Enemy) { return; }
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Time = Enemy->HasAuthority() || !GameState ? GetWorld()->GetTimeSeconds() : GameState->GetServerWorldTimeSeconds();
	Files.Add(TEXT("diagnostics.csv"), CommonHeader + TEXT(",Kind,SequenceId,SourceTime,Value,CostMicroseconds,Reason"),
		CommonRow(Time) + FString::Printf(TEXT(",%s,%u,%.9f,%.6f,%.3f,%s"),
			*FSharpRecordingFiles::Csv(Kind), Sequence, SourceTime, Value, CostMicroseconds, *FSharpRecordingFiles::Csv(Reason)));
}

void USharpMovementRecorderComponent::RecordDetection(uint32 Sequence, const FSharpClassification& Result,
	double CostMicroseconds, const FString& Route)
{
	if (!bRecording || !Enemy) { return; }
	const FVector Position = Enemy->GetActorLocation(), Velocity = Enemy->GetVelocity();
	const double Now = GetWorld()->GetTimeSeconds();
	Files.Add(TEXT("detector_samples.csv"), CommonHeader + TEXT(",DeltaV,DeltaThetaDeg,Acceleration,Reasons,Risk,SharpEvent,CostMicroseconds"),
		CommonRow(Now) + FString::Printf(TEXT(",%.6f,%.6f,%.6f,%u,%d,%d,%.3f"), Result.DeltaVelocity, Result.DirectionDegrees,
			Result.Acceleration, Result.Reasons, Result.bRisk, Result.bSharpEvent, CostMicroseconds));
	if (!Result.bSharpEvent && !Result.bRiskEnded) { return; }
	Files.Add(TEXT("detector.csv"), CommonHeader + TEXT(",SequenceId,ObservedTime,PosX,PosY,PosZ,PostVelX,PostVelY,PostVelZ,MovementMode,DeltaV,DeltaThetaDeg,Reason,RiskState,CostMicroseconds,Route"),
		CommonRow(Now) + FString::Printf(TEXT(",%u,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%.6f,%.6f,%u,%s,%.3f,%s"),
			Sequence, Now, Position.X, Position.Y, Position.Z, Velocity.X, Velocity.Y, Velocity.Z,
			static_cast<uint8>(Enemy->GetCharacterMovement()->MovementMode), Result.DeltaVelocity, Result.DirectionDegrees, Result.Reasons,
			Result.bSharpEvent ? TEXT("Enter") : TEXT("Exit"), CostMicroseconds, *FSharpRecordingFiles::Csv(Route)));
}

uint32 USharpMovementRecorderComponent::BeginImpulse(FName VolumeId, const FVector& WorldImpulse, bool bVelocityChange, bool bIsSharpEvent)
{
	if (!bRecording || !Enemy || !Enemy->HasAuthority() || !Enemy->Experiment->IsObservingMovement() || NextEventId == MAX_uint32) { return 0; }
	if (PendingImpulses.Num() >= Context.Recording.MaximumPendingEvents)
	{
		WriteImpulse(PendingImpulses[0], -1, TEXT("PendingLimit")); PendingImpulses.RemoveAt(0);
	}
	FPendingImpulse Event;
	Event.Id = ++NextEventId; Event.VolumeId = VolumeId; Event.Impulse = WorldImpulse;
	Event.BeforeVelocity = Enemy->GetVelocity(); Event.CallTime = GetWorld()->GetTimeSeconds(); Event.WallTime = WallSeconds();
	Event.Frame = GFrameCounter; Event.bVelocityChange = bVelocityChange; Event.bIsSharpEvent = bIsSharpEvent; PendingImpulses.Add(Event);
	RecordDiagnostic(TEXT("ImpulseCall"), Event.Id, Event.CallTime, WorldImpulse.Size(), 0, VolumeId.ToString());
	return Event.Id;
}

void USharpMovementRecorderComponent::CompleteImpulse(uint32 EventId)
{
	for (FPendingImpulse& Event : PendingImpulses)
	{
		if (Event.Id == EventId) { Event.CompleteTime = GetWorld()->GetTimeSeconds(); break; }
	}
}

void USharpMovementRecorderComponent::ObserveForceApplication()
{
	if (!Enemy || !Enemy->HasAuthority() || !bRecording || !Enemy->Experiment->IsObservingMovement()) { return; }
	for (FPendingImpulse& Event : PendingImpulses)
	{
		if (Event.CompleteTime >= 0 && Event.AppliedTime < 0)
		{
			Event.AppliedTime = GetWorld()->GetTimeSeconds();
			RecordDiagnostic(TEXT("ImpulseApplied"), Event.Id, Event.AppliedTime, Enemy->GetVelocity().Size(), 0, Event.VolumeId.ToString());
		}
	}
}

void USharpMovementRecorderComponent::WriteImpulse(const FPendingImpulse& Event, double ObservedTime, const FString& Outcome)
{
	const FVector Velocity = Enemy->GetVelocity();
	Files.Add(TEXT("ground_truth.csv"), CommonHeader + TEXT(",EventId,VolumeId,ImpulseCallTime,CallCompleteTime,AppliedTime,FirstSampleTime,ActualVelocityChangeTime,ImpulseX,ImpulseY,ImpulseZ,PreVelX,PreVelY,PreVelZ,PostVelX,PostVelY,PostVelZ,VelocityChange,CallFrame,CallWallTime,Status,IsSharp"),
		CommonRow(GetWorld()->GetTimeSeconds()) + FString::Printf(TEXT(",%u,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%llu,%.6f,%s,%d"),
			Event.Id, *FSharpRecordingFiles::Csv(Event.VolumeId.ToString()), Event.CallTime, Event.CompleteTime, Event.AppliedTime, Event.FirstSampleTime, ObservedTime,
			Event.Impulse.X, Event.Impulse.Y, Event.Impulse.Z, Event.BeforeVelocity.X, Event.BeforeVelocity.Y, Event.BeforeVelocity.Z,
			Velocity.X, Velocity.Y, Velocity.Z, Event.bVelocityChange, static_cast<unsigned long long>(Event.Frame), Event.WallTime, *Outcome, Event.bIsSharpEvent));
}

void USharpMovementRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!Enemy) { return; }
	if (!bRecording) { HandleSessionChanged(); }
	if (!bRecording) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Enemy->HasAuthority())
	{
		const AGameStateBase* GameState = GetWorld()->GetGameState();
		if (GameState && Enemy->GetLocalRole() == ROLE_SimulatedProxy)
		{
			const FVector Capsule = Enemy->GetActorLocation();
			const FVector RawMesh = Enemy->GetMesh()->GetComponentLocation();
			const FVector BaseOffset = Enemy->GetCapsuleComponent()->GetComponentTransform().TransformVector(Enemy->GetBaseTranslationOffset());
			const FVector Mesh = RawMesh - BaseOffset;
			FString ClientId = TEXT("0"); FParse::Value(FCommandLine::Get(), TEXT("SharpClientId="), ClientId);
			Files.Add(TEXT("client_") + FPaths::MakeValidFileName(ClientId) + TEXT("_samples.csv"),
				CommonHeader + TEXT(",ClientId,CapsuleX,CapsuleY,CapsuleZ,MeshX,MeshY,MeshZ,RawMeshX,RawMeshY,RawMeshZ,UpdateCount,RxStateTime,FrameMs,Predicting"),
				CommonRow(GameState->GetServerWorldTimeSeconds()) + FString::Printf(TEXT(",%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%llu,%.9f,%.3f,%d"),
					*FSharpRecordingFiles::Csv(ClientId), Capsule.X, Capsule.Y, Capsule.Z, Mesh.X, Mesh.Y, Mesh.Z,
					RawMesh.X, RawMesh.Y, RawMesh.Z, static_cast<unsigned long long>(ReceiptCount), LastReceiptSourceTime, DeltaTime * 1000, Enemy->VisualPredictor->bPredicting));
		}
	}
	for (int32 I = PendingImpulses.Num() - 1; I >= 0; --I)
	{
		if (Now - PendingImpulses[I].CallTime >= Context.Recording.EventObservationSeconds)
		{
			WriteImpulse(PendingImpulses[I], -1, TEXT("NotObserved")); PendingImpulses.RemoveAt(I);
		}
	}
	if (Context.Recording.bDrawDebug)
	{
		DrawDebugCapsule(GetWorld(), Enemy->GetActorLocation(), Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
			Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius(), Enemy->GetActorQuat(), Enemy->HasAuthority() ? FColor::Green : FColor::Cyan, false, 0);
		DrawDebugDirectionalArrow(GetWorld(), Enemy->GetActorLocation(), Enemy->GetActorLocation() + Enemy->GetVelocity() * 0.2, 20, FColor::Yellow, false, 0);
		DrawDebugString(GetWorld(), Enemy->GetActorLocation() + FVector(0, 0, 120),
			FString::Printf(TEXT("Enemy %d | %s | G%d S%d | %s"), RecordedActorId, *Context.PathId.ToString(), Context.Generation, Context.Segment,
				*StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Context.Identity.Mode))), nullptr, FColor::White, 0);
	}
	if (Context.Recording.bVisualLogger)
	{
		UE_VLOG_LOCATION(Enemy, LogTemp, Log, Enemy->GetActorLocation(), 10, FColor::Green, TEXT("ActorId=%d G=%d S=%d"), Enemy->ExperimentActorId, Context.Generation, Context.Segment);
	}
	if (Now - FlushTime >= Context.Recording.FlushIntervalSeconds) { Files.Flush(); FlushTime = Now; }
	if (!Files.IsHealthy()) { Finish(TEXT("WriteFailed")); }
	else if (TailDeadline >= 0 && Now >= TailDeadline) { Finish(UEnum::GetValueAsString(Context.State)); }
}

void USharpMovementRecorderComponent::Finish(const FString& Reason)
{
	if (!bRecording) { return; }
	for (const FPendingImpulse& Event : PendingImpulses) { WriteImpulse(Event, -1, TEXT("Cancelled")); }
	PendingImpulses.Reset(); Files.Flush(); WriteManifest(Reason); Files.Close(); bRecording = false; TailDeadline = -1;
	ClosedGeneration = Context.Generation; ClosedSegment = Context.Segment;
	#if ENABLE_VISUAL_LOG
	if (bVisualLoggerLease)
	{
		bVisualLoggerLease = false;
		if (--VisualLoggerUsers == 0) { FVisualLogger::Get().SetIsRecording(bWasVisualLoggerRecording); }
	}
	#endif
}

void USharpMovementRecorderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Finish(TEXT("EndPlay"));
	if (Enemy) { Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementRecorderComponent::ObserveMovement); }
	Super::EndPlay(Reason);
}
