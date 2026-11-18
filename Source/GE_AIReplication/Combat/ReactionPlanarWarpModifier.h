#pragma once

#include "RootMotionModifier.h"
#include "ReactionPlanarWarpModifier.generated.h"

UCLASS()
class UReactionPlanarWarpModifier final : public URootMotionModifier_Scale
{
    GENERATED_BODY()

    void Update(const FMotionWarpingUpdateContext& Context) override;
};
