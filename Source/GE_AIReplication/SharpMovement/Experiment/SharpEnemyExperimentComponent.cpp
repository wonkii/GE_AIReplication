#include "SharpEnemyExperimentComponent.h"
#include "SharpExperimentManager.h"
#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Enemy/SharpEnemyAIController.h"
#include "../Measurement/SharpMovementDiagnosticsComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Movement/SharpRandomWalkerComponent.h"
#include "../Net/SharpSnapshotChannelComponent.h"
#include "../Net/SharpUpdatePolicy.h"
#include "../Scenario/SharpEnemyPlacement.h"
#include "../Scenario/SharpSplinePath.h"
#include "../Scenario/SharpImpulseVolume.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Combat/HitReactionComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyTrial, Log, All);

USharpEnemyExperimentComponent::USharpEnemyExperimentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void USharpEnemyExperimentComponent::BeginPlay()
{
	Super::BeginPlay();
	Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy && Enemy->HasAuthority())
	{
		Enemy->SplineFollower->OnStateChanged.AddUniqueDynamic(this, &USharpEnemyExperimentComponent::OnFollowStateChanged);
		Enemy->SplineFollower->AddTickPrerequisiteComponent(this);
		Enemy->RandomWalker->OnStateChanged.AddUniqueDynamic(this, &USharpEnemyExperimentComponent::OnRandomWalkStateChanged);
		Enemy->RandomWalker->AddTickPrerequisiteComponent(this);
	}
}

void USharpEnemyExperimentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FString Reason;
	if (!ValidateBinding(Reason)) { Fail(Reason); }
}

void USharpEnemyExperimentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USharpEnemyExperimentComponent, Status);
}

bool USharpEnemyExperimentComponent::Owns(const ASharpExperimentManager* InManager, const ASharpSplinePath* InPath) const
{
	return Manager == InManager && Path == InPath;
}

bool USharpEnemyExperimentComponent::ValidateBinding(FString& OutReason) const
{
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(Manager) ||
		Enemy->ExperimentActorId != PreparedActorId || Enemy->ExperimentMovementSource != PreparedMovementSource ||
		Status.MovementSource != PreparedMovementSource ||
		Manager->Identity.RunId != Status.Identity.RunId || Manager->Identity.TrialId != Status.Identity.TrialId ||
		Manager->Identity.Mode != Status.Identity.Mode || !IsValid(PreparedSettings))
	{
		OutReason = TEXT("Prepared registration, movement source, identity or Enemy changed; reset the trial"); return false;
	}
	if (PreparedMovementSource == ESharpEnemyMovementSource::Spline)
	{
		if (!IsValid(Path) || !Manager->IsRegistered(Path) || Path->AssignedEnemy != Enemy ||
			Path->PathId != Status.PathId || Path->PathRevision != Status.PathRevision)
		{
			OutReason = TEXT("Prepared Spline or path identity changed; reset the trial"); return false;
		}
	}
	else if (IsValid(Path) || !Manager->IsRegisteredRandom(Enemy) ||
		Status.PathId != FName(*FString::Printf(TEXT("RW_%d"), PreparedActorId)) || Status.PathRevision != 1)
	{
		OutReason = TEXT("Prepared RandomWalk registration changed; reset the trial"); return false;
	}
	if (!Cast<ASharpEnemyAIController>(Enemy->GetController()))
	{
		OutReason = TEXT("Prepared Enemy lost its SharpEnemyAIController"); return false;
	}
	OutReason.Empty(); return true;
}

bool USharpEnemyExperimentComponent::Prepare(ASharpExperimentManager* InManager, ASharpSplinePath* InPath,
	const FSharpExperimentIdentity& Identity, const FTransform& StartTransform, FString& OutReason)
{
	if (!Enemy || !Enemy->HasAuthority() || !IsValid(InManager) || !IsValid(InManager->Settings) ||
		Status.Generation == MAX_int32 ||
		(Enemy->ExperimentMovementSource == ESharpEnemyMovementSource::Spline ?
			(!IsValid(InPath) || InPath->AssignedEnemy != Enemy || !InManager->IsRegistered(InPath)) :
			(IsValid(InPath) || !InManager->IsRegisteredRandom(Enemy))))
	{
		OutReason = TEXT("Invalid server session binding or generation exhausted"); return false;
	}
	Release();
	PreparedNetHz = Enemy->GetNetUpdateFrequency(); PreparedMinNetHz = Enemy->GetMinNetUpdateFrequency();
	Manager = InManager;
	Path = InPath;
	PreparedMovementSource = Enemy->ExperimentMovementSource;
	PreparedActorId = Enemy->ExperimentActorId;
	// A running experiment never observes edits to a shared settings asset.
	PreparedSettings = DuplicateObject<USharpExperimentSettings>(InManager->Settings, this);
	const int32 NextGeneration = Status.Generation + 1;
	Status = FSharpEnemyTrialStatus();
	Status.Identity = Identity;
	Status.MovementSource = PreparedMovementSource;
	Status.PathId = InPath ? InPath->PathId : FName(*FString::Printf(TEXT("RW_%d"), PreparedActorId));
	Status.PathRevision = InPath ? InPath->PathRevision : 1;
	Status.Generation = NextGeneration;
	Status.Recording = PreparedSettings->Recording;
	Status.Prediction = PreparedSettings->GetPredictionSettings();
	Enemy->SnapshotChannel->Invalidate();
	Enemy->ConsumeMovementInputVector();
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->ClearAccumulatedForces();
	if (!FSharpEnemyPlacement::Place(*Enemy, StartTransform, OutReason)) { Fail(OutReason); return false; }
	Movement->SetMovementMode(MOVE_Walking);
	for (TActorIterator<ASharpImpulseVolume> It(GetWorld()); It; ++It)
	{
		if (It->AppliesTo(Manager) && !It->PrepareEnemy(*Enemy, StartTransform, OutReason)) { Fail(OutReason); return false; }
	}
	if (PreparedMovementSource == ESharpEnemyMovementSource::Spline)
	{
		if (!Enemy->SplineFollower->Prepare(Path, OutReason)) { Fail(OutReason); return false; }
	}
	else if (!Enemy->RandomWalker->Prepare(PreparedActorId, OutReason)) { Fail(OutReason); return false; }
	Enemy->Diagnostics->HandleSessionChanged();
	return true;
}

bool USharpEnemyExperimentComponent::CanStart(FString& OutReason) const
{
	if (Enemy && Enemy->HitReaction->IsReacting()) { OutReason = TEXT("Cannot start during a combat reaction"); return false; }
	if (Status.State != ESharpEnemyTrialState::Ready && Status.State != ESharpEnemyTrialState::Stopped && Status.State != ESharpEnemyTrialState::Suspended)
	{
		OutReason = TEXT("Only Ready, Stopped or Suspended Enemy can start; reset Completed/Failed Enemy"); return false;
	}
	return ValidateBinding(OutReason) &&
		(PreparedMovementSource == ESharpEnemyMovementSource::Spline ?
			Enemy->SplineFollower->CanStart(OutReason) : Enemy->RandomWalker->CanStart(OutReason)) &&
		Enemy->UpdatePolicy->ValidateConfiguration(Status.Identity.Mode, PreparedSettings, OutReason);
}

bool USharpEnemyExperimentComponent::Start(FString& OutReason)
{
	if (Status.State == ESharpEnemyTrialState::Running) { return ValidateBinding(OutReason); }
	if (!CanStart(OutReason)) { return false; }
	CancelImpulseResume();
	if (Status.Segment == MAX_int32) { OutReason = TEXT("Segment exhausted; reset Enemy"); return false; }
	if (!Enemy->UpdatePolicy->IsPolicyInitialized())
	{
		ApplyBaseline();
		const bool bSuccess = Enemy->UpdatePolicy->InitializePolicy(Status.Identity.Mode, PreparedSettings);
		const FString Mode = StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Status.Identity.Mode));
		UE_LOG(LogSharpEnemyTrial, Display, TEXT("[M5][PolicyBind] Actor=%s ActorId=%d RunId=%s TrialId=%d Mode=%s InitSuccess=%d NetUpdateHz=%.1f Generation=%d"),
			*Enemy->GetName(), Enemy->ExperimentActorId, *Status.Identity.RunId, Status.Identity.TrialId, *Mode,
			bSuccess, Enemy->GetNetUpdateFrequency(), Status.Generation);
		if (!bSuccess) { OutReason = TEXT("Network policy initialization failed"); Fail(OutReason); return false; }
	}
	if (Status.ServerStartTime < 0) { Status.ServerStartTime = GetWorld()->GetTimeSeconds(); }
	Status.SegmentStartTime = GetWorld()->GetTimeSeconds();
	++Status.Segment;
	Status.ServerEndTime = -1.0;
	Enemy->Detector->Start(*PreparedSettings);
	if (!StartMovement(OutReason)) { Fail(OutReason); return false; }
    // A movement-free initial anchor must exist even before the first detector event.
    if (Status.Identity.Mode == ESharpExperimentMode::EventOnly || Status.Identity.Mode == ESharpExperimentMode::EventPredictor)
    {
        if (!Enemy->Detector->PublishStateKeyframe(true))
        {
            OutReason = TEXT("Could not publish event transport initial anchor");
            Fail(OutReason); return false;
        }
    }
	return true;
}

void USharpEnemyExperimentComponent::Stop()
{
	AbortCombatSuspension();
	CancelImpulseResume();
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	StopMovement();
}

void USharpEnemyExperimentComponent::SuspendInput()
{
	bResumeAfterCombat = false;
	CancelImpulseResume();
	if (Enemy && Enemy->HasAuthority() && IsExecuting()) { SuspendMovement(); }
}

bool USharpEnemyExperimentComponent::ResumeInput(FString& OutReason)
{
	if (Status.State != ESharpEnemyTrialState::Suspended) { OutReason = TEXT("Enemy input is not suspended"); return false; }
	return Start(OutReason);
}

void USharpEnemyExperimentComponent::RestorePolicy()
{
	if (Enemy && Enemy->HasAuthority() && Enemy->UpdatePolicy->IsPolicyInitialized()) { Enemy->UpdatePolicy->RestorePolicyState(); }
	if (Enemy && bBaselineOverridden)
	{
		Enemy->SetNetUpdateFrequency(OriginalNetHz); Enemy->SetMinNetUpdateFrequency(OriginalMinNetHz); bBaselineOverridden = false;
	}
}

void USharpEnemyExperimentComponent::ApplyBaseline()
{
	if (!Enemy || !PreparedSettings || !PreparedSettings->bUseControlledLowRate || bBaselineOverridden) { return; }
	OriginalNetHz = Enemy->GetNetUpdateFrequency(); OriginalMinNetHz = Enemy->GetMinNetUpdateFrequency();
	bBaselineOverridden = true;
	Enemy->SetNetUpdateFrequency(Status.Identity.Mode == ESharpExperimentMode::Adaptive ? PreparedSettings->HighNetUpdateFrequency : PreparedSettings->NormalNetUpdateFrequency);
	Enemy->SetMinNetUpdateFrequency(PreparedSettings->NormalNetUpdateFrequency);
}

void USharpEnemyExperimentComponent::CancelImpulseResume()
{
	bImpulseSuspended = false;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ImpulseResumeTimer); }
}

bool USharpEnemyExperimentComponent::SuspendForImpulse(float Seconds)
{
	if (!Enemy || !Enemy->HasAuthority() || !IsExecuting() || !FMath::IsFinite(Seconds) || Seconds <= 0) { return false; }
	SuspendInput();
	bImpulseSuspended = true;
	const int32 CapturedGeneration = Status.Generation, CapturedSegment = Status.Segment;
	GetWorld()->GetTimerManager().SetTimer(ImpulseResumeTimer, FTimerDelegate::CreateWeakLambda(this, [this, CapturedGeneration, CapturedSegment]()
	{
		if (Status.Generation != CapturedGeneration || Status.Segment != CapturedSegment || Status.State != ESharpEnemyTrialState::Suspended) { return; }
		bImpulseSuspended = false;
		if (CombatReactionId != 0) return;
		// Impulse suspension belongs to the same observation segment: preserve the Detector and Recorder context.
		FString Reason;
		if (!ValidateBinding(Reason) || !ResumeMovement(Reason)) { Fail(Reason); }
	}), Seconds, false);
	return true;
}

void USharpEnemyExperimentComponent::BeginCombatSuspension(int32 ReactionId)
{
	if (!Enemy || !Enemy->HasAuthority()) return;
	CombatReactionId = ReactionId; CombatGeneration = Status.Generation; CombatSegment = Status.Segment;
	bResumeAfterCombat = IsExecuting() || (Status.State == ESharpEnemyTrialState::Suspended && bImpulseSuspended);
	if (IsExecuting()) SuspendMovement();
}

void USharpEnemyExperimentComponent::EndCombatSuspension(int32 ReactionId)
{
	if (!Enemy || !Enemy->HasAuthority() || CombatReactionId != ReactionId) return;
	CombatReactionId = 0;
	const bool bResume = bResumeAfterCombat && CombatGeneration == Status.Generation && CombatSegment == Status.Segment &&
		Status.State == ESharpEnemyTrialState::Suspended && !bImpulseSuspended;
	bResumeAfterCombat = false;
	if (bResume)
	{
		FString Reason;
		if (!ValidateBinding(Reason) || !ResumeMovement(Reason)) Fail(Reason);
	}
}

void USharpEnemyExperimentComponent::AbortCombatSuspension()
{
	CombatReactionId = 0; bResumeAfterCombat = false;
	if (Enemy && Enemy->HasAuthority()) Enemy->HitReaction->CancelReaction();
}

void USharpEnemyExperimentComponent::Complete()
{
	if (!Enemy || !Enemy->HasAuthority() || !IsObservingMovement()) { return; }
	AbortCombatSuspension(); CancelImpulseResume(); StopMovement();
    // End of an event trial must restore normal Actor movement replication
    // before next Batch PrepareGroup validation, even if Stop emitted no callback.
    RestorePolicy();
	SetState(ESharpEnemyTrialState::Completed);
}

void USharpEnemyExperimentComponent::Fail(const FString& Reason)
{
	AbortCombatSuspension();
	CancelImpulseResume();
	if (!Enemy || !Enemy->HasAuthority() || Status.Generation == 0) { return; }
	if (PreparedMovementSource == ESharpEnemyMovementSource::Spline) { Enemy->SplineFollower->Fail(Reason); }
	else { Enemy->RandomWalker->Fail(Reason); }
}

void USharpEnemyExperimentComponent::Release()
{
	AbortCombatSuspension();
	CancelImpulseResume();
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	// Close the old context before any teleport or force/velocity changes.
	if (Status.Generation > 0)
	{
		Status.ServerEndTime = GetWorld()->GetTimeSeconds();
		SetState(ESharpEnemyTrialState::Idle, TEXT("Released"));
	}
	RestorePolicy();
	Enemy->SplineFollower->Reset();
	Enemy->RandomWalker->Reset();
	Enemy->ConsumeMovementInputVector();
	Enemy->GetCharacterMovement()->StopMovementImmediately();
	Enemy->GetCharacterMovement()->ClearAccumulatedForces();
	Enemy->SnapshotChannel->Invalidate();
	Manager = nullptr;
	Path = nullptr;
	PreparedSettings = nullptr;
}

bool USharpEnemyExperimentComponent::StartMovement(FString& OutReason)
{
	return PreparedMovementSource == ESharpEnemyMovementSource::Spline ?
		Enemy->SplineFollower->Start(OutReason) : Enemy->RandomWalker->Start(OutReason);
}

void USharpEnemyExperimentComponent::StopMovement()
{
	if (PreparedMovementSource == ESharpEnemyMovementSource::Spline) { Enemy->SplineFollower->Stop(); }
	else { Enemy->RandomWalker->Stop(); }
}

void USharpEnemyExperimentComponent::SuspendMovement()
{
	if (PreparedMovementSource == ESharpEnemyMovementSource::Spline) { Enemy->SplineFollower->Suspend(); }
	else { Enemy->RandomWalker->Suspend(); }
}

bool USharpEnemyExperimentComponent::ResumeMovement(FString& OutReason)
{
	return PreparedMovementSource == ESharpEnemyMovementSource::Spline ?
		Enemy->SplineFollower->Resume(OutReason) : Enemy->RandomWalker->Resume(OutReason);
}

void USharpEnemyExperimentComponent::OnRandomWalkStateChanged(USharpRandomWalkerComponent* Walker,
	ESharpRandomWalkState State, const FString& Reason)
{
	if (!Enemy || !Enemy->HasAuthority() || Walker != Enemy->RandomWalker ||
		PreparedMovementSource != ESharpEnemyMovementSource::RandomWalk) { return; }
	switch (State)
	{
	case ESharpRandomWalkState::Ready: SetState(ESharpEnemyTrialState::Ready); break;
	case ESharpRandomWalkState::Moving: SetState(ESharpEnemyTrialState::Running); break;
	case ESharpRandomWalkState::Stopped: RestorePolicy(); SetState(ESharpEnemyTrialState::Stopped); break;
	case ESharpRandomWalkState::Suspended: SetState(ESharpEnemyTrialState::Suspended); break;
	case ESharpRandomWalkState::Failed: RestorePolicy(); SetState(ESharpEnemyTrialState::Failed, Reason); break;
	case ESharpRandomWalkState::Idle:
		if (Status.State != ESharpEnemyTrialState::Idle)
		{
			RestorePolicy();
			Enemy->GetCharacterMovement()->StopMovementImmediately();
			SetState(ESharpEnemyTrialState::Failed, TEXT("RandomWalker reset outside session control"));
		}
		break;
	default: break;
	}
}

void USharpEnemyExperimentComponent::OnFollowStateChanged(USharpSplineFollowerComponent* Follower, ESharpSplineFollowState State, const FString& Reason)
{
	if (!Enemy || !Enemy->HasAuthority() || Follower != Enemy->SplineFollower ||
		PreparedMovementSource != ESharpEnemyMovementSource::Spline) { return; }
	switch (State)
	{
	case ESharpSplineFollowState::Ready: SetState(ESharpEnemyTrialState::Ready); break;
	case ESharpSplineFollowState::Following: SetState(ESharpEnemyTrialState::Running); break;
	case ESharpSplineFollowState::Stopped: RestorePolicy(); SetState(ESharpEnemyTrialState::Stopped); break;
	case ESharpSplineFollowState::Suspended: SetState(ESharpEnemyTrialState::Suspended); break;
	case ESharpSplineFollowState::Completed: RestorePolicy(); SetState(ESharpEnemyTrialState::Completed); break;
	case ESharpSplineFollowState::Failed: RestorePolicy(); SetState(ESharpEnemyTrialState::Failed, Reason); break;
	case ESharpSplineFollowState::Idle:
		if (Status.State != ESharpEnemyTrialState::Idle)
		{
			RestorePolicy();
			Enemy->GetCharacterMovement()->StopMovementImmediately();
			SetState(ESharpEnemyTrialState::Failed, TEXT("Follower was reset outside session control"));
		}
		break;
	default: break;
	}
}

void USharpEnemyExperimentComponent::SetState(ESharpEnemyTrialState State, const FString& Reason)
{
	Status.State = State;
	SetComponentTickEnabled(Enemy->HasAuthority() && (State == ESharpEnemyTrialState::Running || State == ESharpEnemyTrialState::Suspended));
	Status.FailureReason = Reason;
	if (State == ESharpEnemyTrialState::Completed || State == ESharpEnemyTrialState::Failed || State == ESharpEnemyTrialState::Stopped)
	{
		Status.ServerEndTime = GetWorld()->GetTimeSeconds();
	}
	OnRep_Status();
	if (!IsObservingMovement()) { Enemy->Detector->Stop(); CancelImpulseResume(); }
	if (Manager) { Manager->RefreshSummary(); }
	// Administrative control update, not a Detector event or policy request.
	Enemy->ForceNetUpdate();
}

void USharpEnemyExperimentComponent::OnRep_Status()
{
	ASharpEnemyCharacter* OwnerEnemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (!OwnerEnemy) { return; }
	OwnerEnemy->Diagnostics->HandleSessionChanged();
	OwnerEnemy->Recorder->HandleSessionChanged();
	OwnerEnemy->VisualPredictor->HandleSessionChanged();
	OwnerEnemy->SnapshotChannel->HandleSessionChanged();
	UE_LOG(LogSharpEnemyTrial, Display, TEXT("[Sharp][Control] Actor=%s ActorId=%d RunId=%s TrialId=%d Generation=%d PathId=%s PathRevision=%d State=%s ServerTime=%.6f StartTime=%.6f EndTime=%.6f Segment=%d SegmentStartTime=%.6f Reason=%s"),
		*OwnerEnemy->GetName(), OwnerEnemy->ExperimentActorId, *Status.Identity.RunId, Status.Identity.TrialId, Status.Generation,
		*Status.PathId.ToString(), Status.PathRevision, *UEnum::GetValueAsString(Status.State), GetWorld()->GetTimeSeconds(),
		Status.ServerStartTime, Status.ServerEndTime, Status.Segment, Status.SegmentStartTime, *Status.FailureReason);
}

void USharpEnemyExperimentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Enemy)
	{
		Enemy->SplineFollower->OnStateChanged.RemoveDynamic(this, &USharpEnemyExperimentComponent::OnFollowStateChanged);
		Enemy->SplineFollower->RemoveTickPrerequisiteComponent(this);
		Enemy->RandomWalker->OnStateChanged.RemoveDynamic(this, &USharpEnemyExperimentComponent::OnRandomWalkStateChanged);
		Enemy->RandomWalker->RemoveTickPrerequisiteComponent(this);
		if (Enemy->HasAuthority()) { Release(); }
	}
	Super::EndPlay(EndPlayReason);
}
