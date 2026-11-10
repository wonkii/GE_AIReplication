#pragma once

#include "RootMotionModifier.h"
#include "Reaction/ReactionWarpPlan.h"
#include "ReactionMultiPhaseWarpModifier.generated.h"

/** One modifier owns the whole curve. CMC still owns all movement and collision. */
UCLASS()
class GE_AIREPLICATION_API UReactionMultiPhaseWarpModifier : public URootMotionModifier
{
    GENERATED_BODY()
public:
    UPROPERTY() FReactionWarpPlan Plan;
    UPROPERTY() TArray<FHitWallTrajectorySample> Track;
    void Update(const FMotionWarpingUpdateContext& Context) override;
    FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
    static FTransform Correct(const FTransform& Input, const TArray<FHitWallTrajectorySample>& Samples,
        const TArray<FReactionWarpScaleKey>& Keys, float Start, float End, const FVector* BlendReference = nullptr);
private:
    bool bFinalInterval = false;
};
