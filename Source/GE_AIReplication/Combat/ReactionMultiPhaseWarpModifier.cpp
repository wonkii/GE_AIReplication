#include "ReactionMultiPhaseWarpModifier.h"
#include "MotionWarpingComponent.h"

void UReactionMultiPhaseWarpModifier::Update(const FMotionWarpingUpdateContext& Context)
{
    // UE's character adapter can lose the montage before the last extracted root delta.
    if (!Context.Animation.IsValid() && GetState() == ERootMotionModifierState::Active)
    {
        PreviousPosition = CurrentPosition;
        CurrentPosition = EndTime;
        bFinalInterval = true;
        return;
    }
    if (Context.Animation == Animation && (!FMath::IsNearlyEqual(Context.PlayRate, 1.f) ||
        Context.CurrentPosition < Context.PreviousPosition))
    {
        SetState(ERootMotionModifierState::Disabled);
        UE_LOG(LogTemp, Warning, TEXT("[ReactionWarp] Unsupported runtime rate/section jump Id=%d"), Plan.ReactionId);
        return;
    }
    Super::Update(Context);
    // Modifiers added by animation notifies must never stack on this plan.
    if (auto* Owner = GetOwnerComponent())
        for (auto* Other : Owner->GetModifiers())
            if (Other && Other != this && Other->GetState() == ERootMotionModifierState::Active)
            {
                SetState(ERootMotionModifierState::Disabled);
                UE_LOG(LogTemp, Warning, TEXT("[ReactionWarp] ModifierOwnershipConflict Id=%d"), Plan.ReactionId);
                break;
            }
}

FTransform UReactionMultiPhaseWarpModifier::Correct(const FTransform& Input,
    const TArray<FHitWallTrajectorySample>& Samples, const TArray<FReactionWarpScaleKey>& Keys, float Start, float End,
    const FVector* BlendReference)
{
    const FVector Raw = ReactionWarp::Integrate(Samples, {}, Start, End);
    const FVector Warped = ReactionWarp::Integrate(Samples, Keys, Start, End);
    const FVector In = Input.GetTranslation();
    const FVector& Reference = BlendReference ? *BlendReference : Raw;
    const double Denominator = Reference.SizeSquared2D();
    // Pre-world-conversion Input already contains animation blend weighting. Recover
    // that contribution from the supported single fixed-yaw root track. This also
    // works on the final delta when UE's context no longer supplies montage weight.
    // Add only the correction; never replace the input with an unweighted raw delta.
    const double Blend = Denominator > 1.e-12 ? FMath::Clamp((In.X*Reference.X + In.Y*Reference.Y)/Denominator, 0., 1.) : 0.;
    FTransform Output = Input;
    const FVector Correction = (Warped - Raw) * Blend;
    Output.SetTranslation(In + FVector(Correction.X, Correction.Y, 0.));
    return Output;
}

FTransform UReactionMultiPhaseWarpModifier::ProcessRootMotion(const FTransform& Input, float)
{
    // Recover blend contribution from the engine's exact interval extraction rather
    // than the sampled linear model. The correction itself still uses the shared
    // integrator, preserving raw animation inside fully locked time intervals.
    const FVector Reference = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Animation.Get(),PreviousPosition,CurrentPosition).GetTranslation();
    const FTransform Output = Correct(Input, Track, Plan.Keys, PreviousPosition, CurrentPosition, &Reference);
    if (bFinalInterval) SetState(ERootMotionModifierState::MarkedForRemoval);
    return Output;
}
