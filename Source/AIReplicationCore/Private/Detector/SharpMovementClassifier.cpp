#include "Detector/SharpMovementClassifier.h"

bool FSharpDetectorConfig::IsValid() const
{
	const double Values[] = {VelocityThreshold, DirectionThresholdDegrees, MinimumDirectionSpeed, AccelerationThreshold,
		ReleaseRatio, QuietReleaseSeconds, MinimumRiskSeconds, RetriggerSeconds, MaximumSampleGap};
	for (double Value : Values) { if (!FMath::IsFinite(Value) || Value < 0.0) { return false; } }
	return VelocityThreshold > 0.0 && DirectionThresholdDegrees > 0.0 && DirectionThresholdDegrees <= 180.0 &&
		ReleaseRatio > 0.0 && ReleaseRatio < 1.0 && MaximumSampleGap > 0.0;
}

void FSharpMovementClassifier::Reset()
{
	bHasPrevious = bRisk = false;
	LastEvent = -DBL_MAX;
}

FSharpClassification FSharpMovementClassifier::Observe(const FSharpMovementSample& Sample, const FSharpDetectorConfig& Config)
{
	FSharpClassification Result;
	if (!Config.IsValid() || !FMath::IsFinite(Sample.ServerTime) || Sample.Position.ContainsNaN() || Sample.Velocity.ContainsNaN())
	{
		Result.bRiskEnded = bRisk; Reset(); return Result;
	}
	const double Delta = Sample.ServerTime - Previous.ServerTime;
	if (!bHasPrevious || Delta <= 0.0 || Delta > Config.MaximumSampleGap)
	{
		// Same-world-time subupdates have no measurable dt: keep the latest sample, without inventing acceleration.
		if (bHasPrevious && Delta == 0.0) { Previous = Sample; Result.bRisk = bRisk; return Result; }
		Result.bRiskEnded = bRisk; Reset(); Previous = Sample; bHasPrevious = true; return Result;
	}
	Result.DeltaVelocity = (Sample.Velocity - Previous.Velocity).Size();
	Result.Acceleration = Result.DeltaVelocity / Delta;
	const bool bDirectionValid = Previous.Velocity.Size() > FMath::Max(Config.MinimumDirectionSpeed, static_cast<double>(KINDA_SMALL_NUMBER)) &&
		Sample.Velocity.Size() > FMath::Max(Config.MinimumDirectionSpeed, static_cast<double>(KINDA_SMALL_NUMBER));
	if (bDirectionValid)
	{
		Result.DirectionDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
			Previous.Velocity.GetSafeNormal(), Sample.Velocity.GetSafeNormal()), -1.0, 1.0)));
	}
	if (Result.DeltaVelocity >= Config.VelocityThreshold) { Result.Reasons |= 1; }
	if (bDirectionValid && Result.DirectionDegrees >= Config.DirectionThresholdDegrees) { Result.Reasons |= 2; }
	if (Config.AccelerationThreshold > 0.0 && Result.Acceleration >= Config.AccelerationThreshold) { Result.Reasons |= 4; }
	const bool bModeChanged = Sample.MovementMode != Previous.MovementMode || Sample.CustomMode != Previous.CustomMode;
	if (Config.bDetectModeChanges && bModeChanged) { Result.Reasons |= 8; }
	const bool bAboveRelease = Result.DeltaVelocity >= Config.VelocityThreshold * Config.ReleaseRatio ||
		(bDirectionValid && Result.DirectionDegrees >= Config.DirectionThresholdDegrees * Config.ReleaseRatio) ||
		(Config.AccelerationThreshold > 0.0 && Result.Acceleration >= Config.AccelerationThreshold * Config.ReleaseRatio) ||
		(Config.bDetectModeChanges && bModeChanged);
	if (bAboveRelease) { LastAboveRelease = Sample.ServerTime; }
	if (Result.Reasons && Sample.ServerTime - LastEvent >= Config.RetriggerSeconds)
	{
		Result.bSharpEvent = true; LastEvent = Sample.ServerTime;
		if (!bRisk) { bRisk = true; RiskStart = Sample.ServerTime; Result.bRiskStarted = true; }
	}
	if (bRisk && !bAboveRelease && Sample.ServerTime - RiskStart >= Config.MinimumRiskSeconds &&
		Sample.ServerTime - LastAboveRelease >= Config.QuietReleaseSeconds)
	{
		bRisk = false; Result.bRiskEnded = true;
	}
	Result.bRisk = bRisk; Previous = Sample;
	return Result;
}
