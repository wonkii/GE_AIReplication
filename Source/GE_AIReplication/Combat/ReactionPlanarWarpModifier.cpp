#include "ReactionPlanarWarpModifier.h"
#include "MotionWarpingComponent.h"

void UReactionPlanarWarpModifier::Update(const FMotionWarpingUpdateContext& Context)
{
    // The character adapter can lose its montage instance before converting the
    // final extracted delta. Keep scaling that delta until reaction cleanup.
    if (!Context.Animation.IsValid() && GetState() == ERootMotionModifierState::Active)
        return;
    Super::Update(Context);
}
