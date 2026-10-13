#pragma once
#include "CoreMinimal.h"
#include "Detector/SharpDetectorConfig.h"
#include "SharpDetectionSettings.generated.h"

USTRUCT(BlueprintType)
struct FSharpDetectionSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinimumDirectionSpeed = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AccelerationThreshold = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReleaseRatio = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float QuietReleaseSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinimumRiskSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetriggerSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaximumSampleGap = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDetectModeChanges = false;
	FSharpDetectorConfig MakeConfig(float VelocityThreshold, float DirectionThreshold) const
	{
		FSharpDetectorConfig Result;
		Result.VelocityThreshold = VelocityThreshold; Result.DirectionThresholdDegrees = DirectionThreshold;
		Result.MinimumDirectionSpeed = MinimumDirectionSpeed; Result.AccelerationThreshold = AccelerationThreshold;
		Result.ReleaseRatio = ReleaseRatio; Result.QuietReleaseSeconds = QuietReleaseSeconds;
		Result.MinimumRiskSeconds = MinimumRiskSeconds; Result.RetriggerSeconds = RetriggerSeconds;
		Result.MaximumSampleGap = MaximumSampleGap; Result.bDetectModeChanges = bDetectModeChanges;
		return Result;
	}
};
