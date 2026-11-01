#include "AnimNotifyState_AttackHitWindow.h"
#include "HitWallPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
void UAnimNotifyState_AttackHitWindow::NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, float, const FAnimNotifyEventReference&)
{
    if (Mesh) if (auto* Player = Cast<AHitWallPlayerCharacter>(Mesh->GetOwner())) Player->BeginHitWindow(SocketName, BoxExtent, Offset);
}
void UAnimNotifyState_AttackHitWindow::NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, const FAnimNotifyEventReference&)
{
    if (Mesh) if (auto* Player = Cast<AHitWallPlayerCharacter>(Mesh->GetOwner())) Player->EndHitWindow();
}
