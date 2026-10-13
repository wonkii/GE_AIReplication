#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "../Detector/SharpDetectionSettings.h"
#include "../Measurement/SharpRecordingSettings.h"
#include "../Prediction/SharpVisualPredictionSettings.h"
#include "SharpExperimentSettings.generated.h"

UCLASS(BlueprintType)
class GE_AIREPLICATION_API USharpExperimentSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// Existing asset fields remain the canonical thresholds and prediction durations.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0"))
	float VelocityChangeThreshold = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float DirectionChangeThresholdDegrees = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.1"))
	float NormalNetUpdateFrequency = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.1"))
	float HighNetUpdateFrequency = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prediction", meta = (ClampMin = "0.0"))
	float MaximumPredictionSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Prediction", meta = (ClampMin = "0.0"))
	float CorrectionSeconds = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FVector Impulse = FVector(0.0, 700.0, 250.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Detection") FSharpDetectionSettings Detection;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Measurement") FSharpRecordingSettings Recording;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Prediction") FSharpVisualPredictionSettings Prediction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network", meta=(ClampMin="0.01")) float BurstSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network") bool bUseControlledLowRate = false;
	// Increment when level collision/environment changes beyond the recorded path geometry.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Scenario", meta=(ClampMin="1")) int32 ScenarioRevision = 1;
	FSharpVisualPredictionSettings GetPredictionSettings() const
	{
		FSharpVisualPredictionSettings Result = Prediction;
		Result.MaximumPredictionSeconds = MaximumPredictionSeconds; Result.CorrectionSeconds = CorrectionSeconds;
		return Result;
	}

	bool IsValidConfiguration(FString& OutReason) const;
	FString ToLogString() const;
};

