#include "HitReactionComponent.h"
#include "MeleeAttackComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "MotionWarpingComponent.h"
#include "ReactionPlanarWarpModifier.h"
#include "ReactionEnvironmentComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UHitReactionComponent::UHitReactionComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = false;
}

void UHitReactionComponent::BeginPlay()
{
    Super::BeginPlay(); Character = Cast<ACharacter>(GetOwner());
    if (Character && bCombatEnabled)
    {
        Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Character->GetMesh()->bEnableUpdateRateOptimizations = false;
    }
}

void UHitReactionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UHitReactionComponent, State);
}

bool UHitReactionComponent::ReceiveCombatHit_Implementation(const FCombatHit& Hit)
{
    if (!bCombatEnabled || !Character || !Character->HasAuthority() || IsReacting() || !IsValid(Hit.Attacker) ||
        Hit.Attacker == Character || Hit.Attacker->GetWorld() != GetWorld() || State.Id == MAX_int32) return false;
    auto* Anim = Character->GetMesh()->GetAnimInstance();
    if (!Anim || !Character->GetCharacterMovement()->IsMovingOnGround()) return false;
    // Resolve in the pre-hit frame on authority. Socket positions/normals can swing
    // across the victim during a melee attack; the attacker's position is stable.
    FVector ToSource = Hit.Attacker->GetActorLocation() - Character->GetActorLocation();
    if (ToSource.ContainsNaN()) return false;
    ToSource.Z = 0.f;
    if (!ToSource.Normalize())
    {
        ToSource = Hit.ImpactPoint - Character->GetActorLocation();
        ToSource.Z = 0.f;
        if (ToSource.ContainsNaN() || !ToSource.Normalize()) ToSource = Character->GetActorForwardVector().GetSafeNormal2D();
    }
    const FRotator PreHitYaw(0.f, Character->GetActorRotation().Yaw, 0.f);
    const FVector LocalSource = PreHitYaw.UnrotateVector(ToSource);
    const ECombatHitDirection Direction = FMath::Abs(LocalSource.X) >= FMath::Abs(LocalSource.Y)
        ? (LocalSource.X >= 0.f ? ECombatHitDirection::Front : ECombatHitDirection::Back)
        : (LocalSource.Y >= 0.f ? ECombatHitDirection::Right : ECombatHitDirection::Left);
    UAnimMontage* Montage = HitMontage;
    if (const auto* Variant = DirectionalHitMontages.Find(Direction); Variant && *Variant) Montage = Variant->Get();
    if (!Montage) return false;
    if (auto* Attack = Character->FindComponentByClass<UMeleeAttackComponent>()) Attack->CancelAttack();
    Character->ConsumeMovementInputVector();
    Character->GetCharacterMovement()->StopMovementImmediately();
    Character->GetCharacterMovement()->ClearAccumulatedForces();
    State.Id++; State.bActive = true; State.Montage = Montage; State.ServerStartTime = GetWorld()->GetTimeSeconds();
    State.Direction = Direction; State.FacingYaw = ToSource.Rotation().Yaw;
    State.WarpTarget = {};
    if (const auto* Warp = Character->FindComponentByClass<UReactionWarpComponent>())
    {
        State.WarpTarget = Warp->Plan(*Character, *Montage, State.FacingYaw, State.Id);
    }
    Character->FlushNetDormancy();
    MulticastState(State);
    if (!bLocalReacting) { CancelReaction(); return false; }
    const int32 Id = State.Id;
    GetWorld()->GetTimerManager().SetTimer(SafetyTimer, FTimerDelegate::CreateWeakLambda(this, [this, Id]()
    { if (State.Id == Id && State.bActive) CancelReaction(); }), Montage->GetPlayLength() + 0.5f, false);
    Character->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("[Combat] Hit Target=%s Attacker=%s AttackId=%d ReactionId=%d Direction=%d FacingYaw=%.2f"), *Character->GetName(), *Hit.Attacker->GetName(), Hit.AttackId, State.Id, int32(Direction), State.FacingYaw);
    return true;
}

void UHitReactionComponent::MulticastState_Implementation(FCombatReactionState NewState)
{
    if (NewState.Id < State.Id) return;
    State = NewState; ApplyState();
}

void UHitReactionComponent::OnRep_State() { ApplyState(); }

void UHitReactionComponent::ApplyState()
{
    if (!Character) Character = Cast<ACharacter>(GetOwner());
    if (!Character) return;
    if (!State.bActive) { FinishLocal(); return; }
    if (State.Id <= LastPlayedId || !State.Montage) return;
    FinishLocal();
    LastPlayedId = State.Id;
    auto* Anim = Character->GetMesh()->GetAnimInstance();
    if (!Anim) return;
    const auto* GameState = GetWorld()->GetGameState();
    const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const float Age = Character->HasAuthority() ? 0.f : FMath::Max(0.f, float(Now - State.ServerStartTime));
    // Expired replicated state must not replay an old reaction on relevancy/late join.
    if (Age >= State.Montage->GetPlayLength()) return;
    auto* Movement = Character->GetCharacterMovement();
    bPreviousOrientToMovement = Movement->bOrientRotationToMovement;
    bPreviousDesiredRotation = Movement->bUseControllerDesiredRotation;
    bPreviousControllerYaw = Character->bUseControllerRotationYaw;
    PreviousRootMotionMode = uint8(Anim->RootMotionMode);
    Movement->bOrientRotationToMovement = false; Movement->bUseControllerDesiredRotation = false;
    Character->bUseControllerRotationYaw = false;
    Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
    ActiveMontage = State.Montage; bLocalReacting = true;
    OnReactionChanged.Broadcast(true, State.Id);
    // Apply once per reaction on every peer, after presentation adapters release
    // the mesh. CMC owns translation, root-motion replication and later corrections;
    // do not teleport to an old start position or lock yaw every client tick.
    Character->SetActorRotation(FRotator(0.f, State.FacingYaw, 0.f));
    if (!State.WarpTarget.Name.IsNone())
    {
        if (auto* Warping = Character->FindComponentByClass<UMotionWarpingComponent>())
        {
            // Leave the authored Skew Warp without a target. Scale owns the full montage.
            const double Scale = State.WarpTarget.PlanarScale;
            auto* Modifier = NewObject<UReactionPlanarWarpModifier>(Warping);
            Modifier->Animation = ActiveMontage;
            Modifier->EndTime = ActiveMontage->GetPlayLength();
            Modifier->Scale = FVector(Scale, Scale, 1.);
            Warping->AddModifier(Modifier);
            ActiveWarpModifier = Modifier;
        }
    }
    if (Anim->Montage_Play(ActiveMontage, 1.f, EMontagePlayReturnType::MontageLength, Age) <= 0.f)
    { FinishLocal(); return; }
    const auto* Environment = Character->FindComponentByClass<UReactionEnvironmentComponent>();
    if (ActiveWarpModifier.IsValid() && Environment && Environment->bDrawDebug)
    {
        TArray<FMotionWarpingWindowData> Windows;
        UMotionWarpingUtilities::GetMotionWarpingWindowsFromAnimation(ActiveMontage, Windows);
        const auto* Instance = Anim->GetActiveInstanceForMontage(ActiveMontage);
        if (Instance && Windows.Num() == 1 && Age < Windows[0].EndTime)
        {
            DebugWarpWindowEnd = Windows[0].EndTime;
            DebugWarpMontageInstanceId = Instance->GetInstanceID();
            Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::SampleWarpMovement);
        }
        UE_LOG(LogTemp, Display, TEXT("[ReactionWarpSample] Actor=%s Id=%d Role=%d Stage=Start Age=%.3f Capsule=%s Target=%s"),
            *Character->GetName(), State.Id, int32(Character->GetLocalRole()), Age,
            *Character->GetActorLocation().ToString(), *State.WarpTarget.Location.ToString());
    }
    FOnMontageEnded Delegate;
    Delegate.BindUObject(this, &ThisClass::ReactionEnded, State.Id);
    Anim->Montage_SetEndDelegate(Delegate, ActiveMontage);
    UE_LOG(LogTemp, Display, TEXT("[Combat] ReactionStart Target=%s Id=%d Role=%d Age=%.3f"), *Character->GetName(), State.Id, int32(Character->GetLocalRole()), Age);
}

void UHitReactionComponent::SampleWarpMovement(float, FVector, FVector)
{
    if (DebugWarpWindowEnd <= 0.f) return;
    auto* Anim = Character->GetMesh()->GetAnimInstance();
    // Blend-out can remove the montage from the active map while it still contributes root motion.
    const auto* Instance = Anim ? Anim->GetMontageInstanceForID(DebugWarpMontageInstanceId) : nullptr;
    if (!Instance || Instance->GetPosition() < DebugWarpWindowEnd) return;

    UE_LOG(LogTemp, Display, TEXT("[ReactionWarpSample] Actor=%s Id=%d Role=%d Stage=WindowCrossing Position=%.3f WindowEnd=%.3f Weight=%.3f Capsule=%s"),
        *Character->GetName(), LastPlayedId, int32(Character->GetLocalRole()), Instance->GetPosition(),
        DebugWarpWindowEnd, Instance->GetWeight(), *Character->GetActorLocation().ToString());
    DebugWarpWindowEnd = 0.f;
}

void UHitReactionComponent::ReactionEnded(UAnimMontage*, bool bInterrupted, int32 Id)
{
    if (Id != State.Id) return;
    if (const auto* Environment = Character ? Character->FindComponentByClass<UReactionEnvironmentComponent>() : nullptr;
        Environment && Environment->bDrawDebug && ActiveWarpModifier.IsValid())
    {
        UE_LOG(LogTemp, Display, TEXT("[ReactionWarpSample] Actor=%s Id=%d Role=%d Stage=MontageEnded Interrupted=%d Capsule=%s"),
            *Character->GetName(), Id, int32(Character->GetLocalRole()), bInterrupted,
            *Character->GetActorLocation().ToString());
    }
    if (!bInterrupted && ActiveWarpModifier.IsValid())
    {
        const auto EndingModifier = ActiveWarpModifier;
        // Montage delegates can run before character movement consumes the last extracted delta.
        GetWorld()->GetTimerManager().SetTimerForNextTick([EndingModifier]()
        {
            if (auto* Modifier = EndingModifier.Get())
                Modifier->SetState(ERootMotionModifierState::MarkedForRemoval);
        });
        ActiveWarpModifier.Reset();
    }
    if (Character && Character->HasAuthority()) CancelReaction();
    else FinishLocal();
}

void UHitReactionComponent::FinishLocal()
{
    if (!bLocalReacting || !Character) return;
    bLocalReacting = false;
    if (DebugWarpMontageInstanceId != INDEX_NONE)
        Character->OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::SampleWarpMovement);
    DebugWarpWindowEnd = 0.f;
    DebugWarpMontageInstanceId = INDEX_NONE;
    if (auto* Modifier = ActiveWarpModifier.Get())
        Modifier->SetState(ERootMotionModifierState::MarkedForRemoval);
    ActiveWarpModifier.Reset();
    auto* Montage = ActiveMontage.Get(); ActiveMontage = nullptr;
    if (auto* Anim = Character->GetMesh()->GetAnimInstance())
    {
        if (Montage && Anim->Montage_IsPlaying(Montage)) Anim->Montage_Stop(0.f, Montage);
        Anim->SetRootMotionMode(ERootMotionMode::Type(PreviousRootMotionMode));
    }
    auto* Movement = Character->GetCharacterMovement();
    Movement->bOrientRotationToMovement = bPreviousOrientToMovement;
    Movement->bUseControllerDesiredRotation = bPreviousDesiredRotation;
    Character->bUseControllerRotationYaw = bPreviousControllerYaw;
    OnReactionChanged.Broadcast(false, LastPlayedId);
    UE_LOG(LogTemp, Display, TEXT("[Combat] ReactionEnd Target=%s Id=%d Role=%d"), *Character->GetName(), LastPlayedId, int32(Character->GetLocalRole()));
}

void UHitReactionComponent::CancelReaction()
{
    if (!GetOwner()->HasAuthority()) return;
    GetWorld()->GetTimerManager().ClearTimer(SafetyTimer);
    if (!State.bActive && !bLocalReacting) return;
    State.bActive = false; MulticastState(State); GetOwner()->ForceNetUpdate();
}

void UHitReactionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(SafetyTimer);
    State.bActive = false; FinishLocal(); Super::EndPlay(Reason);
}
