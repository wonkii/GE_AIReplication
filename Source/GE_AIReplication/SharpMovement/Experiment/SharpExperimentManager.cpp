#include "SharpExperimentManager.h"

#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Enemy/SharpEnemyAIController.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Scenario/SharpSplinePath.h"
#include "../Scenario/SharpImpulseVolume.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "../Net/SharpUpdatePolicy.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpExperiment, Log, All);

ASharpExperimentManager::ASharpExperimentManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void ASharpExperimentManager::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}

	Identity.RunId = ConfiguredRunId;
	FParse::Value(FCommandLine::Get(), TEXT("SharpRunId="), Identity.RunId);
	if (Identity.RunId.IsEmpty())
	{
		Identity.RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	Identity.TrialId = ConfiguredTrialId;
	FParse::Value(FCommandLine::Get(), TEXT("SharpTrialId="), Identity.TrialId);
	Identity.TrialId = FMath::Max(1, Identity.TrialId);

	Identity.Mode = ConfiguredMode;
	ActiveModeSequence = ModeSequence;
	if (!ActiveModeSequence.IsEmpty()) { Identity.Mode = ActiveModeSequence[0]; }
	FString ModeOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("SharpMode="), ModeOverride))
	{
		const UEnum* ModeEnum = StaticEnum<ESharpExperimentMode>();
		const int64 Value = ModeEnum->GetValueByNameString(ModeOverride);
		if (Value != INDEX_NONE)
		{
			Identity.Mode = static_cast<ESharpExperimentMode>(Value);
			ActiveModeSequence.Reset();
		}
		else
		{
			UE_LOG(LogSharpExperiment, Error, TEXT("Unknown SharpMode '%s'; using configured mode"), *ModeOverride);
		}
	}

	LogStartup();
	ForceNetUpdate();
	if (bAutoStartTrial) { AutoStartTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ASharpExperimentManager::AutoPrepare); }
}

void ASharpExperimentManager::OnRep_Identity()
{
	LogStartup();
}

void ASharpExperimentManager::LogStartup() const
{
	if (!Settings)
	{
		UE_LOG(LogSharpExperiment, Error, TEXT("RunId=%s TrialId=%d Mode=%s Settings=MISSING"),
			*Identity.RunId, Identity.TrialId, *StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Identity.Mode)));
		return;
	}
	FString Reason;
	if (!Settings->IsValidConfiguration(Reason))
	{
		UE_LOG(LogSharpExperiment, Error, TEXT("Invalid settings asset %s: %s"), *GetNameSafe(Settings), *Reason);
	}
	UE_LOG(LogSharpExperiment, Display, TEXT("Role=%s RunId=%s TrialId=%d Mode=%s SettingsAsset=%s %s"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *Identity.RunId, Identity.TrialId,
		*StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Identity.Mode)),
		*GetNameSafe(Settings), *Settings->ToLogString());
}

void ASharpExperimentManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASharpExperimentManager, Identity);
	DOREPLIFETIME(ASharpExperimentManager, TrialStatus);
}

bool ASharpExperimentManager::CanControlTrial() const
{
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !HasActorBegunPlay() || !HasAuthority())
	{
		UE_LOG(LogSharpExperiment, Warning, TEXT("Trial buttons require the running server/Standalone world. Select the PIE server instance, or call these functions from a server Blueprint."));
		return false;
	}
	return true;
}

ASharpEnemyAIController* ASharpExperimentManager::GetEnemyController() const
{
	return IsValid(Enemy) ? Cast<ASharpEnemyAIController>(Enemy->GetController()) : nullptr;
}

bool ASharpExperimentManager::ValidateTrial(FString& OutReason) const
{
	if (!IsValid(Settings) || !Settings->IsValidConfiguration(OutReason))
	{
		if (!IsValid(Settings)) { OutReason = TEXT("Experiment Settings asset is missing"); }
		return false;
	}
	if (Identity.Mode == ESharpExperimentMode::EventPredictor && Settings->VisualPrediction.bEnablePrediction && !Settings->bReplicateMovementTimestamps)
	{
		OutReason = TEXT("EventPredictor requires Replicate Movement Timestamps to reject superseded snapshots"); return false;
	}
	if (!IsValid(Enemy) || Enemy->GetWorld() != GetWorld() || !Enemy->HasAuthority() || Enemy->ActorId.IsNone() || !GetEnemyController())
	{
		OutReason = TEXT("Assign a server SharpEnemy with a unique ActorId and SharpEnemyAIController possession");
		return false;
	}
	if (!Enemy->GetIsReplicated() || !Enemy->IsReplicatingMovement())
	{
		OutReason = TEXT("Enable Replicates and Replicate Movement on the Enemy");
		return false;
	}
	if (!IsValid(Path) || Path->GetWorld() != GetWorld() || !Path->ValidatePath(OutReason))
	{
		if (!IsValid(Path)) { OutReason = TEXT("Assign a SharpSplinePath"); }
		return false;
	}
	if (!Enemy->SplineFollower->Settings.IsValid(OutReason)) { return false; }
	if (!FMath::IsFinite(StartHeightOffset) || StartHeightOffset < 0.0f)
	{
		OutReason = TEXT("Start Height Offset must be finite and nonnegative");
		return false;
	}
	if (!FMath::IsFinite(AutoStartDelaySeconds) || AutoStartDelaySeconds < 0.0f ||
		!FMath::IsFinite(RepeatDelaySeconds) || RepeatDelaySeconds < 0.0f || RepeatCount < 1 ||
		!Enemy->MovementRecorder || !Enemy->MovementRecorder->Observation.IsValid(OutReason))
	{
		if (OutReason.IsEmpty()) { OutReason = TEXT("Invalid automation delays, repeat count or recorder"); }
		return false;
	}
	TSet<const ASharpImpulseVolume*> UniqueVolumes;
	for (const ASharpImpulseVolume* Volume : ImpulseVolumes)
	{
		if (!IsValid(Volume) || Volume->GetWorld() != GetWorld() || !Volume->HasAuthority() || UniqueVolumes.Contains(Volume))
		{
			OutReason = TEXT("ImpulseVolumes must contain unique server Volume instances from this world"); return false;
		}
		if (!Volume->ValidateConfiguration(OutReason)) { return false; }
		if (Enemy->GetCapsuleComponent()->GetCollisionResponseToChannel(Volume->TriggerBox->GetCollisionObjectType()) == ECR_Ignore)
		{
			OutReason = TEXT("Enemy Capsule ignores a Volume collision object channel"); return false;
		}
		UniqueVolumes.Add(Volume);
		for (TActorIterator<ASharpImpulseVolume> It(GetWorld()); It; ++It)
		{
			if (*It != Volume && It->VolumeId == Volume->VolumeId) { OutReason = TEXT("Duplicate VolumeId in this world"); return false; }
		}
		for (TActorIterator<ASharpExperimentManager> It(GetWorld()); It; ++It)
		{
			if (*It != this && It->ImpulseVolumes.Contains(Volume)) { OutReason = TEXT("A Volume is assigned to multiple Managers"); return false; }
		}
	}
	if (!ImpulseVolumes.IsEmpty() && (!Enemy->GetCapsuleComponent()->GetGenerateOverlapEvents() ||
		Enemy->GetCapsuleComponent()->GetCollisionObjectType() != ECC_Pawn || !Enemy->GetCapsuleComponent()->IsQueryCollisionEnabled()))
	{
		OutReason = TEXT("Enemy Capsule needs Pawn object type and Generate Overlap Events for M2"); return false;
	}
	for (TActorIterator<ASharpSplinePath> It(GetWorld()); It; ++It)
	{
		if (*It != Path && It->PathId == Path->PathId)
		{
			OutReason = TEXT("Duplicate PathId in this world");
			return false;
		}
	}
	for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Enemy && It->ActorId == Enemy->ActorId)
		{
			OutReason = TEXT("Duplicate Enemy ActorId in this world");
			return false;
		}
	}
	for (TActorIterator<ASharpExperimentManager> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->Enemy == Enemy)
		{
			OutReason = TEXT("Multiple Managers target the same Enemy");
			return false;
		}
	}
	OutReason.Empty();
	return true;
}

void ASharpExperimentManager::PrepareTrial()
{
	if (!CanControlTrial()) { return; }
	if (TrialStatus.State == ESharpTrialState::Running || TrialStatus.State == ESharpTrialState::Suspended)
	{
		UE_LOG(LogSharpExperiment, Warning, TEXT("Reset the active Trial before preparing another"));
		return;
	}
	StopM2(TEXT("Prepare"));
	if (IsValid(PreparedEnemy))
	{
		PreparedEnemy->SplineFollower->OnStateChanged.RemoveDynamic(this, &ASharpExperimentManager::OnFollowStateChanged);
		PreparedEnemy->SplineFollower->Reset();
		RestoreReplicationProfile();
		RemoveTickPrerequisiteComponent(PreparedEnemy->GetCharacterMovement());
	}
	PreparedEnemy = nullptr;
	FString Reason;
	TrialStatus.ServerStartTime = 0.0;
	if (!ValidateTrial(Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); return; }
	PreparedEnemy = Enemy;
	AddTickPrerequisiteComponent(PreparedEnemy->GetCharacterMovement());
	OriginalNetFrequency = Enemy->GetNetUpdateFrequency();
	OriginalMinNetFrequency = Enemy->GetMinNetUpdateFrequency();
	bHasOriginalReplicationProfile = true;
	if (BaselineProfile == ESharpBaselineProfile::ControlledLowRate)
	{
		Enemy->SetNetUpdateFrequency(Settings->NormalNetUpdateFrequency);
		Enemy->SetMinNetUpdateFrequency(FMath::Min(OriginalMinNetFrequency, Settings->NormalNetUpdateFrequency));
	}
	const bool bReverse = Enemy->SplineFollower->Settings.bReverseDirection;
	const float StartDistance = bReverse ? Path->GetLength() : 0.0f;
	FVector Direction = Path->GetDirection(StartDistance) * (bReverse ? -1.0f : 1.0f);
	Direction.Z = 0.0f;
	const FRotator Rotation = bAlignRotationToPath ? Direction.Rotation() : Enemy->GetActorRotation();
	if (!Enemy->PrepareAtStart(Path->GetLocation(StartDistance), Rotation, StartHeightOffset, Reason))
	{
		SetTrialState(ESharpTrialState::Failed, Reason);
		RestoreReplicationProfile();
		return;
	}
	Enemy->ExperimentContext.Identity = Identity;
	Enemy->ExperimentContext.PathId = Path->PathId;
	Enemy->ExperimentContext.PathRevision = Path->PathRevision;
	Enemy->InvalidateSnapshot();
	Enemy->ClientState = FSharpExperimentClientState();
	Enemy->ClientState.Context = Enemy->ExperimentContext;
	Enemy->ClientState.Recording = Settings->Recording;
	Enemy->ClientState.Prediction = Settings->MakePredictionSettings();
	for (ASharpImpulseVolume* Volume : ImpulseVolumes)
	{
		PreparedVolumes.Add(Volume);
		if (!Volume->Prepare(this, Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); RestoreReplicationProfile(); return; }
	}
	Enemy->SplineFollower->OnStateChanged.AddUniqueDynamic(this, &ASharpExperimentManager::OnFollowStateChanged);
	if (!GetEnemyController()->PrepareFollow(Path, Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); return; }
	SetTrialState(ESharpTrialState::Ready);
	UE_LOG(LogSharpExperiment, Display, TEXT("RunId=%s TrialId=%d ActorId=%s PathId=%s Revision=%d Baseline=%s NetHz=%.2f TargetSpeed=%.2f LookAhead=%.2f Reverse=%d Acceptance=%.2f Slowdown=%.2f"),
		*Identity.RunId, Identity.TrialId, *Enemy->ActorId.ToString(), *Path->PathId.ToString(), Path->PathRevision,
		*UEnum::GetValueAsString(BaselineProfile), Enemy->GetNetUpdateFrequency(), Enemy->SplineFollower->Settings.TargetSpeed,
		Enemy->SplineFollower->Settings.LookAheadDistance, bReverse, Enemy->SplineFollower->Settings.AcceptanceRadius,
		Enemy->SplineFollower->Settings.EndSlowdownDistance);
}

void ASharpExperimentManager::StartTrial()
{
	if (!CanControlTrial()) { return; }
	if (TrialStatus.State == ESharpTrialState::Idle) { PrepareTrial(); }
	if (TrialStatus.State != ESharpTrialState::Ready || Enemy != PreparedEnemy || !GetEnemyController())
	{
		UE_LOG(LogSharpExperiment, Warning, TEXT("Prepare or reset the Trial before starting"));
		return;
	}
	FString Reason;
	GetWorldTimerManager().ClearTimer(AutoStartTimer);
	if (!ValidateTrial(Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); return; }
	if (PreparedVolumes.Num() != ImpulseVolumes.Num() || PreparedVolumes.ContainsByPredicate([this](const TWeakObjectPtr<ASharpImpulseVolume>& Volume)
		{ return Volume.IsValid() && !ImpulseVolumes.Contains(Volume.Get()); }))
	{
		SetTrialState(ESharpTrialState::Failed, TEXT("Volume list changed after Prepare; reset the trial")); return;
	}
	for (const TWeakObjectPtr<ASharpImpulseVolume>& Volume : PreparedVolumes)
	{
		if (!Volume.IsValid() || !Volume->IsPreparedGeometryUnchanged() || Volume->ContainsTargetCapsule())
		{
			SetTrialState(ESharpTrialState::Failed, TEXT("Prepared Volume changed or intersects the start Capsule; reset the trial")); return;
		}
	}
	ActivePostTrialSeconds = Settings->Recording.PostTrialObservationSeconds;
	if (!Enemy->UpdatePolicy->Start(Identity.Mode, Settings, Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); return; }
	if (!Enemy->MovementRecorder->StartRecording(this, Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); return; }
	for (const TWeakObjectPtr<ASharpImpulseVolume>& Volume : PreparedVolumes) { if (Volume.IsValid()) { Volume->Arm(); } }
	TrialStatus.ServerStartTime = GetWorld()->GetTimeSeconds();
	Enemy->ClientState.Recording = Settings->Recording;
	Enemy->ClientState.Prediction = Settings->MakePredictionSettings();
	Enemy->ClientState.ServerStartTime = TrialStatus.ServerStartTime;
	Enemy->ClientState.bHasStarted = true;
	if (!GetEnemyController()->StartFollow(Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); }
}

void ASharpExperimentManager::SuspendTrial()
{
	CancelInputResume();
	if (CanControlTrial() && Enemy == PreparedEnemy && TrialStatus.State == ESharpTrialState::Running && GetEnemyController())
	{
		GetEnemyController()->SuspendFollow();
	}
}

void ASharpExperimentManager::ResumeTrial()
{
	CancelInputResume();
	if (!CanControlTrial() || Enemy != PreparedEnemy || TrialStatus.State != ESharpTrialState::Suspended || !GetEnemyController()) { return; }
	FString Reason;
	if (!GetEnemyController()->ResumeFollow(Reason)) { SetTrialState(ESharpTrialState::Failed, Reason); }
}

void ASharpExperimentManager::ResetTrial()
{
	if (!CanControlTrial()) { return; }
	StopM2(TEXT("Reset"));
	if (IsValid(PreparedEnemy))
	{
		PreparedEnemy->SplineFollower->OnStateChanged.RemoveDynamic(this, &ASharpExperimentManager::OnFollowStateChanged);
		PreparedEnemy->SplineFollower->Reset();
	}
	SetTrialState(ESharpTrialState::Idle);
	Identity.TrialId = Identity.TrialId < MAX_int32 ? Identity.TrialId + 1 : 1;
	PrepareTrial();
}

void ASharpExperimentManager::OnFollowStateChanged(ESharpSplineFollowState State, const FString& Reason)
{
	switch (State)
	{
	case ESharpSplineFollowState::Ready: SetTrialState(ESharpTrialState::Ready); break;
	case ESharpSplineFollowState::Following: SetTrialState(ESharpTrialState::Running); break;
	case ESharpSplineFollowState::Suspended: SetTrialState(ESharpTrialState::Suspended); break;
	case ESharpSplineFollowState::Completed: SetTrialState(ESharpTrialState::Completed); break;
	case ESharpSplineFollowState::Failed: SetTrialState(ESharpTrialState::Failed, Reason); break;
	case ESharpSplineFollowState::Idle:
		if (TrialStatus.State == ESharpTrialState::Running || TrialStatus.State == ESharpTrialState::Suspended)
		{
			SetTrialState(ESharpTrialState::Failed, TEXT("Follower reset or Controller possession was lost during the Trial"));
		}
		break;
	default: break;
	}
}

void ASharpExperimentManager::SetTrialState(ESharpTrialState State, const FString& Reason)
{
	TrialStatus.Identity = Identity;
	TrialStatus.State = State;
	TrialStatus.FailureReason = Reason;
	if (IsValid(PreparedEnemy))
	{
		PreparedEnemy->ClientState.State = State;
		if (State == ESharpTrialState::Completed || State == ESharpTrialState::Failed) { PreparedEnemy->ClientState.ServerEndTime = GetWorld()->GetTimeSeconds(); }
		PreparedEnemy->ForceNetUpdate();
	}
	OnRep_TrialStatus();
	if (State == ESharpTrialState::Completed || State == ESharpTrialState::Failed)
	{
		DisarmVolumes(); CancelInputResume();
		GetWorldTimerManager().ClearTimer(AutoStartTimer);
		GetWorldTimerManager().ClearTimer(RepeatTimer);
		if (!bFinishingRecording)
		{
			FinishReason = State == ESharpTrialState::Completed ? TEXT("Completed") : TEXT("Failed: ") + Reason;
			FinishDeadline = GetWorld()->GetTimeSeconds() + ActivePostTrialSeconds;
			FinishRequestFrame = GFrameCounter;
			bFinishingRecording = true;
		}
	}
}

void ASharpExperimentManager::OnRep_TrialStatus()
{
	UE_LOG(LogSharpExperiment, Display, TEXT("Role=%s RunId=%s TrialId=%d State=%s StartTime=%.3f Reason=%s"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *TrialStatus.Identity.RunId, TrialStatus.Identity.TrialId,
		*UEnum::GetValueAsString(TrialStatus.State), TrialStatus.ServerStartTime, *TrialStatus.FailureReason);
}

void ASharpExperimentManager::RestoreReplicationProfile()
{
	if (IsValid(PreparedEnemy) && bHasOriginalReplicationProfile)
	{
		PreparedEnemy->SetNetUpdateFrequency(OriginalNetFrequency);
		PreparedEnemy->SetMinNetUpdateFrequency(OriginalMinNetFrequency);
	}
	bHasOriginalReplicationProfile = false;
}

void ASharpExperimentManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopM2(TEXT("EndPlay"));
	if (IsValid(PreparedEnemy))
	{
		PreparedEnemy->SplineFollower->OnStateChanged.RemoveDynamic(this, &ASharpExperimentManager::OnFollowStateChanged);
		PreparedEnemy->SplineFollower->Reset();
		RemoveTickPrerequisiteComponent(PreparedEnemy->GetCharacterMovement());
	}
	RestoreReplicationProfile();
	Super::EndPlay(EndPlayReason);
}

void ASharpExperimentManager::DisarmVolumes()
{
	for (const TWeakObjectPtr<ASharpImpulseVolume>& Volume : PreparedVolumes) { if (Volume.IsValid()) { Volume->Disarm(); } }
	for (ASharpImpulseVolume* Volume : ImpulseVolumes) { if (IsValid(Volume)) { Volume->Disarm(); } }
}

void ASharpExperimentManager::CancelInputResume()
{
	GetWorldTimerManager().ClearTimer(InputResumeTimer);
	InputResumeTrialId = 0;
}

void ASharpExperimentManager::StopM2(const FString& Reason)
{
	DisarmVolumes(); CancelInputResume();
	GetWorldTimerManager().ClearTimer(AutoStartTimer);
	GetWorldTimerManager().ClearTimer(RepeatTimer);
	bFinishingRecording = false;
	if (IsValid(PreparedEnemy)) { PreparedEnemy->UpdatePolicy->Stop(); }
	if (IsValid(PreparedEnemy) && PreparedEnemy->MovementRecorder) { PreparedEnemy->MovementRecorder->StopRecording(Reason); }
	RestoreReplicationProfile();
	PreparedVolumes.Reset();
}

bool ASharpExperimentManager::CanTriggerImpulse(const ASharpImpulseVolume* Volume, int32 TrialId) const
{
	return HasAuthority() && Identity.TrialId == TrialId && Enemy == PreparedEnemy && IsValid(PreparedEnemy) &&
		PreparedEnemy->MovementRecorder->bRecording && TrialStatus.State == ESharpTrialState::Running &&
		PreparedVolumes.ContainsByPredicate([Volume](const TWeakObjectPtr<ASharpImpulseVolume>& Item) { return Item.Get() == Volume; });
}

void ASharpExperimentManager::FailImpulseTrial(const FString& Reason)
{
	if (!HasAuthority()) { return; }
	if (GetEnemyController()) { GetEnemyController()->SuspendFollow(); }
	SetTrialState(ESharpTrialState::Failed, Reason);
}

void ASharpExperimentManager::SuspendForImpulse(float Duration)
{
	if (!HasAuthority() || TrialStatus.State != ESharpTrialState::Running || !GetEnemyController()) { return; }
	GetEnemyController()->SuspendFollow();
	InputResumeTrialId = Identity.TrialId;
	GetWorldTimerManager().SetTimer(InputResumeTimer, this, &ASharpExperimentManager::ResumeAfterImpulse, Duration, false);
}

void ASharpExperimentManager::ResumeAfterImpulse()
{
	if (InputResumeTrialId != Identity.TrialId || Enemy != PreparedEnemy || TrialStatus.State != ESharpTrialState::Suspended) { return; }
	ResumeTrial();
}

void ASharpExperimentManager::AutoPrepare()
{
	if (!HasAuthority() || !HasActorBegunPlay() || TrialStatus.State != ESharpTrialState::Idle) { return; }
	PrepareTrial();
	if (TrialStatus.State == ESharpTrialState::Ready) { ScheduleStart(); }
}

void ASharpExperimentManager::ScheduleStart()
{
	if (AutoStartDelaySeconds > 0.0f) { GetWorldTimerManager().SetTimer(AutoStartTimer, this, &ASharpExperimentManager::StartTrial, AutoStartDelaySeconds, false); }
	else { AutoStartTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ASharpExperimentManager::StartTrial); }
}

void ASharpExperimentManager::RepeatTrial()
{
	if (!HasAuthority() || !bAutoRepeat || TrialStatus.State != ESharpTrialState::Completed || bFinishingRecording) { return; }
	if (CompletedRepetitions >= RepeatCount)
	{
		if (!ActiveModeSequence.IsValidIndex(ModeSequenceIndex + 1)) { return; }
		Identity.Mode = ActiveModeSequence[++ModeSequenceIndex]; CompletedRepetitions = 0;
	}
	ResetTrial();
	if (TrialStatus.State == ESharpTrialState::Ready) { ScheduleStart(); }
}

void ASharpExperimentManager::FinishRecording()
{
	bFinishingRecording = false;
	if (IsValid(PreparedEnemy)) { PreparedEnemy->UpdatePolicy->Stop(); }
	if (IsValid(PreparedEnemy)) { PreparedEnemy->MovementRecorder->StopRecording(FinishReason); }
	RestoreReplicationProfile();
	if (IsValid(PreparedEnemy) && !PreparedEnemy->MovementRecorder->LastError.IsEmpty())
	{
		TrialStatus.State = ESharpTrialState::Failed;
		TrialStatus.FailureReason = PreparedEnemy->MovementRecorder->LastError;
		PreparedEnemy->ClientState.State = ESharpTrialState::Failed;
		PreparedEnemy->ForceNetUpdate();
		OnRep_TrialStatus();
	}
	if (TrialStatus.State == ESharpTrialState::Completed)
	{
		++CompletedRepetitions;
		if (bAutoRepeat && (CompletedRepetitions < RepeatCount || ActiveModeSequence.IsValidIndex(ModeSequenceIndex + 1)) && IsValid(PreparedEnemy) && PreparedEnemy->MovementRecorder->LastError.IsEmpty())
		{
			if (RepeatDelaySeconds > 0.0f) { GetWorldTimerManager().SetTimer(RepeatTimer, this, &ASharpExperimentManager::RepeatTrial, RepeatDelaySeconds, false); }
			else { RepeatTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ASharpExperimentManager::RepeatTrial); }
		}
	}
}

void ASharpExperimentManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }
	const bool bActive = TrialStatus.State == ESharpTrialState::Running || TrialStatus.State == ESharpTrialState::Suspended;
	if (bActive && (!IsValid(PreparedEnemy) || PreparedEnemy != Enemy || !GetEnemyController()))
	{
		FailImpulseTrial(TEXT("Prepared Enemy or server AIController was lost or changed"));
	}
	if (bActive)
	{
		for (const TWeakObjectPtr<ASharpImpulseVolume>& Volume : PreparedVolumes)
		{
			if (!Volume.IsValid() || !Volume->IsPreparedGeometryUnchanged())
			{
				FailImpulseTrial(TEXT("Prepared Volume was removed or moved; reset the trial")); break;
			}
		}
	}
	if (IsValid(PreparedEnemy) && !PreparedEnemy->MovementRecorder->LastError.IsEmpty() &&
		(TrialStatus.State == ESharpTrialState::Running || TrialStatus.State == ESharpTrialState::Suspended))
	{
		FailImpulseTrial(PreparedEnemy->MovementRecorder->LastError);
	}
	// End on a later frame, after all movement delegate subscribers have seen the final update.
	if (bFinishingRecording && GFrameCounter > FinishRequestFrame && GetWorld()->GetTimeSeconds() >= FinishDeadline) { FinishRecording(); }
}
