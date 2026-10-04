#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SharpExperimentSettings.generated.h"

UCLASS(BlueprintType)
class GE_AIREPLICATION_API USharpExperimentSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// Detection thresholds are reserved for the M4 detector.
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

	bool IsValidConfiguration(FString& OutReason) const;
	FString ToLogString() const;
};

