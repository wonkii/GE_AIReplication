#include "SharpEnemyCharacter.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Measurement/SharpMovementDiagnosticsComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "Combat/HitReactionComponent.h"
#include "Components/SkeletalMeshComponent.h"

ESharpExperimentOptimizationPreset ASharpEnemyCharacter::GetOptimizationProfile() const
{
	return static_cast<ESharpExperimentOptimizationPreset>(AppliedOptimizationProfileValue);
}

void ASharpEnemyCharacter::ApplyOptimizationProfile(const ASharpExperimentManager& Manager)
{
	USkeletalMeshComponent* SkeletalMeshComp = GetMesh();
	if (!SkeletalMeshComp || !Diagnostics || !Recorder || !VisualPredictor) { return; }
	if (!bOptimizationDefaultsSaved)
	{
		bOptimizationDefaultsSaved = true;
		bInitialDiagnosticLogging = Diagnostics->bEnableLogging;
		bInitialAnimationURO = SkeletalMeshComp->bEnableUpdateRateOptimizations;
		InitialAnimationVisibilityOption = static_cast<uint8>(SkeletalMeshComp->VisibilityBasedAnimTickOption);
		bInitialAlwaysRelevant = bAlwaysRelevant;
		InitialNetCullDistanceSquared = GetNetCullDistanceSquared();
	}
	const ESharpExperimentOptimizationPreset Profile = Manager.ActiveOptimizationProfile;
	AppliedOptimizationProfileValue = static_cast<uint8>(Profile);
	const bool bOptimized = Profile != ESharpExperimentOptimizationPreset::Baseline;
	Diagnostics->SetDiagnosticLoggingEnabled(bOptimized ? false : bInitialDiagnosticLogging);
	Recorder->SetIdleTickOptimizationEnabled(bOptimized);
	VisualPredictor->SetIdleTickOptimizationEnabled(bOptimized);

	// Preserve authoritative animation, sockets, montages and server movement.
	// URO is client visual-only and is disabled when combat reactions need bones.
	const bool bSafeVisualOnly = !HasAuthority() && !(HitReaction && HitReaction->bCombatEnabled);
	SkeletalMeshComp->bEnableUpdateRateOptimizations = bOptimized && bSafeVisualOnly ? true : bInitialAnimationURO;
	SkeletalMeshComp->VisibilityBasedAnimTickOption = bSafeVisualOnly && Profile == ESharpExperimentOptimizationPreset::Scalability
		? EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered
		: static_cast<EVisibilityBasedAnimTickOption>(InitialAnimationVisibilityOption);

	// Never change replication behavior during an official policy comparison.
	// Scalability distance culling requires an explicit secondary opt-in.
	if (HasAuthority())
	{
		const USharpExperimentSettings* Settings = Manager.Settings;
		if (Profile == ESharpExperimentOptimizationPreset::Scalability && Settings &&
			Settings->bScalabilityEnableDistanceRelevancy)
		{
			bAlwaysRelevant = false;
			SetNetCullDistanceSquared(FMath::Square(Settings->ScalabilityNetCullDistanceCm));
		}
		else
		{
			bAlwaysRelevant = bInitialAlwaysRelevant;
			SetNetCullDistanceSquared(InitialNetCullDistanceSquared);
		}
	}
}
