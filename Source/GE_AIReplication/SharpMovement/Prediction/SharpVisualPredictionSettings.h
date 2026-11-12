#pragma once
#include "CoreMinimal.h"
#include "SharpVisualPredictionSettings.generated.h"

USTRUCT(BlueprintType)
struct FSharpVisualPredictionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model") bool bEnablePrediction = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model") bool bEnableAirborneModel = true;
    // Legacy value retained for existing DA assets. EventOnly now updates its capsule.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Control", meta=(DeprecatedProperty, DeprecationMessage="EventOnly now owns capsule movement")) bool bEventOnlyUsesCustomCorrection = false;
    // Time since last detector event/anchor before sending a recovery state.
    // Zero deliberately disables recovery for a strict, potentially freezing event-only experiment.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event Transport", meta=(ClampMin="0.0", Units="s")) float EventRecoveryIntervalSeconds = 0.5f;
    // Maximum client extrapolation after an anchor. Separate from legacy mesh predictor horizon.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event Transport", meta=(ClampMin="0.0", Units="s")) float EventExtrapolationHorizonSeconds = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds", meta = (ClampMin = "0.0", Units = "cm")) float MaximumVisualOffset = 150.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds", meta = (ClampMin = "0.0", Units = "s")) float FutureTimeTolerance = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (ClampMin = "0.0", Units = "s")) float PredictionBlendSeconds = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision") bool bCollisionGuard = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision") TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Visibility;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "0.1", ClampMax = "1.0")) float CollisionCapsuleScale = 0.9f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime") float MaximumPredictionSeconds = 0.15f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runtime") float CorrectionSeconds = 0.1f;
	bool IsValid() const
	{
		const float Values[] = {MaximumVisualOffset, FutureTimeTolerance, PredictionBlendSeconds, MaximumPredictionSeconds, CorrectionSeconds, CollisionCapsuleScale, EventRecoveryIntervalSeconds, EventExtrapolationHorizonSeconds};
		for (float Value : Values) { if (!FMath::IsFinite(Value) || Value < 0.0f) { return false; } }
		return CollisionCapsuleScale >= 0.1f && CollisionCapsuleScale <= 1.0f;
	}
};
