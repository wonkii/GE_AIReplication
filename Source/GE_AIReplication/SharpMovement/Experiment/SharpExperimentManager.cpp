#include "SharpExperimentManager.h"

#include "SharpExperimentSettings.h"
#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
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
		return;
	}

	Identity.RunId = ConfiguredRunId;
	FParse::Bool(FCommandLine::Get(), TEXT("SharpAutoRepeat="), bAutoRepeat);
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
	if (FParse::Value(FCommandLine::Get(), TEXT("SharpMode="), ModeOverride))
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

	LogStartup();
	ForceNetUpdate();
	FParse::Bool(FCommandLine::Get(), TEXT("SharpAutoStart="), bAutoStartTrial);
	FParse::Value(FCommandLine::Get(), TEXT("SharpStartDelay="), AutoStartDelaySeconds);
	FParse::Value(FCommandLine::Get(), TEXT("SharpExpectedClients="), ExpectedClientCount);
	FParse::Value(FCommandLine::Get(), TEXT("SharpDurationSec="), TrialDurationSeconds);
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
	DOREPLIFETIME(ASharpExperimentManager, Summary);
	DOREPLIFETIME(ASharpExperimentManager, LastCommandError);
	DOREPLIFETIME(ASharpExperimentManager, RecordingConfiguration);
}

void ASharpExperimentManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
