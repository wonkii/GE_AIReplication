#include "SharpExperimentSettings.h"

bool USharpExperimentSettings::IsValidConfiguration(FString& OutReason) const
{
	const float Values[] = {VelocityChangeThreshold, DirectionChangeThresholdDegrees, NormalNetUpdateFrequency,
		HighNetUpdateFrequency, MaximumPredictionSeconds, CorrectionSeconds, BurstSeconds, ScalabilityNetCullDistanceCm};
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value)) { OutReason = TEXT("Experiment settings must be finite"); return false; }
	}
	if (VelocityChangeThreshold < 0.0f || DirectionChangeThresholdDegrees < 0.0f || DirectionChangeThresholdDegrees > 180.0f ||
		NormalNetUpdateFrequency <= 0.0f || HighNetUpdateFrequency < NormalNetUpdateFrequency ||
		MaximumPredictionSeconds < 0.0f || CorrectionSeconds < 0.0f || Impulse.ContainsNaN())
	{
		OutReason = TEXT("Invalid threshold, frequency, duration, or impulse");
		return false;
	}
	if (!Detection.MakeConfig(VelocityChangeThreshold, DirectionChangeThresholdDegrees).IsValid() ||
		!Recording.IsValid() || !GetPredictionSettings().IsValid() || BurstSeconds <= 0 || ScenarioRevision < 1 || ScalabilityNetCullDistanceCm < 100.0f)
	{
		OutReason = TEXT("Invalid Detector, recording, prediction or Burst configuration"); return false;
	}
	OutReason.Empty();
	return true;
}

FString USharpExperimentSettings::ToLogString() const
{
	return FString::Printf(TEXT("OptimizationProfile=%s ScalabilityDistanceRelevancy=%d ScalabilityCullDistanceCm=%.1f VelocityChangeThreshold=%.3f DirectionChangeThresholdDegrees=%.3f NormalNetUpdateFrequency=%.3f HighNetUpdateFrequency=%.3f MaximumPredictionSeconds=%.3f CorrectionSeconds=%.3f Impulse=%s"),
		*StaticEnum<ESharpExperimentOptimizationPreset>()->GetNameStringByValue(static_cast<int64>(OptimizationProfile)),
		bScalabilityEnableDistanceRelevancy, ScalabilityNetCullDistanceCm,
		VelocityChangeThreshold, DirectionChangeThresholdDegrees, NormalNetUpdateFrequency, HighNetUpdateFrequency,
		MaximumPredictionSeconds, CorrectionSeconds, *Impulse.ToString());
}
