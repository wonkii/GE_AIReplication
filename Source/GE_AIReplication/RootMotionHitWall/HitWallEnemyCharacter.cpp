#include "HitWallEnemyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

void AHitWallEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();
    InitialTransform = GetActorTransform();
    if (auto* Anim = GetMesh()->GetAnimInstance()) Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
}

AHitWallEnemyCharacter::AHitWallEnemyCharacter()
{
    bReplicates = true;
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);
    SetReplicateMovement(true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->bEnableUpdateRateOptimizations = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    bUseControllerRotationYaw = false;
}
void AHitWallEnemyCharacter::ResetReaction()
{
    if (!HasAuthority() || bReacting) return;
    GetCharacterMovement()->StopMovementImmediately();
    SetActorTransform(InitialTransform, false, nullptr, ETeleportType::TeleportPhysics);
    ForceNetUpdate();
}
bool AHitWallEnemyCharacter::ReceiveAttack(AActor* Attacker, int32 AttackId)
{
    if (!HasAuthority() || bReacting || !Attacker || !HitMontage) return false;
    const FVector Direction = (Attacker->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    if (FVector::DotProduct(GetActorForwardVector(), Direction) < FMath::Cos(FMath::DegreesToRadians(FrontHalfAngle))) return false;
    GetCharacterMovement()->StopMovementImmediately();
    UE_LOG(LogTemp, Display, TEXT("[HitWall] Hit attacker=%s AttackId=%d ReactionId=%d"), *Attacker->GetName(), AttackId, ReactionId + 1);
    MulticastReaction(++ReactionId);
    ForceNetUpdate();
    return true;
}
void AHitWallEnemyCharacter::MulticastReaction_Implementation(int32 Id)
{
    if (Id <= LastPlayedId) return;
    LastPlayedId = Id;
    auto* Anim = GetMesh()->GetAnimInstance();
    if (!Anim || !HitMontage) return;
    bReacting = true;
    if (Anim->Montage_Play(HitMontage) <= 0.f) { bReacting = false; return; }
    FOnMontageEnded Delegate;
    Delegate.BindUObject(this, &ThisClass::ReactionEnded);
    Anim->Montage_SetEndDelegate(Delegate, HitMontage);
}
void AHitWallEnemyCharacter::ReactionEnded(UAnimMontage*, bool bInterrupted)
{
    bReacting = false;
    UE_LOG(LogTemp, Display, TEXT("[HitWall] ReactionEnd Id=%d Interrupted=%d Role=%d"), LastPlayedId, bInterrupted, int32(GetLocalRole()));
}
