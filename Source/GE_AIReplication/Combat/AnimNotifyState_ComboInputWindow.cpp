#include "AnimNotifyState_ComboInputWindow.h"
#include "MeleeAttackComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Components/SkeletalMeshComponent.h"

UAnimNotifyState_ComboInputWindow::UAnimNotifyState_ComboInputWindow() { bIsNativeBranchingPoint = true; }

void UAnimNotifyState_ComboInputWindow::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
    if (auto* Mesh = Payload.SkelMeshComponent; Mesh && Mesh->GetOwner())
        if (auto* Attack = Mesh->GetOwner()->FindComponentByClass<UMeleeAttackComponent>())
            Attack->BeginComboInputWindow(Payload.SequenceAsset, Payload.MontageInstanceID, this, NextAttackIndex);
}

void UAnimNotifyState_ComboInputWindow::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
    if (auto* Mesh = Payload.SkelMeshComponent; Mesh && Mesh->GetOwner())
        if (auto* Attack = Mesh->GetOwner()->FindComponentByClass<UMeleeAttackComponent>())
            Attack->EndComboInputWindow(Payload.SequenceAsset, Payload.MontageInstanceID, this, Payload.bReachedEnd);
}
