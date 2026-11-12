#include "SharpExperimentManager.h"

#include "SharpExperimentSettings.h"
#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Movement/SharpRandomWalkerComponent.h"
#include "../Measurement/SharpConnectionRecorderComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpExperiment, Log, All);

ASharpExperimentManager::ASharpExperimentManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	ConnectionRecorder = CreateDefaultSubobject<USharpConnectionRecorderComponent>(TEXT("ConnectionRecorder"));
}

void ASharpExperimentManager::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		// OnRep_OptimizationProfile will apply the authoritative choice later.
		ApplyOptimizationProfileToEnemies();
		return;
	}
    FParse::Bool(FCommandLine::Get(), TEXT("SharpIntegratedCourse="), bEnableIntegratedMovementCourse);
    if (bEnableIntegratedMovementCourse && (bAutoSpawnRandomWalkEnemies || !RandomWalkEnemies.IsEmpty()))
    {
        // Obstacle avoidance is explicitly enabled for the integrated course.
        // This is normally off in legacy levels, and therefore must not depend
        // on an editor-side property change made between Batch trials.
        bEnableRandomWalkArena = true;
        bRandomWalkAvoidWorldStaticWalls = true;
        AutoSpawnBehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed;
        for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
        {
            if (IsValid(Enemy) && Enemy->RandomWalker)
            {
                Enemy->RandomWalker->Settings.BehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed;
                Enemy->RandomWalker->Settings.MaximumTurnAngle = 180.0f;
            }
        }
    }
	ActiveOptimizationProfile = Settings ? Settings->OptimizationProfile : ESharpExperimentOptimizationPreset::Baseline;
	ApplyOptimizationProfileToEnemies();

	// A dedicated preset is applied at runtime, so existing map / Blueprint values
	// for the older Repeat controls cannot silently reduce the batch to one trial.
	bAllModesBatchActive = bAutoRunAllModes;
	FParse::Bool(FCommandLine::Get(), TEXT("SharpAllModesBatch="), bAllModesBatchActive);
	if (bAllModesBatchActive)
	{
		bPrepareOnBeginPlay = true;
		bAutoStartTrial = true;
		bAutoRepeat = true;
		bManageAdaptiveForModeSequence = true;
		RepeatCount = AllModesRepetitionsPerMode;
		TrialDurationSeconds = AllModesTrialDurationSeconds;
		RepeatDelaySeconds = 1.0f;
		ModeSequence = {
			ESharpExperimentMode::Default, ESharpExperimentMode::Adaptive,
			ESharpExperimentMode::AlwaysHigh, ESharpExperimentMode::SingleShot,
			ESharpExperimentMode::Burst, ESharpExperimentMode::EventOnly,
			ESharpExperimentMode::EventPredictor
		};
		UE_LOG(LogSharpExperiment, Display,
			TEXT("[Sharp][Batch] Enabled: %d modes x %d trials, %.1fs per trial; total %d trials"),
			ModeSequence.Num(), RepeatCount, TrialDurationSeconds, ModeSequence.Num() * RepeatCount);
	}

	Identity.RunId = ConfiguredRunId;
	if (!bAllModesBatchActive) { FParse::Bool(FCommandLine::Get(), TEXT("SharpAutoRepeat="), bAutoRepeat); }
	FParse::Value(FCommandLine::Get(), TEXT("SharpRunId="), Identity.RunId);
	if (Identity.RunId.IsEmpty())
	{
		Identity.RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}

	Identity.TrialId = ConfiguredTrialId;
	FParse::Value(FCommandLine::Get(), TEXT("SharpTrialId="), Identity.TrialId);
	Identity.TrialId = FMath::Max(1, Identity.TrialId);

	Identity.Mode = ConfiguredMode;
	if (bAutoRepeat && !ModeSequence.IsEmpty()) { Identity.Mode = ModeSequence[0]; }
	FString ModeOverride;
	if (!bAllModesBatchActive && FParse::Value(FCommandLine::Get(), TEXT("SharpMode="), ModeOverride))
	{
		const UEnum* ModeEnum = StaticEnum<ESharpExperimentMode>();
		const int64 Value = ModeEnum->GetValueByNameString(ModeOverride);
		if (Value != INDEX_NONE)
		{
			Identity.Mode = static_cast<ESharpExperimentMode>(Value);
		}
		else
		{
			UE_LOG(LogSharpExperiment, Error, TEXT("Unknown SharpMode '%s'; using configured mode"), *ModeOverride);
		}
	}

    FString AssignReason;
    // Spawn before registry validation, experiment preparation and auto identity binding.
    // Only the authoritative world performs this; clients receive replicated actors.
    if (!SpawnConfiguredRandomWalkEnemies(AssignReason) ||
        !AssignRandomWalkIdentities(AssignReason))
    {
        Reject(AssignReason);
        return;
    }
    // Course configuration and placement are optional: they must never
    // silently prevent the seven-mode experiment from starting.
    if (!SpawnIntegratedCourseObstacles(AssignReason))
    {
        UE_LOG(LogSharpExperiment, Error,
            TEXT("[Sharp][Course] Disabled because course setup failed: %s. Batch will continue."),
            *AssignReason);
        bEnableIntegratedMovementCourse = false;
    }
	ApplyOptimizationProfileToEnemies(); // Includes auto-spawned enemies.
	LogStartup();
	ForceNetUpdate();
	if (!bAllModesBatchActive) { FParse::Bool(FCommandLine::Get(), TEXT("SharpAutoStart="), bAutoStartTrial); }
	FParse::Value(FCommandLine::Get(), TEXT("SharpStartDelay="), AutoStartDelaySeconds);
	FParse::Value(FCommandLine::Get(), TEXT("SharpExpectedClients="), ExpectedClientCount);
	if (!bAllModesBatchActive) { FParse::Value(FCommandLine::Get(), TEXT("SharpDurationSec="), TrialDurationSeconds); }
	if (bAutoRepeat && !ModeSequence.IsEmpty())
	{
		SequenceIndex = ModeSequence.IndexOfByKey(Identity.Mode);
		if (SequenceIndex == INDEX_NONE) { Reject(TEXT("Initial mode is not in ModeSequence")); return; }
	}
	if (!FMath::IsFinite(AutoStartDelaySeconds) || AutoStartDelaySeconds < 0 ||
		!FMath::IsFinite(PreparationTimeoutSeconds) || PreparationTimeoutSeconds <= 0 || ExpectedClientCount < 0 ||
		!FMath::IsFinite(TrialDurationSeconds) || TrialDurationSeconds < 0 || !FMath::IsFinite(RepeatDelaySeconds) || RepeatDelaySeconds < 0 || RepeatCount < 1)
	{
		Reject(TEXT("Invalid startup delay, preparation timeout or expected client count")); return;
	}
	if (bPrepareOnBeginPlay || bAutoStartTrial)
	{
		PreparationDeadline = GetWorld()->GetTimeSeconds() + PreparationTimeoutSeconds;
		GetWorldTimerManager().SetTimer(PreparationTimer, this, &ASharpExperimentManager::TryAutomaticPrepare, 0.1f, true);
	}
}

void ASharpExperimentManager::OnRep_OptimizationProfile()
{
	ApplyOptimizationProfileToEnemies();
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
	DOREPLIFETIME(ASharpExperimentManager, ActiveOptimizationProfile);
	DOREPLIFETIME(ASharpExperimentManager, Summary);
	DOREPLIFETIME(ASharpExperimentManager, LastCommandError);
	DOREPLIFETIME(ASharpExperimentManager, RecordingConfiguration);
}

void ASharpExperimentManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopIntegratedKnockback();
	CancelAutomaticControl();
	RestoreAutomaticNetworkMode();
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && Enemy->Experiment->GetManager() == this) { Enemy->Experiment->Release(); }
	}
	PreparedEnemies.Reset();
	RestoreAutomaticNetworkMode();
	Super::EndPlay(EndPlayReason);
}
