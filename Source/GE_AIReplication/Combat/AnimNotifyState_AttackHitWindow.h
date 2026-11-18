#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_AttackHitWindow.generated.h"
UCLASS(meta=(DisplayName="Attack Hit Window"))
class GE_AIREPLICATION_API UAnimNotifyState_AttackHitWindow : public UAnimNotifyState
{
    GENERATED_BODY()
public:
    UAnimNotifyState_AttackHitWindow();
    UPROPERTY(EditAnywhere, Category="Combat") FName SocketName = TEXT("AttackSocket_R");
    UPROPERTY(EditAnywhere, Category="Hit") FVector BoxExtent = FVector(28, 28, 28);
    UPROPERTY(EditAnywhere, Category="Hit") FVector Offset = FVector::ZeroVector;
    virtual void BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload) override;
    virtual void BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload) override;
};
