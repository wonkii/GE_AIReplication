#include "SharpVisualPredictorComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"

USharpVisualPredictorComponent::USharpVisualPredictorComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USharpVisualPredictorComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy)
	{
		AddTickPrerequisiteComponent(Enemy->GetCharacterMovement());
		// Initial RepNotify may run before component BeginPlay on a newly relevant actor.
		QueueSnapshot(Enemy->SharpMoveSnapshot);
	}
}

void USharpVisualPredictorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetOwnership(false); Super::EndPlay(Reason);
}

FVector USharpVisualPredictorComponent::BaselinePosition() const
{
	return Enemy->GetCapsuleComponent()->GetComponentTransform().TransformPosition(BaseRelativePosition);
}

void USharpVisualPredictorComponent::SetOwnership(bool bEnabled)
{
	if (!Enemy || bEnabled == bOwnsPresentation) { return; }
	UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement();
	if (bEnabled)
	{
		OriginalSmoothing = CMC->NetworkSmoothingMode;
		BaseRelativePosition = Enemy->GetBaseTranslationOffset();
		DisplayPosition = Enemy->GetMesh()->GetComponentLocation();
		ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0.0f;
		CMC->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	}
	else
	{
		Enemy->GetMesh()->SetRelativeLocation(BaseRelativePosition);
		CMC->NetworkSmoothingMode = OriginalSmoothing;
		if (FNetworkPredictionData_Client_Character* Data = CMC->GetPredictionData_Client_Character())
		{
			Data->MeshTranslationOffset = FVector::ZeroVector; Data->OriginalMeshTranslationOffset = FVector::ZeroVector;
			Data->MeshRotationOffset = FQuat::Identity; Data->MeshRotationTarget = FQuat::Identity;
		}
		bPredicting = false;
	}
	bOwnsPresentation = bEnabled;
}

void USharpVisualPredictorComponent::QueueSnapshot(const FSharpMoveSnapshot& Snapshot)
{
	if (!Enemy || Enemy->GetLocalRole() != ROLE_SimulatedProxy ||
		(bHasSequence && !FSharpMoveSnapshot::IsNewer(Snapshot.SequenceId, LastSequence))) { return; }
	if (bHasSequence && Snapshot.SequenceId - LastSequence > 1)
	{
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("SnapshotSequenceGap"), Snapshot.SequenceId, Snapshot.ServerTime,
			Snapshot.SequenceId - LastSequence - 1, 0.0, TEXT("LatestStateMayCoalesce"));
	}
	LastSequence = Snapshot.SequenceId; bHasSequence = true;
	if (!Snapshot.bValid && bPredicting) { StopPrediction(TEXT("SnapshotInvalidated")); }
	Pending = Snapshot; bPending = true;
}

void USharpVisualPredictorComponent::NotifyCorrection(const FVector& OldMeshPosition)
{
	if (!bOwnsPresentation || !Enemy) { return; }
	DisplayPosition = OldMeshPosition;
	Enemy->GetMesh()->SetWorldLocation(DisplayPosition, false, nullptr, ETeleportType::TeleportPhysics);
	ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0.0f;
}

void USharpVisualPredictorComponent::StopPrediction(const FString& Reason)
{
	if (bPredicting && Enemy)
	{
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("PredictionStop"), Active.SequenceId, Active.ServerTime, 0.0, 0.0, Reason);
	}
	bPredicting = false; LastStopReason = Reason;
	ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0.0f;
}

void USharpVisualPredictorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!Enemy || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !Enemy->GetMesh()) { return; }
	const FSharpExperimentClientState& Runtime = Enemy->ClientState;
	const bool bActiveTrial = Runtime.State == ESharpTrialState::Running || Runtime.State == ESharpTrialState::Suspended;
	const ESharpExperimentMode Mode = Runtime.Context.Identity.Mode;
	const bool bWanted = bActiveTrial && (Mode == ESharpExperimentMode::EventPredictor ||
		(Mode == ESharpExperimentMode::EventOnly && Runtime.Prediction.bEventOnlyUsesCustomCorrection));
	if (RunId != Runtime.Context.Identity.RunId || TrialId != Runtime.Context.Identity.TrialId)
	{
		SetOwnership(false); RunId = Runtime.Context.Identity.RunId; TrialId = Runtime.Context.Identity.TrialId;
	}
	Settings = Runtime.Prediction;
	if (!Settings.IsValid()) { SetOwnership(false); return; }
	// Finish the visual convergence during the recorder tail; restore native smoothing afterwards.
	const AGameStateBase* GS = GetWorld()->GetGameState();
	if (!GS) { return; }
	const double Now = GS->GetServerWorldTimeSeconds();
	const bool bTail = bOwnsPresentation && Runtime.bHasStarted && Runtime.ServerEndTime > 0.0 && Now < Runtime.ServerEndTime + Runtime.Recording.PostTrialObservationSeconds;
	SetOwnership(bWanted || bTail);
	if (!bOwnsPresentation)
	{
		// A Snapshot may precede its trial metadata in property replication. Retain it until the metadata or expiry arrives.
		if (bPending && (!Pending.bValid || Now - Pending.ServerTime > Settings.MaximumPredictionSeconds ||
			(Pending.RunId == RunId && Pending.TrialId == TrialId && Runtime.ServerEndTime > 0.0))) { bPending = false; }
		return;
	}
	if (!bWanted && bPredicting) { StopPrediction(TEXT("TrialEnded")); }
	UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement();
	if (bPending)
	{
		const double Age = Now - Pending.ServerTime;
		if (Enemy->ActorId.IsNone()) { return; }
		if (!Pending.bValid || !Pending.IsFinite())
		{
			bPending = false;
		}
		else if (Pending.RunId != RunId || Pending.TrialId != TrialId || Pending.ActorId != Enemy->ActorId)
		{
			if (Age > Settings.MaximumPredictionSeconds || Pending.ActorId != Enemy->ActorId) { bPending = false; }
		}
		else if (Age < -Settings.FutureTimeTolerance || Age > Settings.MaximumPredictionSeconds ||
			(Enemy->MovementGeneration > 0 && Enemy->GetReplicatedServerLastTransformUpdateTimeStamp() > static_cast<float>(Pending.ServerTime)))
		{
			Enemy->MovementRecorder->RecordDiagnostic(TEXT("SnapshotRejected"), Pending.SequenceId, Pending.ServerTime, Age, 0.0, TEXT("ExpiredFutureOrSuperseded")); bPending = false;
		}
		else if (Age >= 0.0)
		{
			bPending = false;
			if (Mode == ESharpExperimentMode::EventPredictor && Settings.bEnablePrediction && Settings.MaximumPredictionSeconds > 0.0f &&
				(Pending.PredictionType == ESharpPredictionType::ConstantVelocity || (Pending.PredictionType == ESharpPredictionType::Airborne && Settings.bEnableAirborneModel)))
			{
				Active = Pending; bPredicting = true; AcceptedMovementGeneration = Enemy->MovementGeneration;
				BlendStart = DisplayPosition; BlendElapsed = 0.0f;
				Enemy->MovementRecorder->RecordDiagnostic(TEXT("PredictionStart"), Active.SequenceId, Active.ServerTime, Age, 0.0, TEXT("AcceptedLatestState"));
			}
		}
	}
	const FVector Baseline = BaselinePosition();
	if (bPredicting)
	{
		const double Age = Now - Active.ServerTime;
		if (Age < 0.0 || Age > Settings.MaximumPredictionSeconds) { StopPrediction(TEXT("Horizon")); }
		else if (CMC->MovementMode != Active.MovementMode || CMC->CustomMovementMode != Active.CustomMovementMode) { StopPrediction(TEXT("MovementModeChanged")); }
		else if (Enemy->MovementGeneration > AcceptedMovementGeneration && Enemy->GetReplicatedServerLastTransformUpdateTimeStamp() >= static_cast<float>(Active.ServerTime))
		{
			StopPrediction(TEXT("NewServerMovement"));
		}
		else
		{
			FVector PredictedCapsule = Active.StartPosition + Active.PostVelocity * Age;
			if (Active.PredictionType == ESharpPredictionType::Airborne) { PredictedCapsule += Active.Gravity * (0.5 * Age * Age); }
			const FVector Target = PredictedCapsule + (Baseline - Enemy->GetActorLocation());
			bool bBlocked = false;
			if (Settings.bCollisionGuard)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SharpVisualPrediction), false, Enemy);
				const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
				bBlocked = GetWorld()->SweepSingleByChannel(Hit, Enemy->GetActorLocation(), PredictedCapsule, Capsule->GetComponentQuat(), Settings.CollisionChannel,
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() * Settings.CollisionCapsuleScale,
						Capsule->GetScaledCapsuleHalfHeight() * Settings.CollisionCapsuleScale), Params);
			}
			if (bBlocked || (Target - Baseline).Size() > Settings.MaximumVisualOffset) { StopPrediction(bBlocked ? TEXT("Collision") : TEXT("OffsetBound")); }
			else
			{
				BlendElapsed += DeltaTime;
				const float Alpha = Settings.PredictionBlendSeconds <= 0.0f ? 1.0f : FMath::Clamp(BlendElapsed / Settings.PredictionBlendSeconds, 0.0f, 1.0f);
				DisplayPosition = FMath::Lerp(BlendStart, Target, Alpha);
			}
		}
	}
	if (!bPredicting)
	{
		ConvergenceElapsed += DeltaTime;
		const float Alpha = Settings.CorrectionSeconds <= 0.0f ? 1.0f : FMath::Clamp(ConvergenceElapsed / Settings.CorrectionSeconds, 0.0f, 1.0f);
		DisplayPosition = Baseline + ConvergenceOffset.GetClampedToMaxSize(Settings.MaximumVisualOffset) * (1.0f - Alpha);
	}
	// Only the Mesh location changes. No Capsule, CMC velocity or authority state is modified.
	Enemy->GetMesh()->SetWorldLocation(DisplayPosition, false, nullptr, ETeleportType::TeleportPhysics);
}
