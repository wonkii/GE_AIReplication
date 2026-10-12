#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "../Measurement/SharpMovementRecordTypes.h"
#include "../Prediction/SharpVisualPredictionSettings.h"
#include "Detector/SharpDetectorConfig.h"
#include "SharpExperimentSettings.generated.h"

UCLASS(BlueprintType)
class GE_AIREPLICATION_API USharpExperimentSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "M2 Recording")
	FSharpRecordingSettings Recording;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "M6 Prediction") FSharpVisualPredictionSettings VisualPrediction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0")) float MinimumDirectionSpeed = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0")) float AccelerationThreshold = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.001", ClampMax = "0.99")) float DetectorReleaseRatio = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0", Units = "s")) float QuietReleaseSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0", Units = "s")) float MinimumRiskSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.0", Units = "s")) float RetriggerSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.001", Units = "s")) float MaximumDetectorSampleGap = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection") bool bDetectMovementModeChanges = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Network", meta = (ClampMin = "0.001", Units = "s")) float BurstSeconds = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Network") bool bReplicateMovementTimestamps = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.001"))
	float VelocityChangeThreshold = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Detection", meta = (ClampMin = "0.001", ClampMax = "180.0"))
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

	bool IsValidConfiguration(FString& OutReason) const;
	FString ToLogString() const;
	FSharpDetectorConfig MakeDetectorConfig() const;
	FSharpVisualPredictionSettings MakePredictionSettings() const;
};

