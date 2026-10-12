#include "SharpImpulseTypes.h"

bool FSharpImpulseSettings::IsValid(FString& OutReason) const
{
	if (ImpulseVector.ContainsNaN() || ImpulseVector.IsNearlyZero() || !FMath::IsFinite(RearmDelay) || RearmDelay < 0.0f ||
		!FMath::IsFinite(InputSuspendSeconds) || InputSuspendSeconds <= 0.0f)
	{
		OutReason = TEXT("Impulse must be finite and nonzero; rearm delay must be nonnegative and suspension duration positive");
		return false;
	}
	OutReason.Empty();
	return true;
}
