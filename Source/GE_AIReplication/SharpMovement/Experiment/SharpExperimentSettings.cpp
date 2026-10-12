#include "SharpExperimentSettings.h"

bool USharpExperimentSettings::IsValidConfiguration(FString& OutReason) const
{
	if (!Recording.IsValid(OutReason)) { return false; }
	if (!MakeDetectorConfig().IsValid() || !MakePredictionSettings().IsValid() || !FMath::IsFinite(BurstSeconds) || BurstSeconds <= 0.0f)
	{
		OutReason = TEXT("Invalid detector, prediction bounds or burst duration"); return false;
	}
	const float Values[] = {VelocityChangeThreshold, DirectionChangeThresholdDegrees, NormalNetUpdateFrequency,
		HighNetUpdateFrequency, MaximumPredictionSeconds, CorrectionSeconds};
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value))
		{
			OutReason = TEXT("Settings must contain finite numbers");
			return false;
		}
	}
	if (VelocityChangeThreshold < 0.0f || DirectionChangeThresholdDegrees < 0.0f || DirectionChangeThresholdDegrees > 180.0f ||
		NormalNetUpdateFrequency <= 0.0f || HighNetUpdateFrequency < NormalNetUpdateFrequency ||
		MaximumPredictionSeconds < 0.0f || CorrectionSeconds < 0.0f || Impulse.ContainsNaN())
	{
		OutReason = TEXT("Invalid threshold, frequency, duration, or impulse");
		return false;
	}
	OutReason.Empty();
	return true;
}

FString USharpExperimentSettings::ToLogString() const
{
	return FString::Printf(TEXT("VelocityChangeThreshold=%.3f DirectionChangeThresholdDegrees=%.3f NormalNetUpdateFrequency=%.3f HighNetUpdateFrequency=%.3f MaximumPredictionSeconds=%.3f CorrectionSeconds=%.3f Impulse=%s"),
		VelocityChangeThreshold, DirectionChangeThresholdDegrees, NormalNetUpdateFrequency, HighNetUpdateFrequency,
		MaximumPredictionSeconds, CorrectionSeconds, *Impulse.ToString());
}

FSharpDetectorConfig USharpExperimentSettings::MakeDetectorConfig() const
{
	FSharpDetectorConfig Config;
	Config.VelocityThreshold = VelocityChangeThreshold; Config.DirectionThresholdDegrees = DirectionChangeThresholdDegrees;
	Config.MinimumDirectionSpeed = MinimumDirectionSpeed; Config.AccelerationThreshold = AccelerationThreshold;
	Config.ReleaseRatio = DetectorReleaseRatio; Config.QuietReleaseSeconds = QuietReleaseSeconds; Config.MinimumRiskSeconds = MinimumRiskSeconds;
	Config.RetriggerSeconds = RetriggerSeconds; Config.MaximumSampleGap = MaximumDetectorSampleGap; Config.bDetectModeChanges = bDetectMovementModeChanges;
	return Config;
}

FSharpVisualPredictionSettings USharpExperimentSettings::MakePredictionSettings() const
{
	FSharpVisualPredictionSettings Result = VisualPrediction;
	Result.MaximumPredictionSeconds = MaximumPredictionSeconds; Result.CorrectionSeconds = CorrectionSeconds;
	return Result;
}
