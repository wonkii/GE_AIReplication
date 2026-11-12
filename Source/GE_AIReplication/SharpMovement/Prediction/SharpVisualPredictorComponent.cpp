#include "SharpVisualPredictorComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Combat/HitReactionComponent.h"

USharpVisualPredictorComponent::USharpVisualPredictorComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USharpVisualPredictorComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy) { AddTickPrerequisiteComponent(Enemy->GetCharacterMovement()); HandleSessionChanged(); }
}

void USharpVisualPredictorComponent::SetIdleTickOptimizationEnabled(bool bEnabled)
{
	bOptimizeIdleTick = bEnabled;
	RefreshTickScheduling();
}

void USharpVisualPredictorComponent::RefreshTickScheduling()
{
	if (!HasBegunPlay() || !Enemy || !Enemy->Experiment) { return; }
    const ESharpExperimentMode Mode = Enemy->Experiment->Status.Identity.Mode;
    const bool bEventTransport = Mode == ESharpExperimentMode::EventOnly || Mode == ESharpExperimentMode::EventPredictor;
    bool bNeedsTick = !bEventTransport;
    if (bNeedsTick && bOptimizeIdleTick)
    {
        bNeedsTick = Enemy->GetLocalRole() == ROLE_SimulatedProxy && (bPending || bOwnsPresentation);
    }
	if (IsComponentTickEnabled() != bNeedsTick) { SetComponentTickEnabled(bNeedsTick); }
}

void USharpVisualPredictorComponent::HandleSessionChanged()
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy) { return; }
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
    if (Status.Identity.Mode == ESharpExperimentMode::EventOnly || Status.Identity.Mode == ESharpExperimentMode::EventPredictor)
    {
        bPending = false;
        SetOwnership(false);
    }
	if (Generation != Status.Generation || Segment != Status.Segment)
	{
		SetOwnership(false); bPending = false; Generation = Status.Generation; Segment = Status.Segment;
	}
	if (Status.State == ESharpEnemyTrialState::Idle || Status.State == ESharpEnemyTrialState::Ready ||
		Status.State == ESharpEnemyTrialState::Failed)
	{
		SetOwnership(false); bPending = false;
	}
	RefreshTickScheduling();
}

FVector USharpVisualPredictorComponent::BaselinePosition() const
{
	return Enemy->GetCapsuleComponent()->GetComponentTransform().TransformPosition(BaseRelativePosition);
}

void USharpVisualPredictorComponent::SetOwnership(bool bEnabled)
{
	if (!Enemy || !Enemy->GetMesh() || bEnabled == bOwnsPresentation) { return; }
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	if (bEnabled)
	{
		OriginalSmoothing = Movement->NetworkSmoothingMode;
		BaseRelativePosition = Enemy->GetBaseTranslationOffset();
		DisplayPosition = Enemy->GetMesh()->GetComponentLocation();
		ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0;
		Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	}
	else
	{
		Enemy->GetMesh()->SetRelativeLocationAndRotation(BaseRelativePosition, Enemy->GetBaseRotationOffset());
		Movement->NetworkSmoothingMode = OriginalSmoothing;
		if (FNetworkPredictionData_Client_Character* Data = Movement->GetPredictionData_Client_Character())
		{
			Data->MeshTranslationOffset = FVector::ZeroVector; Data->OriginalMeshTranslationOffset = FVector::ZeroVector;
			Data->MeshRotationOffset = FQuat::Identity; Data->MeshRotationTarget = FQuat::Identity;
		}
		bPredicting = false;
	}
	bOwnsPresentation = bEnabled;
	RefreshTickScheduling();
}

void USharpVisualPredictorComponent::QueueSnapshot(const FSharpMoveSnapshot& Snapshot)
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
    // Event modes exclusively use the capsule-level SnapshotChannel now.
    if (!Enemy || Enemy->GetLocalRole() != ROLE_SimulatedProxy || Enemy->HitReaction->IsReacting() || Snapshot.ServerTime <= CombatSnapshotCutoff ||
        Enemy->Experiment->Status.Identity.Mode == ESharpExperimentMode::EventOnly ||
        Enemy->Experiment->Status.Identity.Mode == ESharpExperimentMode::EventPredictor) { return; }
	Pending = Snapshot; bPending = true;
	RefreshTickScheduling();
}

void USharpVisualPredictorComponent::HandleCombatChanged()
{
	if (!Enemy) Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (!Enemy) return;
	if (const auto* GameState = GetWorld()->GetGameState()) CombatSnapshotCutoff = GameState->GetServerWorldTimeSeconds();
	bPending = false; StopPrediction(TEXT("CombatReaction")); SetOwnership(false);
	RefreshTickScheduling();
}

void USharpVisualPredictorComponent::NotifyCorrection(const FVector& OldMeshPosition)
{
	if (!Enemy || !bOwnsPresentation) { return; }
	DisplayPosition = OldMeshPosition;
	Enemy->GetMesh()->SetWorldLocation(DisplayPosition, false, nullptr, ETeleportType::TeleportPhysics);
	ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0;
}

void USharpVisualPredictorComponent::StopPrediction(const FString& Reason)
{
	if (!bPredicting || !Enemy) { return; }
	Enemy->Recorder->RecordDiagnostic(TEXT("PredictionStop"), Active.SequenceId, Active.ServerTime, 0, 0, Reason);
	bPredicting = false; LastStopReason = Reason;
	ConvergenceOffset = DisplayPosition - BaselinePosition(); ConvergenceElapsed = 0;
}

void USharpVisualPredictorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!Enemy || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !Enemy->GetMesh()) { return; }
	if (Enemy->HitReaction->IsReacting()) { bPending = false; SetOwnership(false); return; }
	HandleSessionChanged();
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	const FSharpVisualPredictionSettings& Settings = Status.Prediction;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState || !Settings.IsValid()) { SetOwnership(false); return; }
	const double Now = GameState->GetServerWorldTimeSeconds();
	const bool bActive = Status.State == ESharpEnemyTrialState::Running || Status.State == ESharpEnemyTrialState::Suspended;
    // Kept only for legacy visual-correction code; new event modes never use it.
    const bool bWanted = false;
    const bool bCustomCorrection = false;
	if (bCustomCorrection) { SetOwnership(true); }
	if (bPending)
	{
		bPending = false;
		const double Age = Now - Pending.ServerTime;
		const bool bModel = Pending.PredictionType == ESharpPredictionType::ConstantVelocity ||
			(Pending.PredictionType == ESharpPredictionType::Airborne && Settings.bEnableAirborneModel);
		if (bWanted && bModel && Age >= -Settings.FutureTimeTolerance && Age <= Settings.MaximumPredictionSeconds &&
			Pending.Generation == Generation && Pending.Segment == Segment &&
			Enemy->GetReplicatedServerLastTransformUpdateTimeStamp() <= Pending.ServerTime + KINDA_SMALL_NUMBER)
		{
			if (Age < 0) { bPending = true; }
			else
			{
				SetOwnership(true); Active = Pending; bPredicting = true; BlendStart = DisplayPosition; BlendElapsed = 0;
				Enemy->Recorder->RecordDiagnostic(TEXT("PredictionStart"), Active.SequenceId, Active.ServerTime, Age, 0, TEXT("AcceptedLatestState"));
			}
		}
		else if (Status.Identity.Mode == ESharpExperimentMode::EventOnly)
		{
			Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotObserved"), Pending.SequenceId, Pending.ServerTime, Age, 0, TEXT("EventOnly"));
		}
		else { Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotRejected"), Pending.SequenceId, Pending.ServerTime, Age, 0, TEXT("InactiveModelExpiredFutureOrSuperseded")); }
	}
	if (!bWanted) { StopPrediction(TEXT("TrialEnded")); }
	if (!bOwnsPresentation) { RefreshTickScheduling(); return; }
	const FVector Baseline = BaselinePosition();
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	if (bPredicting)
	{
		const double Age = Now - Active.ServerTime;
		if (Age < 0 || Age > Settings.MaximumPredictionSeconds) { StopPrediction(TEXT("Horizon")); }
		else if (Movement->MovementMode != Active.MovementMode || Movement->CustomMovementMode != Active.CustomMovementMode) { StopPrediction(TEXT("MovementModeChanged")); }
		else if (Enemy->GetReplicatedServerLastTransformUpdateTimeStamp() > Active.ServerTime + KINDA_SMALL_NUMBER) { StopPrediction(TEXT("NewServerMovement")); }
		else
		{
			FVector CapsuleTarget = Active.StartPosition + Active.PostVelocity * Age;
			if (Active.PredictionType == ESharpPredictionType::Airborne) { CapsuleTarget += Active.Gravity * (0.5 * Age * Age); }
			const FVector Target = CapsuleTarget + (Baseline - Enemy->GetActorLocation());
			bool bBlocked = false;
			if (Settings.bCollisionGuard)
			{
				FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(SharpPrediction), false, Enemy);
				const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
				bBlocked = GetWorld()->SweepSingleByChannel(Hit, Enemy->GetActorLocation(), CapsuleTarget, Capsule->GetComponentQuat(), Settings.CollisionChannel,
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() * Settings.CollisionCapsuleScale,
						Capsule->GetScaledCapsuleHalfHeight() * Settings.CollisionCapsuleScale), Params);
			}
			if (bBlocked || (Target - Baseline).Size() > Settings.MaximumVisualOffset) { StopPrediction(bBlocked ? TEXT("Collision") : TEXT("OffsetBound")); }
			else
			{
				BlendElapsed += DeltaTime;
				const float Alpha = Settings.PredictionBlendSeconds <= 0 ? 1 : FMath::Clamp(BlendElapsed / Settings.PredictionBlendSeconds, 0.0f, 1.0f);
				DisplayPosition = FMath::Lerp(BlendStart, Target, Alpha);
			}
		}
	}
	if (!bPredicting)
	{
		ConvergenceElapsed += DeltaTime;
		const float Alpha = Settings.CorrectionSeconds <= 0 ? 1 : FMath::Clamp(ConvergenceElapsed / Settings.CorrectionSeconds, 0.0f, 1.0f);
		DisplayPosition = Baseline + ConvergenceOffset.GetClampedToMaxSize(Settings.MaximumVisualOffset) * (1 - Alpha);
		if (Alpha >= 1 && !bCustomCorrection) { SetOwnership(false); return; }
	}
	Enemy->GetMesh()->SetWorldLocation(DisplayPosition, false, nullptr, ETeleportType::TeleportPhysics);
}

void USharpVisualPredictorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetOwnership(false); Super::EndPlay(Reason);
}
