#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CombatHitReceiver.generated.h"

USTRUCT(BlueprintType)
struct GE_AIREPLICATION_API FCombatHit
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> Attacker = nullptr;
    UPROPERTY(BlueprintReadOnly) int32 AttackId = 0;
    UPROPERTY(BlueprintReadOnly) FVector ImpactPoint = FVector::ZeroVector;
};

/** Implement on an Actor or one of its components. Only the server delivers authoritative hits. */
UINTERFACE(BlueprintType)
class GE_AIREPLICATION_API UCombatHitReceiver : public UInterface
{
    GENERATED_BODY()
};

class GE_AIREPLICATION_API ICombatHitReceiver
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat")
    bool ReceiveCombatHit(const FCombatHit& Hit);
};
