#include "SharpExperimentSettings.h"

bool USharpExperimentSettings::IsValidConfiguration(FString& OutReason) const
{
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
