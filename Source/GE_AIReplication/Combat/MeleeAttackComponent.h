#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeAttackComponent.generated.h"

class ACharacter;
class UAnimMontage;
class UBoxComponent;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UAnimSequenceBase;
class UAnimNotifyState;

/** Montage Notify-controlled hit/one-input combo windows; independent of any Character subclass. */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API UMeleeAttackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMeleeAttackComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") bool bCombatEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") TArray<TObjectPtr<UAnimMontage>> AttackMontages;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Input") TObjectPtr<UInputAction> AttackAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Input") TObjectPtr<UInputMappingContext> AttackMappingContext;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Input") int32 MappingPriority = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") bool bDrawAttackBox = false;
    UFUNCTION(BlueprintCallable, Category="Combat") void RequestAttack();
    UFUNCTION(BlueprintPure, Category="Combat") bool IsAttacking() const { return bAttacking; }
    void BindInput(UEnhancedInputComponent* Input);
    void BeginHitWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, FName Socket, FVector Extent, FVector Offset);
    void EndHitWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window);
    void BeginComboInputWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, int32 NextIndex);
    void EndComboInputWindow(const UAnimSequenceBase* Source, int32 InstanceId, const UAnimNotifyState* Window, bool bReachedEnd);
    UFUNCTION(BlueprintPure, Category="Combat") bool IsComboInputWindowOpen() const { return bComboInputWindow; }
    UFUNCTION(BlueprintPure, Category="Combat") bool HasBufferedComboInput() const { return bComboInputBuffered; }
    bool IsAttackCollisionEnabled() const;
    void CheckHitWindow();
    void CancelAttack();
    int32 GetPlayedAttackId() const { return LastPlayedId; }
    int32 GetAcceptedHitCount() const { return AcceptedHitCount; }
    bool IsHitWindowOpen() const { return bHitWindow; }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
    UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
    UPROPERTY(Transient) TObjectPtr<UBoxComponent> AttackBox;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> InstalledContext;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveMontage;
    UFUNCTION(Server, Reliable) void ServerRequestAttack(int32 ExpectedAttackId);
    UFUNCTION(NetMulticast, Reliable) void MulticastAttack(int32 Index, int32 Id);
    UFUNCTION(NetMulticast, Reliable) void MulticastCancelAttack(int32 Id);
    void StartAttack(int32 Index);
    void AttackEnded(UAnimMontage* Montage, bool bInterrupted, int32 Id);
    void RestoreAnimation();
    void TryHit(AActor* Other);
    bool IsActiveAttackNotify(const UAnimSequenceBase* Source, int32 InstanceId) const;
    void CloseHitWindow();
    void ClearComboWindow();
    void ClearLocalAttack();
    int32 AttackId = 0, LastPlayedId = 0, AcceptedHitCount = 0;
    int32 ActiveMontageInstanceId = INDEX_NONE, NextComboIndex = INDEX_NONE;
    const UAnimNotifyState* HitWindowOwner = nullptr;
    const UAnimNotifyState* ComboWindowOwner = nullptr;
    bool bComboInputWindow = false, bComboInputBuffered = false, bComboTransitionPending = false;
    bool bAttacking = false, bHitWindow = false;
    FVector PreviousBoxLocation = FVector::ZeroVector;
    double LastRequestTime = -1;
    TSet<TWeakObjectPtr<AActor>> AlreadyHit;
};
