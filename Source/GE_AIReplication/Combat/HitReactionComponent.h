#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatHitReceiver.h"
#include "ReactionWarpComponent.h"
#include "HitReactionComponent.generated.h"

class ACharacter;
class UAnimMontage;
class URootMotionModifier;

/** Side of the attacker relative to the victim BEFORE the reaction turns it. */
UENUM(BlueprintType)
enum class ECombatHitDirection : uint8
{
    Front,
    Back,
    Left,
    Right
};

USTRUCT()
struct FCombatReactionState
{
    GENERATED_BODY()
    UPROPERTY() int32 Id = 0;
    UPROPERTY() bool bActive = false;
    UPROPERTY() TObjectPtr<UAnimMontage> Montage = nullptr;
    UPROPERTY() double ServerStartTime = 0;
    UPROPERTY() ECombatHitDirection Direction = ECombatHitDirection::Front;
    // World yaw is resolved once on authority, never from a client's attacker transform.
    UPROPERTY() float FacingYaw = 0.f;
    UPROPERTY() FReactionWarpTarget WarpTarget;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombatReactionChanged, bool, bActive, int32, ReactionId);

/** Montage/CMC reaction, with hooks for project-specific steering and presentation adapters. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API UHitReactionComponent : public UActorComponent, public ICombatHitReceiver
{
    GENERATED_BODY()
public:
    UHitReactionComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") bool bCombatEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<UAnimMontage> HitMontage;
    // Optional variants must also retreat in local -X after facing the attack source.
    // An unset entry falls back to HitMontage, preserving existing BP configuration.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") TMap<ECombatHitDirection, TObjectPtr<UAnimMontage>> DirectionalHitMontages;
    UPROPERTY(BlueprintAssignable, Category="Combat") FCombatReactionChanged OnReactionChanged;
    UFUNCTION(BlueprintPure, Category="Combat") bool IsReacting() const { return State.bActive || bLocalReacting; }
    UFUNCTION(BlueprintCallable, Category="Combat") void CancelReaction();
    virtual bool ReceiveCombatHit_Implementation(const FCombatHit& Hit) override;
    int32 GetReactionId() const { return State.Id; }
    int32 GetPlayedReactionId() const { return LastPlayedId; }
    UFUNCTION(BlueprintPure, Category="Combat") ECombatHitDirection GetHitDirection() const { return State.Direction; }
    UFUNCTION(BlueprintPure, Category="Combat") float GetReactionFacingYaw() const { return State.FacingYaw; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
    UPROPERTY(ReplicatedUsing=OnRep_State) FCombatReactionState State;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveMontage;
    TWeakObjectPtr<URootMotionModifier> ActiveWarpModifier;
    float DebugWarpWindowEnd = 0.f;
    int32 DebugWarpMontageInstanceId = INDEX_NONE;
    UFUNCTION() void SampleWarpMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
    UFUNCTION() void OnRep_State();
    UFUNCTION(NetMulticast, Reliable) void MulticastState(FCombatReactionState NewState);
    void ApplyState();
    void FinishLocal();
    void ReactionEnded(UAnimMontage* Montage, bool bInterrupted, int32 Id);
    bool bLocalReacting = false;
    bool bPreviousOrientToMovement = false, bPreviousControllerYaw = false, bPreviousDesiredRotation = false;
    uint8 PreviousRootMotionMode = 0;
    int32 LastPlayedId = 0;
    FTimerHandle SafetyTimer;
};
