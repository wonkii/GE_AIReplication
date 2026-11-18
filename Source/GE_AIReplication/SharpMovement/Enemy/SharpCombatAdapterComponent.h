#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpCombatAdapterComponent.generated.h"

/** Sharp-specific suspension and presentation policy. Generic combat components know nothing about experiments. */
UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpCombatAdapterComponent : public UActorComponent
{
    GENERATED_BODY()
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void ReactionChanged(bool bActive, int32 ReactionId);
};
