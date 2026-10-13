#pragma once
#include "SharpMovementSample.h"
#include "SharpDetectorConfig.h"

/** No actors, overlap information, networking or experiment ground truth. */
class AIREPLICATIONCORE_API FSharpMovementClassifier
{
public:
	void Reset();
	FSharpClassification Observe(const FSharpMovementSample& Sample, const FSharpDetectorConfig& Config);
private:
	FSharpMovementSample Previous;
	bool bHasPrevious = false;
	bool bRisk = false;
	double RiskStart = 0.0;
	double LastEvent = -DBL_MAX;
	double LastAboveRelease = 0.0;
};
