#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_AttackHitWindow.generated.h"
UCLASS(meta=(DisplayName="Attack Hit Window"))
class GE_AIREPLICATION_API UAnimNotifyState_AttackHitWindow : public UAnimNotifyState
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Hit") FName SocketName = TEXT("AttackSocket_R");
    UPROPERTY(EditAnywhere, Category="Hit") FVector BoxExtent = FVector(28, 28, 28);
    UPROPERTY(EditAnywhere, Category="Hit") FVector Offset = FVector::ZeroVector;
    virtual void NotifyBegin(USkeletalMeshComponent*, UAnimSequenceBase*, float, const FAnimNotifyEventReference&) override;
    virtual void NotifyEnd(USkeletalMeshComponent*, UAnimSequenceBase*, const FAnimNotifyEventReference&) override;
};
