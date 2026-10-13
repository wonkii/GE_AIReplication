#pragma once
#include "CoreMinimal.h"

struct AIREPLICATIONCORE_API FSharpDetectorConfig
{
	double VelocityThreshold = 300.0;
	double DirectionThresholdDegrees = 45.0;
	double MinimumDirectionSpeed = 50.0;
	double AccelerationThreshold = 0.0; // Zero disables this additional criterion.
	double ReleaseRatio = 0.5;
	double QuietReleaseSeconds = 0.1;
	double MinimumRiskSeconds = 0.1;
	double RetriggerSeconds = 0.1;
	double MaximumSampleGap = 0.25;
	bool bDetectModeChanges = false;
	bool IsValid() const;
};
