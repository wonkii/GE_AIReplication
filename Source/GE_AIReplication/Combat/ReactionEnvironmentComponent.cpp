#include "ReactionEnvironmentComponent.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

EReactionObstacleResult UReactionEnvironmentComponent::FindObstacle(const ACharacter& Character,
    const FVector& Direction, double Travel, double LateralToleranceCm, int32 ReactionId, FHitResult& Hit) const
{
    Hit = FHitResult();
    const UWorld* World = GetWorld();
    if (!World || !Character.HasAuthority() || DetectionRadiusCm <= 0.f || DetectionHalfHeightCm <= 0.f)
        return EReactionObstacleResult::Clear;

    const auto* Capsule = Character.GetCapsuleComponent();
    const FVector Start = Capsule->GetComponentLocation();
    const FVector End = Start + Direction * Travel;
    const float SweepRadius = Capsule->GetScaledCapsuleRadius() + LateralToleranceCm;
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    if (SweepRadius > HalfHeight) return EReactionObstacleResult::Clear;

    FCollisionQueryParams Query(SCENE_QUERY_STAT(ReactionEnvironment), false, &Character);
    FCollisionResponseParams Response;
    Capsule->InitSweepCollisionParams(Query, Response);
    FCollisionObjectQueryParams Objects;
    for (const auto Channel : ObstacleObjectChannels)
    {
        if (Response.CollisionResponse.GetResponse(Channel) == ECR_Block)
            Objects.AddObjectTypesToQuery(Channel);
    }
    if (!Objects.IsValid()) return EReactionObstacleResult::Clear;

    // A box encloses the sensing cylinder. Bounds prune candidates conservatively;
    // the swept surface contact below supplies the exact cylinder membership test.
    TArray<FOverlapResult> Overlaps;
    World->OverlapMultiByObjectType(Overlaps, Start, FQuat::Identity, Objects,
        FCollisionShape::MakeBox(FVector(DetectionRadiusCm, DetectionRadiusCm, DetectionHalfHeightCm)), Query);
    TSet<const UPrimitiveComponent*> Candidates;
    const double RadiusSquared = FMath::Square(DetectionRadiusCm);
    for (const FOverlapResult& Overlap : Overlaps)
    {
        const auto* Component = Overlap.GetComponent();
        if (!Component || Component->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block)
            continue;
        const FBox Bounds = Component->Bounds.GetBox();
        const FVector Closest = Bounds.GetClosestPointTo(Start);
        if (FVector::DistSquared2D(Closest, Start) <= RadiusSquared &&
            FMath::Abs(Closest.Z - Start.Z) <= DetectionHalfHeightCm)
        {
            Candidates.Add(Component);
            if (bDrawDebug)
                DrawDebugBox(World, Bounds.GetCenter(), Bounds.GetExtent(), FColor::Yellow, false, 3.f);
        }
    }

    // Preserve movement blocking order, including blockers outside the sensing filter.
    const bool bBlocked = !Candidates.IsEmpty() && World->SweepSingleByChannel(Hit, Start, End,
        Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(SweepRadius, HalfHeight), Query, Response);
    EReactionObstacleResult Result = EReactionObstacleResult::Clear;
    if (bBlocked && Candidates.Contains(Hit.GetComponent()) &&
        FVector::DistSquared2D(Hit.ImpactPoint, Start) <= RadiusSquared &&
        FMath::Abs(Hit.ImpactPoint.Z - Start.Z) <= DetectionHalfHeightCm)
        Result = Hit.bStartPenetrating ? EReactionObstacleResult::InitialOverlap : EReactionObstacleResult::Blocked;

    if (bDrawDebug)
    {
        const FVector Height(0., 0., DetectionHalfHeightCm);
        DrawDebugCylinder(World, Start - Height, Start + Height, DetectionRadiusCm, 32, FColor::Cyan, false, 3.f);
        DrawDebugLine(World, Start, End, FColor::White, false, 3.f);
        if (Result == EReactionObstacleResult::Blocked)
        {
            DrawDebugPoint(World, Hit.ImpactPoint, 12.f, FColor::Red, false, 3.f);
            DrawDebugDirectionalArrow(World, Hit.ImpactPoint, Hit.ImpactPoint + Hit.ImpactNormal * 25.f,
                8.f, FColor::Red, false, 3.f);
            DrawDebugCapsule(World, Hit.Location, HalfHeight, SweepRadius, Capsule->GetComponentQuat(),
                FColor::Orange, false, 3.f);
        }
        UE_LOG(LogTemp, Display, TEXT("[ReactionEnvironment] Actor=%s Id=%d Candidates=%d Result=%d Blocker=%s Contact=%s Distance=%.2f"),
            *Character.GetName(), ReactionId, Candidates.Num(), int32(Result), *GetNameSafe(Hit.GetComponent()),
            *Hit.ImpactPoint.ToString(), Hit.Distance);
    }
    return Result;
}

void UReactionEnvironmentComponent::DrawTargets(const UWorld& World, const FVector& SafeCapsuleEnd,
    const FVector& WarpTarget) const
{
    if (!bDrawDebug) return;
    DrawDebugSphere(&World, SafeCapsuleEnd, 8.f, 12, FColor::Green, false, 3.f);
    DrawDebugSphere(&World, WarpTarget, 6.f, 12, FColor::Magenta, false, 3.f);
}
