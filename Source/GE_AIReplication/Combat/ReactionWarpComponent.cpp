#include "ReactionWarpComponent.h"
#include "ReactionEnvironmentComponent.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier_SkewWarp.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"

FReactionWarpTarget UReactionWarpComponent::Plan(
    const ACharacter& Character, const UAnimMontage& Montage, float FacingYaw, int32 ReactionId) const
{
    if (!bEnableWarp || Mode != EReactionWarpMode::Legacy) return {};

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

bool UReactionWarpComponent::ResolveCandidate(FHitWallReactionCandidate& Out, FString& Error) const
{
    if (!Library || !Policy) { Error = TEXT("MissingLibraryOrPolicy"); return false; }
    // V1 analysis is accepted only with a separate, explicitly bound V2 policy.
    int32 Count = 0;
    for (const auto& C : Library->Candidates) if (C.CandidateId == Policy->CandidateId) { Out = C; ++Count; }
    if (Count != 1) { Error = TEXT("CandidateMissingOrAmbiguous"); return false; }
    Error = ReactionWarp::Validate(Out, *Policy, bAllowDraftSession);
    return Error.IsEmpty();
}

bool UReactionWarpComponent::ValidateLocalPlan(const FReactionWarpPlan& P, FHitWallReactionCandidate& Out, FString& Error) const
{
    // The server's draft flag permits only consumption of this exact payload. It does
    // not change the client's game lookup policy or allow local optimization.
    const auto* LocalLibrary=P.Library.LoadSynchronous();
    const auto* LocalPolicy=P.Policy.LoadSynchronous();
    if (!LocalLibrary || !LocalPolicy) { Error=TEXT("MissingLocalCookedData"); return false; }
    int32 Count=0;
    for (const auto& C : LocalLibrary->Candidates) if (C.CandidateId==P.CandidateId) { Out=C; ++Count; }
    Error = Count==1 ? ReactionWarp::Validate(Out,*LocalPolicy,P.bDraft) : TEXT("CandidateMissingOrAmbiguous");
    if (!Error.IsEmpty()) return false;
    if (P.SchemaVersion!=2 || P.Keys.Num()<2 || P.Keys.Num()>ReactionWarp::MaxKeys ||
        P.PhaseTravel.Num()>ReactionWarp::MaxPhases || P.SourceHash!=Out.SourceHash || P.AnalysisHash!=Out.AnalysisHash ||
        P.PolicyHash!=ReactionWarp::PolicyHash(*LocalPolicy) || P.PlanHash!=ReactionWarp::PlanHash(P) ||
        P.Montage!=Out.Animation.Get() || P.PlayRate!=1.f || !P.bPathChecked)
    { Error=TEXT("LocalPlanIdentityMismatch"); return false; }
    float Previous=-1.f;
    for (const auto& K : P.Keys)
    {
        if (!FMath::IsFinite(K.Time) || !FMath::IsFinite(K.Scale) || K.Time<=Previous || K.Scale<0.f || K.Scale>1.f)
        { Error=TEXT("InvalidReplicatedCurve"); return false; }
        Previous=K.Time;
    }
    if (P.Keys[0].Time!=0.f || !FMath::IsNearlyEqual(Previous,Out.DurationSeconds,1.e-4f))
    { Error=TEXT("InvalidReplicatedCurveRange"); return false; }
    for (const auto& S : Out.Trajectory)
        if (!UMotionWarpingUtilities::ExtractRootMotionFromAnimation(P.Montage,0.f,S.AssetTimeSeconds).Equals(S.RootMotion,0.05))
        { Error=TEXT("LocalCookedRootTrackMismatch"); return false; }
    return true;
}

FReactionWarpPlan UReactionWarpComponent::PlanMulti(const ACharacter& Character, UAnimMontage& Montage,
    float FacingYaw, int32 ReactionId, double ServerStartTime) const
{
    FReactionWarpPlan R; R.Mode=Mode; R.Montage=&Montage; R.ReactionId=ReactionId; R.ServerStartTime=ServerStartTime;
    R.Library=Library.Get(); R.Policy=Policy.Get(); R.bDraft=bAllowDraftSession;
    if (Policy)
    {
        R.CandidateId=Policy->CandidateId; R.SourceHash=Policy->SourceHash; R.AnalysisHash=Policy->AnalysisHash;
        R.PolicyHash=ReactionWarp::PolicyHash(*Policy);
    }
    const auto Seal = [&]() { R.PlanHash=ReactionWarp::PlanHash(R); return R; };
    const auto Fail = [&](const FString& Reason)
    {
        R.Result=EReactionWarpResult::Fallback; R.Reason=Reason; R.Keys.Reset();
        UE_LOG(LogTemp, Display, TEXT("[ReactionWarp] Actor=%s Id=%d Fallback=%s"), *Character.GetName(),ReactionId,*Reason);
        return Seal();
    };
    if (Mode==EReactionWarpMode::Legacy || Mode==EReactionWarpMode::Off || !bEnableWarp) { R.Reason=TEXT("Disabled"); return Seal(); }
    if (!Character.HasAuthority()) return Fail(TEXT("AuthorityRequired"));
    if (!Character.GetCharacterMovement()->IsMovingOnGround()) return Fail(TEXT("GroundRequired"));
    // Owner prediction needs a saved-plan replay fixture; do not silently claim support.
    if (Character.IsPlayerControlled()) return Fail(TEXT("OwnerPredictionNotValidated"));
    auto* Warping=Character.FindComponentByClass<UMotionWarpingComponent>();
    if (!Warping || Warping->GetIsReplicated()) return Fail(TEXT("MissingOrReplicatedWarpComponent"));
    if (!Character.GetMesh()->GetComponentScale().Equals(FVector::OneVector,1.e-4)) return Fail(TEXT("UnsupportedMeshScale"));
    if (!Montage.HasRootMotion() || Montage.RateScale!=1.f || Montage.CompositeSections.Num()!=1 ||
        !Montage.CompositeSections[0].NextSectionName.IsNone() || Montage.TimeStretchCurve.IsValid() ||
        Montage.SlotAnimTracks.Num()!=1 || Montage.SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1)
        return Fail(TEXT("UnsupportedMontage"));
    const auto& Segment=Montage.SlotAnimTracks[0].AnimTrack.AnimSegments[0];
    if (Segment.LoopingCount!=1 || !FMath::IsNearlyEqual(Segment.GetValidPlayRate(),1.f)) return Fail(TEXT("UnsupportedSegmentPlayback"));
    FHitWallReactionCandidate C; FString Error;
    if (!ResolveCandidate(C,Error)) return Fail(Error);
    if (C.Animation.LoadSynchronous()!=&Montage || !FMath::IsNearlyEqual(C.DurationSeconds,Montage.GetPlayLength(),1.e-4f))
        return Fail(TEXT("MontageBindingMismatch"));
    // Compare the baked track to the installed/cooked extraction; no runtime file SHA.
    for (const auto& S : C.Trajectory)
        if (!UMotionWarpingUtilities::ExtractRootMotionFromAnimation(&Montage,0.f,S.AssetTimeSeconds).Equals(S.RootMotion,0.05))
            return Fail(TEXT("CookedRootTrackMismatch"));
    for (auto* Modifier : Warping->GetModifiers())
        if (Modifier && Modifier->GetState()!=ERootMotionModifierState::MarkedForRemoval && Modifier->GetState()!=ERootMotionModifierState::Disabled)
            return Fail(TEXT("ModifierOwnershipConflict"));
    TArray<FMotionWarpingWindowData> Windows;
    UMotionWarpingUtilities::GetMotionWarpingWindowsFromAnimation(&Montage,Windows);
    for (const auto& W : Windows)
    {
        const auto* Skew=Cast<URootMotionModifier_SkewWarp>(W.AnimNotify->RootMotionModifier);
        if (!Skew || Warping->FindWarpTarget(Skew->WarpTargetName)) return Fail(TEXT("AuthoredModifierConflict"));
    }
    const FQuat MeshFacing=FRotator(0.f,FacingYaw,0.f).Quaternion()*Character.GetBaseRotationOffset();
    const FVector LocalDirection=C.Trajectory.Last().RootMotion.GetTranslation().GetSafeNormal2D();
    const FVector Direction=MeshFacing.RotateVector(LocalDirection);
    const float Natural=C.Trajectory.Last().RootMotion.GetTranslation().Size2D();
    const auto* Capsule=Character.GetCapsuleComponent();
    const FVector Start=Capsule->GetComponentLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ReactionMultiWarp),false,&Character);
    FCollisionResponseParams Response; Capsule->InitSweepCollisionParams(Query,Response);
    UWorld* World=GetWorld();
    if (World->OverlapBlockingTestByChannel(Start,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),Capsule->GetCollisionShape(),Query,Response))
        return Fail(TEXT("InitialOverlap"));
    const auto* Environment=Character.FindComponentByClass<UReactionEnvironmentComponent>();
    if (!Environment) return Fail(TEXT("MissingReactionEnvironment"));
    if (Capsule->GetScaledCapsuleRadius()+Policy->LateralLimitCm>Capsule->GetScaledCapsuleHalfHeight())
        return Fail(TEXT("LateralEnvelopeTooLarge"));
    FHitResult Hit;
    const auto Obstacle=Environment->FindObstacle(Character,Direction,Natural,Policy->LateralLimitCm,ReactionId,Hit);
    if (Obstacle==EReactionObstacleResult::InitialOverlap) return Fail(TEXT("InitialEnvelopeOverlap"));
    float Budget=Obstacle==EReactionObstacleResult::Blocked ? FMath::Clamp(Hit.Distance-WallClearance,0.f,Natural) : Natural;
    for (int32 Attempt=0; Attempt<4; ++Attempt)
    {
        R=ReactionWarp::Build(C,*Policy,Budget,Mode,bAllowDraftSession);
        R.Montage=&Montage; R.ReactionId=ReactionId; R.ServerStartTime=ServerStartTime;
        R.Library=Library.Get(); R.Policy=Policy.Get();
        if (R.Result==EReactionWarpResult::Fallback) return Fail(R.Reason);
        bool Clear=true;
        // Within each source/key interval q is a vector-valued quadratic. Splitting
        // to <=1cm source travel and inflating XY by 0.02cm bounds chord error.
        TArray<float> Times; Times.Add(0.f);
        for (const auto& S : C.Trajectory) Times.AddUnique(S.AssetTimeSeconds);
        for (const auto& K : R.Keys) Times.AddUnique(K.Time);
        Times.Sort();
        FVector Previous=Start;
        const float Radius=Capsule->GetScaledCapsuleRadius()+0.02f;
        const float Half=Capsule->GetScaledCapsuleHalfHeight();
        if (Radius>Half) return Fail(TEXT("UnsupportedCapsuleShape"));
        for (int32 I=1; I<Times.Num() && Clear; ++I)
        {
            const double Length=ReactionWarp::Integrate(C.Trajectory,{},Times[I-1],Times[I]).Size2D();
            const int32 Steps=FMath::Max(1,FMath::CeilToInt(Length));
            for (int32 J=1; J<=Steps; ++J)
            {
                const float Time=FMath::Lerp(Times[I-1],Times[I],float(J)/Steps);
                const FVector Next=Start+MeshFacing.RotateVector(ReactionWarp::Integrate(C.Trajectory,R.Keys,0.f,Time));
                if (World->SweepSingleByChannel(Hit,Previous,Next,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),
                    FCollisionShape::MakeCapsule(Radius,Half),Query,Response)) { Clear=false; break; }
                if (Environment->bDrawDebug) DrawDebugLine(World,Previous,Next,FColor::Green,false,2.f);
                Previous=Next;
            }
        }
        if (Clear) { R.bPathChecked=true; return Seal(); }
        if (Hit.GetComponent() && Hit.GetComponent()->Mobility!=EComponentMobility::Static) return Fail(TEXT("DynamicObstacleUnsupported"));
        Budget=FMath::Max(0.f,Budget-FMath::Max(5.f,WallClearance));
    }
    return Fail(TEXT("FullPathSweepRetryLimit"));
}
