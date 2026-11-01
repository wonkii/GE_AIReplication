#pragma once
#include "CoreMinimal.h"
#include "GE_AIReplicationCharacter.h"
#include "HitWallPlayerCharacter.generated.h"

class UBoxComponent;
class UAnimMontage;
class UInputAction;

UCLASS()
class GE_AIREPLICATION_API AHitWallPlayerCharacter : public AGE_AIReplicationCharacter
{
    GENERATED_BODY()
public:
    AHitWallPlayerCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void DoMove(float Right, float Forward) override;
    virtual void DoJumpStart() override;
    void BeginHitWindow(FName Socket, FVector Extent, FVector Offset);
    void EndHitWindow();
    void CheckHitWindow();
    UFUNCTION(Exec) void HitWallReset();
protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool CanJumpInternal_Implementation() const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UBoxComponent> AttackBox;
    UPROPERTY(EditDefaultsOnly, Category="Attack") TArray<TObjectPtr<UAnimMontage>> AttackMontages;
    UPROPERTY(EditDefaultsOnly, Category="Attack") TObjectPtr<UInputAction> AttackAction;
    UPROPERTY(EditAnywhere, Category="Attack") bool bDrawAttackBox = true;
private:
    void AttackPressed();
    UFUNCTION(Server, Reliable) void ServerRequestAttack();
    UFUNCTION(Server, Reliable) void ServerResetTargets();
    UFUNCTION(NetMulticast, Reliable) void MulticastAttack(int32 Index, int32 Id);
    void StartNextAttack();
    void AttackEnded(UAnimMontage* Montage, bool bInterrupted);
    UFUNCTION() void OnAttackOverlap(UPrimitiveComponent* Overlapped, AActor* Other,
        UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);
    void TryHit(AActor* Other);
    int32 NextAttack = 0;
    int32 AttackId = 0;
    int32 LastPlayedId = 0;
    int32 QueuedAttacks = 0;
    bool bAttacking = false;
    bool bHitWindow = false;
    FVector PreviousBoxLocation = FVector::ZeroVector;
    double LastRequestTime = -1.0;
    TSet<TWeakObjectPtr<AActor>> AlreadyHit;
    FTransform InitialTransform;
    TArray<FTimerHandle> SmokeTimers;
};
