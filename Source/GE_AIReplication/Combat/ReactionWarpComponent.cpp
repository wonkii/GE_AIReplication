#include "ReactionWarpComponent.h"
#include "ReactionEnvironmentComponent.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier_SkewWarp.h"

FReactionWarpTarget UReactionWarpComponent::Plan(
    const ACharacter& Character, const UAnimMontage& Montage, float FacingYaw, int32 ReactionId) const
{
    if (!bEnableWarp) return {};

    const auto Fallback = [&](const TCHAR* Reason) -> FReactionWarpTarget
    {
        UE_LOG(LogTemp, Display, TEXT("[ReactionWarp] Actor=%s Id=%d Fallback=%s"),
            *Character.GetName(), ReactionId, Reason);
        return {};
    };
    const auto* Warping = Character.FindComponentByClass<UMotionWarpingComponent>();
    if (!Warping || TargetName.IsNone()) return Fallback(TEXT("MissingWarpConfiguration"));
    // The reaction state delivers the target and montage together. A second
    // replicated target list could overwrite the local start or cleanup.
    if (Warping->GetIsReplicated()) return Fallback(TEXT("DisableWarpComponentReplication"));
    if (Warping->FindWarpTarget(TargetName)) return Fallback(TEXT("TargetInUse"));
    if (!Montage.HasRootMotion() || Montage.CompositeSections.Num() != 1 ||
        !Montage.CompositeSections[0].NextSectionName.IsNone())
    {
        return Fallback(TEXT("UnsupportedMontage"));
    }

    TArray<FMotionWarpingWindowData> Windows;
    UMotionWarpingUtilities::GetMotionWarpingWindowsFromAnimation(&Montage, Windows);
    if (Windows.Num() != 1) return Fallback(TEXT("ExpectedOneWindow"));
    const auto& Window = Windows[0];
    const auto* Modifier = Cast<URootMotionModifier_SkewWarp>(Window.AnimNotify->RootMotionModifier);
    const float Length = Montage.GetPlayLength();
    if (!Modifier || Modifier->WarpTargetName != TargetName || !Modifier->bWarpTranslation ||
        !Modifier->bIgnoreZAxis || Modifier->bWarpRotation || Modifier->WarpPointAnimProvider != EWarpPointAnimProvider::None ||
        Window.StartTime < 0.f || Window.EndTime <= Window.StartTime || Window.EndTime > Length)
    {
        return Fallback(TEXT("UnsupportedWindow"));
    }

    const auto* Capsule = Character.GetCapsuleComponent();
    const FVector Start = Capsule->GetComponentLocation();
    const FQuat Facing = FRotator(0.f, FacingYaw, 0.f).Quaternion();
    const FQuat MeshFacing = Facing * Character.GetBaseRotationOffset();
    const auto Extract = [&](float Time)
    {
        return UMotionWarpingUtilities::ExtractRootMotionFromAnimation(&Montage, 0.f, Time);
    };
    const FVector Total = MeshFacing.RotateVector(Extract(Length).GetTranslation());
    const FVector Direction = Total.GetSafeNormal2D();
    const double NaturalTravel = Total.Size2D();
    if (Direction.IsNearlyZero()) return Fallback(TEXT("NoPlanarTravel"));

    constexpr double VerticalToleranceCm = 0.1;
    const double LateralLimitCm = FMath::Max(0.f, LateralToleranceCm);
    double MaxLateralCm = 0.;
    float MaxLateralTimeSec = 0.f;
    bool bRejectedTrack = false;
    double PreviousTravel = 0.;
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length * 60.f));
    for (int32 Step = 1; Step <= Steps; ++Step)
    {
        const float SampleTime = Length * Step / Steps;
        const FTransform Root = Extract(SampleTime);
        const FVector Offset = MeshFacing.RotateVector(Root.GetTranslation());
        if (Offset.ContainsNaN())
        {
            UE_LOG(LogTemp, Display, TEXT("[ReactionWarp] Actor=%s Id=%d Montage=%s Sample=%d/%d TimeSec=%.4f InvalidOffset=%s"),
                *Character.GetName(), ReactionId, *Montage.GetName(), Step, Steps, SampleTime, *Offset.ToString());
            return Fallback(TEXT("NonStraightRootTrack"));
        }
        const double Travel = FVector::DotProduct(Offset, Direction);
        const FVector OffAxis = Offset - Direction * Travel;
        const double LateralCm = OffAxis.Size2D();
        if (LateralCm > MaxLateralCm)
        {
            MaxLateralCm = LateralCm;
            MaxLateralTimeSec = SampleTime;
        }
        const bool bLateral = LateralCm > LateralLimitCm;
        const bool bVertical = FMath::Abs(Offset.Z) > VerticalToleranceCm;
        const bool bReverse = Travel + UE_KINDA_SMALL_NUMBER < PreviousTravel;
        const bool bRotated = !Root.GetRotation().Equals(FQuat::Identity, UE_KINDA_SMALL_NUMBER);
        if (!bRejectedTrack && (bLateral || bVertical || bReverse || bRotated))
        {
            UE_LOG(LogTemp, Display,
                TEXT("[ReactionWarp] Actor=%s Id=%d Montage=%s Sample=%d/%d TimeSec=%.4f LateralFail=%d LateralCm=%.5f LateralLimitCm=%.5f VerticalFail=%d ZCm=%.5f VerticalLimitCm=%.5f ReverseFail=%d BacktrackCm=%.5f RotationFail=%d RootRotation=%s"),
                *Character.GetName(), ReactionId, *Montage.GetName(), Step, Steps, SampleTime,
                bLateral, LateralCm, LateralLimitCm, bVertical, Offset.Z, VerticalToleranceCm,
                bReverse, FMath::Max(0., PreviousTravel - Travel), bRotated, *Root.GetRotation().Rotator().ToString());
            bRejectedTrack = true;
        }
        PreviousTravel = Travel;
    }
    UE_LOG(LogTemp, Display, TEXT("[ReactionWarp] Actor=%s Id=%d Montage=%s Samples=%d MaxLateralCm=%.5f MaxLateralTimeSec=%.4f LateralLimitCm=%.5f TrackAccepted=%d"),
        *Character.GetName(), ReactionId, *Montage.GetName(), Steps, MaxLateralCm, MaxLateralTimeSec, LateralLimitCm, !bRejectedTrack);
    if (bRejectedTrack) return Fallback(TEXT("NonStraightRootTrack"));

    const auto* Environment = Character.FindComponentByClass<UReactionEnvironmentComponent>();
    if (!Environment) return Fallback(TEXT("MissingReactionEnvironment"));
    if (Capsule->GetScaledCapsuleRadius() + LateralLimitCm > Capsule->GetScaledCapsuleHalfHeight())
        return Fallback(TEXT("LateralToleranceTooLarge"));
    FHitResult Hit;
    const EReactionObstacleResult Obstacle = Environment->FindObstacle(Character, Direction, NaturalTravel,
        LateralLimitCm, ReactionId, Hit);
    if (Obstacle == EReactionObstacleResult::Clear) return {};

    // An overlap of the widened planning capsule leaves no conservative travel budget.
    const double SafeTravel = Obstacle == EReactionObstacleResult::InitialOverlap
        ? 0. : FMath::Clamp(Hit.Distance - WallClearance, 0., NaturalTravel);
    FReactionWarpTarget Target;
    Target.Name = TargetName;
    Target.Location = Start + Direction * SafeTravel;
    Target.PlanarScale = SafeTravel / NaturalTravel;
    Environment->DrawTargets(*GetWorld(), Target.Location, Target.Location);
    UE_LOG(LogTemp, Display, TEXT("[ReactionWarp] Actor=%s Id=%d Natural=%.2f Planned=%.2f PlanarScale=%.4f InitialOverlap=%d Target=%s"),
        *Character.GetName(), ReactionId, NaturalTravel, SafeTravel, Target.PlanarScale,
        Obstacle == EReactionObstacleResult::InitialOverlap, *Target.Location.ToString());
    return Target;
}