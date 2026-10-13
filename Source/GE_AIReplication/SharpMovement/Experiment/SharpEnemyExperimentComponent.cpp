#include "SharpEnemyExperimentComponent.h"
#include "SharpExperimentManager.h"
#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Enemy/SharpEnemyAIController.h"
#include "../Measurement/SharpMovementDiagnosticsComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
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
	if (!IsValid(Enemy) || !Enemy->HasAuthority() || !IsValid(Manager) || !IsValid(Path) ||
		!Manager->IsRegistered(Path) || Path->AssignedEnemy != Enemy || Enemy->ExperimentActorId != PreparedActorId ||
		Path->PathId != Status.PathId || Path->PathRevision != Status.PathRevision ||
		Manager->Identity.RunId != Status.Identity.RunId || Manager->Identity.TrialId != Status.Identity.TrialId ||
		Manager->Identity.Mode != Status.Identity.Mode || !IsValid(PreparedSettings))
	{
		OutReason = TEXT("Prepared registration, identity or Enemy changed; reset the trial"); return false;
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
	if (!Enemy || !Enemy->HasAuthority() || !IsValid(InManager) || !IsValid(InPath) || !IsValid(InManager->Settings) ||
		InPath->AssignedEnemy != Enemy || !InManager->IsRegistered(InPath) || Status.Generation == MAX_int32)
	{
		OutReason = TEXT("Invalid server session binding or generation exhausted"); return false;
	}
	Release();
	PreparedNetHz = Enemy->GetNetUpdateFrequency(); PreparedMinNetHz = Enemy->GetMinNetUpdateFrequency();
	Manager = InManager;
	Path = InPath;
	PreparedActorId = Enemy->ExperimentActorId;
	// A running experiment never observes edits to a shared settings asset.
	PreparedSettings = DuplicateObject<USharpExperimentSettings>(InManager->Settings, this);
	const int32 NextGeneration = Status.Generation + 1;
	Status = FSharpEnemyTrialStatus();
	Status.Identity = Identity;
	Status.PathId = Path->PathId;
	Status.PathRevision = Path->PathRevision;
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
	if (!Enemy->SplineFollower->Prepare(Path, OutReason)) { Fail(OutReason); return false; }
	Enemy->Diagnostics->HandleSessionChanged();
	return true;
}

bool USharpEnemyExperimentComponent::CanStart(FString& OutReason) const
{
	if (Status.State != ESharpEnemyTrialState::Ready && Status.State != ESharpEnemyTrialState::Stopped && Status.State != ESharpEnemyTrialState::Suspended)
	{
		OutReason = TEXT("Only Ready, Stopped or Suspended Enemy can start; reset Completed/Failed Enemy"); return false;
	}
	return ValidateBinding(OutReason) && Enemy->SplineFollower->CanStart(OutReason) &&
		Enemy->UpdatePolicy->ValidateConfiguration(Status.Identity.Mode, PreparedSettings, OutReason);
}

bool USharpEnemyExperimentComponent::Start(FString& OutReason)
{
	CancelImpulseResume();
	if (Status.State == ESharpEnemyTrialState::Running) { return ValidateBinding(OutReason); }
	if (!CanStart(OutReason)) { return false; }
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
	if (!Enemy->SplineFollower->Start(OutReason)) { Fail(OutReason); return false; }
	return true;
}

void USharpEnemyExperimentComponent::Stop()
{
	CancelImpulseResume();
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	Enemy->SplineFollower->Stop();
}

void USharpEnemyExperimentComponent::SuspendInput()
{
	CancelImpulseResume();
	if (Enemy && Enemy->HasAuthority() && IsExecuting()) { Enemy->SplineFollower->Suspend(); }
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
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(ImpulseResumeTimer); }
}

bool USharpEnemyExperimentComponent::SuspendForImpulse(float Seconds)
{
	if (!Enemy || !Enemy->HasAuthority() || !IsExecuting() || !FMath::IsFinite(Seconds) || Seconds <= 0) { return false; }
	SuspendInput();
	const int32 CapturedGeneration = Status.Generation, CapturedSegment = Status.Segment;
	GetWorld()->GetTimerManager().SetTimer(ImpulseResumeTimer, FTimerDelegate::CreateWeakLambda(this, [this, CapturedGeneration, CapturedSegment]()
	{
		if (Status.Generation != CapturedGeneration || Status.Segment != CapturedSegment || Status.State != ESharpEnemyTrialState::Suspended) { return; }
		// Impulse suspension belongs to the same observation segment: preserve the Detector and Recorder context.
		FString Reason;
		if (!ValidateBinding(Reason) || !Enemy->SplineFollower->Resume(Reason)) { Fail(Reason); }
	}), Seconds, false);
	return true;
}

void USharpEnemyExperimentComponent::Complete()
{
	if (!Enemy || !Enemy->HasAuthority() || !IsObservingMovement()) { return; }
	CancelImpulseResume(); Enemy->SplineFollower->Stop();
	SetState(ESharpEnemyTrialState::Completed);
}

void USharpEnemyExperimentComponent::Fail(const FString& Reason)
{
	CancelImpulseResume();
	if (!Enemy || !Enemy->HasAuthority() || Status.Generation == 0) { return; }
	Enemy->SplineFollower->Fail(Reason);
}

void USharpEnemyExperimentComponent::Release()
{
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
	Enemy->ConsumeMovementInputVector();
	Enemy->GetCharacterMovement()->StopMovementImmediately();
	Enemy->GetCharacterMovement()->ClearAccumulatedForces();
	Enemy->SnapshotChannel->Invalidate();
	Manager = nullptr;
	Path = nullptr;
	PreparedSettings = nullptr;
}

void USharpEnemyExperimentComponent::OnFollowStateChanged(USharpSplineFollowerComponent* Follower, ESharpSplineFollowState State, const FString& Reason)
{
	if (!Enemy || !Enemy->HasAuthority() || Follower != Enemy->SplineFollower) { return; }
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
		if (Enemy->HasAuthority()) { Release(); }
	}
	Super::EndPlay(EndPlayReason);
}
