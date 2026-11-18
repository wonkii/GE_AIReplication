#include "MeleeAttackComponent.h"
#include "CombatHitReceiver.h"
#include "HitReactionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"

UMeleeAttackComponent::UMeleeAttackComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UMeleeAttackComponent::BeginPlay()
{
    Super::BeginPlay();
    Character = Cast<ACharacter>(GetOwner());
    if (!Character || !bCombatEnabled) return;
    AttackBox = NewObject<UBoxComponent>(Character, TEXT("MeleeAttackBox"));
    AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttackBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    AttackBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    AttackBox->SetGenerateOverlapEvents(false);
    AttackBox->RegisterComponent();
    AttackBox->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    // Socket-based authority queries need bones even on a headless server.
    Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Character->GetMesh()->bEnableUpdateRateOptimizations = false;
    AddTickPrerequisiteComponent(Character->GetMesh());
}

void UMeleeAttackComponent::BindInput(UEnhancedInputComponent* Input)
{
    if (!bCombatEnabled || !Input || !AttackAction) return;
    Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ThisClass::RequestAttack);
    auto* PC = Cast<APlayerController>(GetOwner()->GetInstigatorController());
    if (PC && PC->IsLocalController() && AttackMappingContext)
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(AttackMappingContext, MappingPriority);
            InputSubsystem = Subsystem;
            InstalledContext = AttackMappingContext;
        }
}

void UMeleeAttackComponent::RequestAttack()
{
    if (!bCombatEnabled || (bAttacking && (!bComboInputWindow || bComboInputBuffered))) return;
    const int32 ExpectedId = bAttacking ? LastPlayedId : 0;
    ServerRequestAttack(ExpectedId);
    // Local gating prevents an early click from becoming valid just because its RPC arrived later.
    if (bAttacking && bComboInputWindow) bComboInputBuffered = true;
}

void UMeleeAttackComponent::ServerRequestAttack_Implementation(int32 ExpectedAttackId)
{
    if (!bCombatEnabled || !Character || !Character->GetController()) return;
    if (auto* Reaction = Character->FindComponentByClass<UHitReactionComponent>(); Reaction && Reaction->IsReacting()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (bAttacking)
    {
        if (ExpectedAttackId != LastPlayedId || !bComboInputWindow || bComboInputBuffered) return;
        LastRequestTime = Now;
        bComboInputBuffered = true;
        UE_LOG(LogTemp, Display, TEXT("[Combat] ComboInput Owner=%s Id=%d NextIndex=%d"), *GetOwner()->GetName(), LastPlayedId, NextComboIndex);
        return;
    }
    if (ExpectedAttackId != 0) return; // A late continuation cannot start an unrelated fresh combo.
    if (Now - LastRequestTime < 0.05) return;
    LastRequestTime = Now;
    StartAttack(0);
}

void UMeleeAttackComponent::StartAttack(int32 Index)
{
    if (!bCombatEnabled || AttackId == MAX_int32 || !AttackMontages.IsValidIndex(Index) || !AttackMontages[Index]) return;
    AlreadyHit.Reset();
    CloseHitWindow(); ClearComboWindow();
    Character->ConsumeMovementInputVector();
    Character->GetCharacterMovement()->StopMovementImmediately();
    MulticastAttack(Index, ++AttackId);
}

void UMeleeAttackComponent::MulticastAttack_Implementation(int32 Index, int32 Id)
{
    if (Id <= LastPlayedId || !Character || !AttackMontages.IsValidIndex(Index)) return;
    LastPlayedId = Id;
    // A newer server attack can arrive before the preceding local end delegate.
    if (bAttacking) ClearLocalAttack();
    auto* Anim = Character->GetMesh()->GetAnimInstance();
    if (!Anim) return;
    // Root Motion policy belongs to the AnimBP; do not override it for attacks.
    ActiveMontage = AttackMontages[Index];
    ClearComboWindow();
    bAttacking = true;
    SetComponentTickEnabled(true);
    if (Anim->Montage_Play(ActiveMontage) <= 0.f)
    {
        ClearLocalAttack(); return;
    }
    if (const auto* Instance = Anim->GetActiveInstanceForMontage(ActiveMontage)) ActiveMontageInstanceId = Instance->GetInstanceID();
    FOnMontageEnded Delegate;
    Delegate.BindUObject(this, &ThisClass::AttackEnded, Id);
    Anim->Montage_SetEndDelegate(Delegate, ActiveMontage);
    UE_LOG(LogTemp, Display, TEXT("[Combat] Attack Owner=%s Id=%d Index=%d Role=%d"), *GetOwner()->GetName(), Id, Index + 1, int32(GetOwner()->GetLocalRole()));
}

void UMeleeAttackComponent::RestoreAnimation()
{
    ActiveMontage = nullptr;
}

void UMeleeAttackComponent::AttackEnded(UAnimMontage*, bool bInterrupted, int32 Id)
{
    if (Id != LastPlayedId) return;
    // A Notify ending at the last montage frame may precede this callback in the same tick.
    const int32 NextIndex = NextComboIndex;
    const bool bContinue = bComboTransitionPending && !bInterrupted && GetOwner()->HasAuthority();
    CloseHitWindow(); ClearComboWindow(); bAttacking = false; ActiveMontageInstanceId = INDEX_NONE;
    SetComponentTickEnabled(false); RestoreAnimation();
    if (bContinue) StartAttack(NextIndex);
}

void UMeleeAttackComponent::CancelAttack()
{
    if (GetOwner()->HasAuthority()) MulticastCancelAttack(LastPlayedId);
    else ClearLocalAttack();
}

void UMeleeAttackComponent::MulticastCancelAttack_Implementation(int32 Id)
{
    if (Id == LastPlayedId) ClearLocalAttack();
}

void UMeleeAttackComponent::ClearLocalAttack()
{
    bAttacking = false; ActiveMontageInstanceId = INDEX_NONE;
    CloseHitWindow(); ClearComboWindow(); SetComponentTickEnabled(false);
    if (Character && ActiveMontage)
        if (auto* Anim = Character->GetMesh()->GetAnimInstance()) Anim->Montage_Stop(0.f, ActiveMontage);
    bAttacking = false; RestoreAnimation();
}

bool UMeleeAttackComponent::IsActiveAttackNotify(const UAnimSequenceBase* Source, int32 InstanceId) const
{
    return bAttacking && Source == ActiveMontage && InstanceId != INDEX_NONE && InstanceId == ActiveMontageInstanceId;
}

void UMeleeAttackComponent::BeginHitWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, FName Socket, FVector Extent, FVector Offset)
{
    if (!GetOwner()->HasAuthority() || !IsActiveAttackNotify(Source, InstanceId) || bHitWindow || !AttackBox || !Character->GetMesh()->DoesSocketExist(Socket)) return;
    AttackBox->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
    AttackBox->SetRelativeLocation(Offset);
    AttackBox->SetBoxExtent(Extent.GetAbs().ComponentMax(FVector(1.f)));
    PreviousBoxLocation = AttackBox->GetComponentLocation();
    HitWindowOwner = Window; bHitWindow = true;
    AttackBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CheckHitWindow();
}

void UMeleeAttackComponent::EndHitWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window)
{
    if (IsActiveAttackNotify(Source, InstanceId) && HitWindowOwner == Window) CloseHitWindow();
}

void UMeleeAttackComponent::CloseHitWindow()
{
    bHitWindow = false; HitWindowOwner = nullptr;
    if (AttackBox) AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool UMeleeAttackComponent::IsAttackCollisionEnabled() const
{
    return AttackBox && AttackBox->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
}

void UMeleeAttackComponent::BeginComboInputWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, int32 NextIndex)
{
    if (!IsActiveAttackNotify(Source, InstanceId) || bComboInputWindow || bComboTransitionPending ||
        !AttackMontages.IsValidIndex(NextIndex) || !AttackMontages[NextIndex]) return;
    ComboWindowOwner = Window; NextComboIndex = NextIndex;
    bComboInputWindow = true; bComboInputBuffered = false;
}

void UMeleeAttackComponent::EndComboInputWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, bool bReachedEnd)
{
    if (!IsActiveAttackNotify(Source, InstanceId) || ComboWindowOwner != Window) return;
    bComboInputWindow = false; ComboWindowOwner = nullptr;
    bComboTransitionPending = bReachedEnd && bComboInputBuffered && GetOwner()->HasAuthority();
    bComboInputBuffered = false;
    if (!bComboTransitionPending) NextComboIndex = INDEX_NONE;
}

void UMeleeAttackComponent::ClearComboWindow()
{
    ComboWindowOwner = nullptr; NextComboIndex = INDEX_NONE;
    bComboInputWindow = bComboInputBuffered = bComboTransitionPending = false;
}

void UMeleeAttackComponent::CheckHitWindow()
{
    if (!GetOwner()->HasAuthority() || !bHitWindow || !AttackBox) return;
    TArray<FOverlapResult> Results;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeAttack), false, GetOwner());
    const FVector Location = AttackBox->GetComponentLocation();
    const auto Shape = FCollisionShape::MakeBox(AttackBox->GetScaledBoxExtent());
    GetWorld()->OverlapMultiByObjectType(Results, Location, AttackBox->GetComponentQuat(), FCollisionObjectQueryParams(ECC_Pawn), Shape, Params);
    for (const auto& Result : Results) TryHit(Result.GetActor());
    TArray<FHitResult> SweptHits;
    GetWorld()->SweepMultiByObjectType(SweptHits, PreviousBoxLocation, Location, AttackBox->GetComponentQuat(), FCollisionObjectQueryParams(ECC_Pawn), Shape, Params);
    for (const auto& Hit : SweptHits) TryHit(Hit.GetActor());
    PreviousBoxLocation = Location;
    if (bDrawAttackBox) DrawDebugBox(GetWorld(), Location, AttackBox->GetScaledBoxExtent(), AttackBox->GetComponentQuat(), FColor::Red, false, 0.05f);
}

void UMeleeAttackComponent::TryHit(AActor* Other)
{
    if (!GetOwner()->HasAuthority() || !bHitWindow || !IsAttackCollisionEnabled() ||
        !IsValid(Other) || Other == GetOwner() || AlreadyHit.Contains(Other)) return;
    UObject* Receiver = Other->Implements<UCombatHitReceiver>() ? Other : nullptr;
    if (!Receiver)
        for (auto* Component : Other->GetComponents())
            if (Component->Implements<UCombatHitReceiver>()) { Receiver = Component; break; }
    if (!Receiver) return;
    FHitResult Wall;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MeleeOcclusion), false, GetOwner());
    Params.AddIgnoredActor(Other);
    if (GetWorld()->LineTraceSingleByChannel(Wall, Character->GetActorLocation(), Other->GetActorLocation(), ECC_Visibility, Params)) return;
    // One delivery per target per attack, including targets that reject a hit while reacting.
    AlreadyHit.Add(Other);
    FCombatHit Hit; Hit.Attacker = GetOwner(); Hit.AttackId = AttackId; Hit.ImpactPoint = AttackBox->GetComponentLocation();
    if (ICombatHitReceiver::Execute_ReceiveCombatHit(Receiver, Hit)) ++AcceptedHitCount;
}

void UMeleeAttackComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction); CheckHitWindow();
    // Process the Notify's transition after animation evaluation, outside its branching-point iteration.
    if (GetOwner()->HasAuthority() && bAttacking && bComboTransitionPending)
    {
        const int32 Index = NextComboIndex;
        bComboTransitionPending = false;
        StartAttack(Index);
    }
}

void UMeleeAttackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearLocalAttack();
    if (InputSubsystem.IsValid() && InstalledContext) InputSubsystem->RemoveMappingContext(InstalledContext);
    if (AttackBox) AttackBox->DestroyComponent();
    Super::EndPlay(Reason);
}
