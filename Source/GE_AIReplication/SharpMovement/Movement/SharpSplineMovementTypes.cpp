#include "SharpSplineMovementTypes.h"

bool FSharpSplineFollowSettings::IsValid(FString& OutReason) const
{
	const float Values[] = {TargetSpeed, LookAheadDistance, AcceptanceRadius,
		StopSpeedTolerance, EndSlowdownDistance, MinimumApproachSpeed,
		BlockedTimeoutSeconds, MinimumProgressDistance, ProjectionSlackDistance, ProjectionSpeedMultiplier};
	for (float Value : Values)
	{
		if (!FMath::IsFinite(Value)) { OutReason = TEXT("Follower settings must be finite"); return false; }
	}
	if (TargetSpeed <= 0 || LookAheadDistance <= 0 || AcceptanceRadius <= 0 ||
		StopSpeedTolerance < 0 || EndSlowdownDistance <= AcceptanceRadius ||
		MinimumApproachSpeed <= 0 || MinimumApproachSpeed > TargetSpeed ||
		BlockedTimeoutSeconds < 0 || MinimumProgressDistance <= 0 || ProjectionSlackDistance < 0 ||
		ProjectionSpeedMultiplier < 1 || !StaticEnum<ESharpSplineTraversal>()->IsValidEnumValue(static_cast<int64>(Traversal)))
	{
		OutReason = TEXT("Invalid speed, arrival, projection or traversal settings"); return false;
	}
	OutReason.Empty(); return true;
}
