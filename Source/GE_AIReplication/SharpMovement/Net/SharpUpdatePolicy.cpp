#include "SharpUpdatePolicy.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

namespace { int32 AdaptiveLeases = 0, OriginalAdaptive = 0, LeasedAdaptive = 0; }

USharpUpdatePolicyComponent::USharpUpdatePolicyComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USharpUpdatePolicyComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy) { Enemy->MovementDetector->OnClassified.AddUObject(this, &USharpUpdatePolicyComponent::OnClassified); }
}

void USharpUpdatePolicyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop(); if (Enemy) { Enemy->MovementDetector->OnClassified.RemoveAll(this); }
	Super::EndPlay(Reason);
}

bool USharpUpdatePolicyComponent::Start(ESharpExperimentMode InMode, const USharpExperimentSettings* Settings, FString& OutReason)
{
	Stop();
	if (!Enemy || !Enemy->HasAuthority() || !Settings || !Settings->IsValidConfiguration(OutReason)) { return false; }
	if ((InMode == ESharpExperimentMode::AlwaysHigh || InMode == ESharpExperimentMode::Burst) && Settings->HighNetUpdateFrequency < Enemy->GetNetUpdateFrequency())
	{
		OutReason = TEXT("High Net Update Frequency is below the prepared baseline; increase it or use ControlledLowRate"); return false;
	}
	IConsoleVariable* Adaptive = IConsoleManager::Get().FindConsoleVariable(TEXT("net.UseAdaptiveNetUpdateFrequency"));
	if (!Adaptive) { OutReason = TEXT("Adaptive NetUpdateFrequency console variable is unavailable"); return false; }
	if (AdaptiveLeases == 0) { OriginalAdaptive = Adaptive->GetInt(); }
	const int32 Desired = InMode == ESharpExperimentMode::Default ? OriginalAdaptive : (InMode == ESharpExperimentMode::Adaptive ? 1 : 0);
	if (AdaptiveLeases > 0 && Desired != LeasedAdaptive)
	{
		OutReason = TEXT("Adaptive frequency is process-wide: concurrent trials require compatible modes; compare modes sequentially"); return false;
	}
	if (AdaptiveLeases++ == 0) { LeasedAdaptive = Desired; Adaptive->SetWithCurrentPriority(Desired); }
	if (Adaptive->GetInt() != Desired)
	{
		if (--AdaptiveLeases == 0) { Adaptive->SetWithCurrentPriority(OriginalAdaptive); }
		OutReason = TEXT("Could not apply the selected adaptive frequency policy"); return false;
	}
	bOwnsAdaptiveLease = true; bActive = true; Mode = InMode;
	OriginalFrequency = Enemy->GetNetUpdateFrequency(); OriginalMinFrequency = Enemy->GetMinNetUpdateFrequency();
	HighFrequency = Settings->HighNetUpdateFrequency; BurstDuration = Settings->BurstSeconds;
	bOriginalReplicateTimestamp = Enemy->GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp;
	if (Settings->bReplicateMovementTimestamps) { Enemy->GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp = true; }
	bAirborneModel = Settings->VisualPrediction.bEnableAirborneModel;
	if (Mode == ESharpExperimentMode::AlwaysHigh)
	{
		Enemy->SetNetUpdateFrequency(HighFrequency); Enemy->SetMinNetUpdateFrequency(HighFrequency);
	}
	Enemy->MovementDetector->Start(Settings->MakeDetectorConfig());
	OutReason.Empty(); return true;
}

void USharpUpdatePolicyComponent::Stop()
{
	if (bActive && Enemy)
	{
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("PolicyStop"), 0, GetWorld()->GetTimeSeconds(), OriginalFrequency, OriginalMinFrequency, TEXT("RestorePreparedBaseline"));
		Enemy->MovementDetector->Stop(); Enemy->SetNetUpdateFrequency(OriginalFrequency); Enemy->SetMinNetUpdateFrequency(OriginalMinFrequency);
		Enemy->GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp = bOriginalReplicateTimestamp;
	}
	bActive = false; BurstEnd = -1.0;
	if (bOwnsAdaptiveLease)
	{
		bOwnsAdaptiveLease = false;
		if (--AdaptiveLeases == 0)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("net.UseAdaptiveNetUpdateFrequency"))) { Var->SetWithCurrentPriority(OriginalAdaptive); }
		}
	}
}

void USharpUpdatePolicyComponent::OnClassified(const FSharpMovementSample& Sample, const FSharpClassification& Result)
{
	if (!bActive || !Result.bSharpEvent) { return; }
	if (Mode == ESharpExperimentMode::EventOnly || Mode == ESharpExperimentMode::EventPredictor)
	{
		FSharpMoveSnapshot Snapshot;
		Snapshot.bValid = true; Snapshot.RunId = Enemy->ExperimentContext.Identity.RunId;
		Snapshot.TrialId = Enemy->ExperimentContext.Identity.TrialId; Snapshot.ActorId = Enemy->ActorId;
		Snapshot.ServerTime = Sample.ServerTime; Snapshot.StartPosition = Sample.Position; Snapshot.PostVelocity = Sample.Velocity;
		Snapshot.MovementMode = Sample.MovementMode; Snapshot.CustomMovementMode = Sample.CustomMode;
		Snapshot.Gravity = Enemy->GetCharacterMovement()->GetGravityDirection() * FMath::Abs(Enemy->GetCharacterMovement()->GetGravityZ());
		Snapshot.PredictionType = Sample.MovementMode == MOVE_Walking ? ESharpPredictionType::ConstantVelocity :
			(bAirborneModel && Sample.MovementMode == MOVE_Falling ? ESharpPredictionType::Airborne : ESharpPredictionType::None);
		Enemy->PublishSnapshot(Snapshot);
	}
	if (Mode == ESharpExperimentMode::Burst)
	{
		Enemy->SetNetUpdateFrequency(HighFrequency); Enemy->SetMinNetUpdateFrequency(HighFrequency);
		BurstEnd = Sample.ServerTime + BurstDuration;
	}
	if (Mode == ESharpExperimentMode::SingleShot || Mode == ESharpExperimentMode::Burst ||
		Mode == ESharpExperimentMode::EventOnly || Mode == ESharpExperimentMode::EventPredictor)
	{
		Enemy->ForceNetUpdate();
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("ForceNetUpdateRequest"), Enemy->SharpMoveSnapshot.SequenceId, Sample.ServerTime,
			Enemy->GetNetUpdateFrequency(), 0.0, UEnum::GetValueAsString(Mode));
	}
}

void USharpUpdatePolicyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (bActive && Mode == ESharpExperimentMode::Burst && BurstEnd >= 0.0 && GetWorld()->GetTimeSeconds() >= BurstEnd)
	{
		Enemy->SetNetUpdateFrequency(OriginalFrequency); Enemy->SetMinNetUpdateFrequency(OriginalMinFrequency); BurstEnd = -1.0;
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("BurstEnd"), 0, GetWorld()->GetTimeSeconds(), OriginalFrequency, 0.0, TEXT("FrequencyRestored"));
	}
}
