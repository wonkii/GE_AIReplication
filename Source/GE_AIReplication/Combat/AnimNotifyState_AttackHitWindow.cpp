#include "AnimNotifyState_AttackHitWindow.h"
#include "MeleeAttackComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"

UAnimNotifyState_AttackHitWindow::UAnimNotifyState_AttackHitWindow() { bIsNativeBranchingPoint = true; }

void UAnimNotifyState_AttackHitWindow::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
    if (auto* Mesh = Payload.SkelMeshComponent; Mesh && Mesh->GetOwner())
        if (auto* Attack = Mesh->GetOwner()->FindComponentByClass<UMeleeAttackComponent>())
            Attack->BeginHitWindow(Payload.SequenceAsset, Payload.MontageInstanceID, this, SocketName, BoxExtent, Offset);
}
void UAnimNotifyState_AttackHitWindow::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
    if (auto* Mesh = Payload.SkelMeshComponent; Mesh && Mesh->GetOwner())
        if (auto* Attack = Mesh->GetOwner()->FindComponentByClass<UMeleeAttackComponent>())
            Attack->EndHitWindow(Payload.SequenceAsset, Payload.MontageInstanceID, this);
}
