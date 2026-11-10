#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Reaction/ReactionWarpPlan.h"
#include "ReactionWarpComponent.generated.h"

class ACharacter;
class UAnimMontage;

USTRUCT()
struct FReactionWarpTarget
{
    GENERATED_BODY()

    UPROPERTY() FName Name = NAME_None;
    UPROPERTY() FVector Location = FVector::ZeroVector;
    UPROPERTY() float PlanarScale = 1.f;
};

/** Opt-in planner for planar, straight root-motion reactions against a static obstacle. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API UReactionWarpComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Reaction Warp|Plan") EReactionWarpMode Mode = EReactionWarpMode::Legacy;
    UPROPERTY(EditAnywhere, Category="Reaction Warp|Plan") TObjectPtr<UHitWallReactionLibrary> Library;
    UPROPERTY(EditAnywhere, Category="Reaction Warp|Plan") TObjectPtr<UReactionWarpPolicy> Policy;
    UPROPERTY(EditAnywhere, Category="Reaction Warp") bool bEnableWarp = false;
    UPROPERTY(EditAnywhere, Category="Reaction Warp") FName TargetName = NAME_None;
    UPROPERTY(EditAnywhere, Category="Reaction Warp", meta=(ClampMin="0.0")) float WallClearance = 5.f;
    UPROPERTY(EditAnywhere, Category="Reaction Warp", meta=(ClampMin="0.0", Units="cm")) float LateralToleranceCm = 0.1f;

    FReactionWarpTarget Plan(const ACharacter& Character, const UAnimMontage& Montage, float FacingYaw, int32 ReactionId) const;
    FReactionWarpPlan PlanMulti(const ACharacter& Character, UAnimMontage& Montage, float FacingYaw,
        int32 ReactionId, double ServerStartTime) const;
    bool ResolveCandidate(FHitWallReactionCandidate& Out, FString& Error) const;
    bool ValidateLocalPlan(const FReactionWarpPlan& Plan, FHitWallReactionCandidate& Out, FString& Error) const;
    // Explicit debug-session opt-in, never saved into an asset or replicated component.
    bool bAllowDraftSession = false;
};
