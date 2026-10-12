#include "SharpMovementRecordTypes.h"
#include "Misc/Paths.h"

bool FSharpRecordingSettings::IsValid(FString& OutReason) const
{
	FString Normalized = OutputSubdirectory;
	Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
	TArray<FString> Parts;
	Normalized.ParseIntoArray(Parts, TEXT("/"), false);
	if (Normalized.IsEmpty() || Normalized.StartsWith(TEXT("/")) || !FPaths::IsRelative(Normalized) || Normalized.Contains(TEXT(":")) ||
		Parts.Contains(TEXT("..")) || Parts.Contains(TEXT(".")))
	{
		OutReason = TEXT("OutputSubdirectory must be a nonempty relative folder without . or .. segments");
		return false;
	}
	if (!FMath::IsFinite(FlushIntervalSeconds) || FlushIntervalSeconds <= 0.0f || !FMath::IsFinite(ConnectionSampleSeconds) || ConnectionSampleSeconds <= 0.0f || MaximumBufferedRows < 1 ||
		!FMath::IsFinite(EventObservationSeconds) || EventObservationSeconds <= 0.0f ||
		!FMath::IsFinite(ObservableVelocityChange) || ObservableVelocityChange < 0.0f || MaximumPendingEvents < 1 || MaximumPendingClientDiagnostics < 1 ||
		!FMath::IsFinite(PostTrialObservationSeconds) || PostTrialObservationSeconds < 0.0f)
	{
		OutReason = TEXT("Invalid recorder interval, buffer size, observation window, or velocity tolerance");
		return false;
	}
	OutReason.Empty();
	return true;
}

bool FSharpObservationSettings::IsValid(FString& OutReason) const
{
	const float Values[] = {ShapeLifetimeSeconds, EventLifetimeSeconds, ArrowTimeScale, ArrowHeadSize, EventRadius, LineThickness};
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0f)
		{
			OutReason = TEXT("Drawing sizes and lifetimes must be finite and nonnegative");
			return false;
		}
	}
	if (ArrowHeadSize <= 0.0f || EventRadius <= 0.0f || EventSphereSegments < 4 || EventSphereSegments > 64)
	{
		OutReason = TEXT("ArrowHeadSize and EventRadius must be positive; sphere segments must be between 4 and 64");
		return false;
	}
	OutReason.Empty();
	return true;
}
