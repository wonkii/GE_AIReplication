#include "SharpImpulseTypes.h"
bool FSharpImpulseSettings::IsValid(FString& OutReason) const
{
	if (ImpulseVector.ContainsNaN() || (!bUseExperimentImpulse && ImpulseVector.IsNearlyZero()) ||
		!FMath::IsFinite(RearmDelay) || RearmDelay < 0 || !FMath::IsFinite(InputSuspendSeconds) || InputSuspendSeconds <= 0 ||
		!StaticEnum<ESharpImpulseDirectionSpace>()->IsValidEnumValue(static_cast<int64>(DirectionSpace)) ||
		!StaticEnum<ESharpImpulseTriggerPolicy>()->IsValidEnumValue(static_cast<int64>(TriggerPolicy)) ||
		!StaticEnum<ESharpImpulseFollowPolicy>()->IsValidEnumValue(static_cast<int64>(FollowPolicy)))
	{
		OutReason = TEXT("Invalid impulse vector, policy or duration"); return false;
	}
	OutReason.Empty(); return true;
}
