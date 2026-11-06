#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_ComboInputWindow.generated.h"

/** Place on an attack Montage: accept one next-attack input, then branch at this window's end. */
UCLASS(meta=(DisplayName="Combo Input Window"))
class GE_AIREPLICATION_API UAnimNotifyState_ComboInputWindow : public UAnimNotifyState
{
    GENERATED_BODY()
public:
    UAnimNotifyState_ComboInputWindow();
    /** Zero-based entry in the owning MeleeAttackComponent's AttackMontages array. */
    UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="0")) int32 NextAttackIndex = 1;
    virtual void BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload) override;
    virtual void BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload) override;
};
