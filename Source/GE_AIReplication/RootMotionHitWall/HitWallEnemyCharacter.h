#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HitWallEnemyCharacter.generated.h"
class UAnimMontage;
UCLASS()
class GE_AIREPLICATION_API AHitWallEnemyCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AHitWallEnemyCharacter();
    bool ReceiveAttack(AActor* Attacker, int32 AttackId);
    void ResetReaction();
    int32 GetReactionCount() const { return LastPlayedId; }
protected:
    virtual void BeginPlay() override;
    UPROPERTY(EditDefaultsOnly, Category="Hit") TObjectPtr<UAnimMontage> HitMontage;
    UPROPERTY(EditAnywhere, Category="Hit") float FrontHalfAngle = 60.f;
private:
    UFUNCTION(NetMulticast, Reliable) void MulticastReaction(int32 Id);
    void ReactionEnded(UAnimMontage*, bool);
    bool bReacting = false;
    int32 ReactionId = 0;
    int32 LastPlayedId = 0;
    FTransform InitialTransform;
};
