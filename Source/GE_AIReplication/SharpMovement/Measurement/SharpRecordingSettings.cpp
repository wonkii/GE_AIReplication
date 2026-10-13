#include "SharpRecordingSettings.h"

bool FSharpRecordingSettings::IsValid() const
{
	const float Values[] = {FlushIntervalSeconds, PostTrialObservationSeconds, EventObservationSeconds,
		ObservableVelocityChange, ConnectionSampleSeconds};
	for (float Value : Values) { if (!FMath::IsFinite(Value) || Value < 0) { return false; } }
	return FlushIntervalSeconds >= 0.05f && MaximumBufferedRows >= 64 && MaximumBufferedRows <= 1000000 &&
		EventObservationSeconds >= 0.01f && MaximumPendingEvents >= 1 && MaximumPendingEvents <= 4096 && ConnectionSampleSeconds >= 0.05f;
}
