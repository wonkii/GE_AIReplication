#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ReactionEnvironmentComponent.generated.h"

class ACharacter;

enum class EReactionObstacleResult : uint8
{
    Clear,
    Blocked,
    InitialOverlap
};

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API UReactionEnvironmentComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category="Reaction Environment", meta=(ClampMin="0.0", Units="cm"))
    float DetectionRadiusCm = 200.f;

    UPROPERTY(EditAnywhere, Category="Reaction Environment", meta=(ClampMin="0.0", Units="cm"))
    float DetectionHalfHeightCm = 100.f;

    UPROPERTY(EditAnywhere, Category="Reaction Environment")
    TArray<TEnumAsByte<ECollisionChannel>> ObstacleObjectChannels = {ECC_WorldStatic};

    UPROPERTY(EditAnywhere, Category="Reaction Environment|Debug")
    bool bDrawDebug = false;

    EReactionObstacleResult FindObstacle(const ACharacter& Character, const FVector& Direction,
        double Travel, double LateralToleranceCm, int32 ReactionId, FHitResult& Hit) const;

    void DrawTargets(const UWorld& World, const FVector& SafeCapsuleEnd, const FVector& WarpTarget) const;
};
