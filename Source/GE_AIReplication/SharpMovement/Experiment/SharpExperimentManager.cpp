#include "SharpExperimentManager.h"

#include "SharpExperimentSettings.h"
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
}
