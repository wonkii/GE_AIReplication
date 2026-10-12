#include "SharpSplineMovementTypes.h"

bool FSharpSplineFollowSettings::IsValid(FString& OutReason) const
{
	const float Values[] = {TargetSpeed, LookAheadDistance, AcceptanceRadius, StopSpeedTolerance,
		EndSlowdownDistance, MinimumApproachSpeed, BlockedTimeoutSeconds, MinimumProgressDistance,
		ProjectionSlackDistance, ProjectionSpeedMultiplier};
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value))
		{
			OutReason = TEXT("Follower settings must be finite");
			return false;
		}
	}
	if (TargetSpeed <= 0.0f || LookAheadDistance <= 0.0f || AcceptanceRadius <= 0.0f ||
		StopSpeedTolerance < 0.0f || EndSlowdownDistance <= AcceptanceRadius || MinimumApproachSpeed <= 0.0f ||
		MinimumApproachSpeed > TargetSpeed || BlockedTimeoutSeconds < 0.0f || MinimumProgressDistance <= 0.0f ||
		ProjectionSlackDistance < 0.0f || ProjectionSpeedMultiplier < 1.0f)
	{
		OutReason = TEXT("Invalid follower settings: check positive values, approach speed <= target speed, slowdown distance > acceptance radius");
		return false;
	}
	OutReason.Empty();
	return true;
}
