#include "SharpSnapshotChannelComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "SharpUpdatePolicy.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpSnapshot, Log, All);

namespace
{
	bool IsPayloadValid(const FSharpMoveSnapshot& Value)
	{
		return Value.SequenceId != 0 && Value.Generation > 0 && Value.Segment > 0 && FMath::IsFinite(Value.ServerTime) && Value.ServerTime >= 0 &&
			!Value.StartPosition.ContainsNaN() && !Value.PostVelocity.ContainsNaN() &&
			!Value.Gravity.ContainsNaN() && Value.MovementMode < MOVE_MAX &&
			static_cast<uint8>(Value.PredictionType) <= static_cast<uint8>(ESharpPredictionType::Airborne);
	}
}

USharpSnapshotChannelComponent::USharpSnapshotChannelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USharpSnapshotChannelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USharpSnapshotChannelComponent, Snapshot);
}

bool USharpSnapshotChannelComponent::Publish(const FSharpMoveSnapshot& Value)
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (!Enemy || !Enemy->HasAuthority() || !Enemy->Experiment->IsObservingMovement() ||
		!Enemy->UpdatePolicy->IsPolicyInitialized() || !IsPayloadValid(Value) || Value.SequenceId <= LastPublishedSequence ||
		Value.Generation != Enemy->Experiment->Status.Generation || Value.Segment != Enemy->Experiment->Status.Segment ||
		Value.ServerTime < Enemy->Experiment->Status.SegmentStartTime ||
		Value.ServerTime > GetWorld()->GetTimeSeconds() + KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogSharpSnapshot, Warning, TEXT("[M5][SnapshotPublish] Rejected Actor=%s SequenceId=%u Generation=%d Reason=InvalidOrStaleExecution"),
			*GetNameSafe(Enemy), Value.SequenceId, Value.Generation); return false;
	}
	const ESharpExperimentMode Mode = Enemy->UpdatePolicy->GetActiveMode();
	if (Mode != ESharpExperimentMode::EventOnly && Mode != ESharpExperimentMode::EventPredictor) { return false; }
	LastPublishedSequence = Value.SequenceId;
	Snapshot = Value;
	Enemy->ForceNetUpdate();
	UE_LOG(LogSharpSnapshot, Display, TEXT("[M5][SnapshotPublish] Requested Actor=%s ActorId=%d Mode=%s SequenceId=%u Generation=%d Segment=%d ServerTime=%.3f StartPosition=%s PostVelocity=%s MovementMode=%u PredictionType=%u Action=SnapshotAndForceNetUpdate"),
		*Enemy->GetName(), Enemy->ExperimentActorId, *UEnum::GetValueAsString(Mode), Value.SequenceId, Value.Generation, Value.Segment, Value.ServerTime,
		*Value.StartPosition.ToCompactString(), *Value.PostVelocity.ToCompactString(), Value.MovementMode, static_cast<uint8>(Value.PredictionType));
	return true;
}

void USharpSnapshotChannelComponent::Invalidate()
{
	if (GetOwner() && GetOwner()->HasAuthority()) { Snapshot = FSharpMoveSnapshot(); }
}

void USharpSnapshotChannelComponent::HandleSessionChanged()
{
	// RepNotify order across components is unspecified. Reconsider a future
	// generation snapshot once its context arrives.
	if (GetOwner() && !GetOwner()->HasAuthority()) { OnRep_Snapshot(); }
}

void USharpSnapshotChannelComponent::OnRep_Snapshot()
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (!Enemy || Enemy->HasAuthority() || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !IsPayloadValid(Snapshot)) { return; }
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	if (Snapshot.Generation > Status.Generation) { return; }
	if (Snapshot.Generation == Status.Generation && Snapshot.Segment > Status.Segment) { return; }
	if (Status.State != ESharpEnemyTrialState::Running && Status.State != ESharpEnemyTrialState::Suspended && Status.State != ESharpEnemyTrialState::Completed) { return; }
	if (Snapshot.Generation != Status.Generation || Snapshot.Segment != Status.Segment || Status.SegmentStartTime < 0 || Snapshot.ServerTime < Status.SegmentStartTime ||
		(Status.ServerEndTime >= 0 && Snapshot.ServerTime > Status.ServerEndTime) || Snapshot.SequenceId <= LastObservedSequence) { return; }
	if (LastObservedSequence > 0 && LastObservedGeneration == Snapshot.Generation && LastObservedSegment == Snapshot.Segment &&
		Snapshot.SequenceId > LastObservedSequence + 1)
	{
		Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotSequenceGap"), Snapshot.SequenceId, Snapshot.ServerTime,
			Snapshot.SequenceId - LastObservedSequence - 1, 0, TEXT("LatestStateMayCoalesce"));
	}
	LastObservedSequence = Snapshot.SequenceId;
	LastObservedGeneration = Snapshot.Generation; LastObservedSegment = Snapshot.Segment;
	Enemy->Recorder->RecordDiagnostic(TEXT("SnapshotReceived"), Snapshot.SequenceId, Snapshot.ServerTime, 0, 0, TEXT("AcceptedContext"));
	Enemy->VisualPredictor->QueueSnapshot(Snapshot);
	UE_LOG(LogSharpSnapshot, Display, TEXT("[M5][SnapshotOnRep] Received World=%s Actor=%s ActorId=%d SequenceId=%u Generation=%d Segment=%d ServerTime=%.3f LocalTime=%.3f StartPosition=%s PostVelocity=%s MovementMode=%u PredictionType=%u CapsulePosition=%s MeshPosition=%s Action=QueueForPresentation"),
		*Enemy->GetWorld()->GetPathName(), *Enemy->GetName(), Enemy->ExperimentActorId, Snapshot.SequenceId, Snapshot.Generation, Snapshot.Segment, Snapshot.ServerTime,
		GetWorld()->GetTimeSeconds(), *Snapshot.StartPosition.ToCompactString(), *Snapshot.PostVelocity.ToCompactString(),
		Snapshot.MovementMode, static_cast<uint8>(Snapshot.PredictionType), *Enemy->GetActorLocation().ToCompactString(), *Enemy->GetMesh()->GetComponentLocation().ToCompactString());
}
