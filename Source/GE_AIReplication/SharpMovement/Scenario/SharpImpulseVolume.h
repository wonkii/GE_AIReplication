#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpImpulseTypes.h"
#include "SharpImpulseVolume.generated.h"

class UBoxComponent;
class ASharpEnemyCharacter;
class ASharpExperimentManager;

/** Server overlap scenario. Each participating Enemy has independent trigger state. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpImpulseVolume : public AActor
{
	GENERATED_BODY()
public:
	ASharpImpulseVolume();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Impulse") TObjectPtr<UBoxComponent> TriggerBox;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Impulse") FName VolumeId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Impulse") FSharpImpulseSettings Settings;
	// Optional filter; a null Manager allows any valid registered session in this world.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Impulse") TObjectPtr<ASharpExperimentManager> Manager;
	bool AppliesTo(const ASharpExperimentManager* SessionManager) const;
	bool ValidateStart(const ASharpEnemyCharacter& Enemy, const FTransform& Start, FString& OutReason) const;
	bool PrepareEnemy(ASharpEnemyCharacter& Enemy, const FTransform& Start, FString& OutReason);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	struct FTriggerState
	{
		int32 Generation = 0; bool bTriggered = false, bInside = false; double LastTrigger = -1;
		FName PreparedId; FTransform Transform; FVector Extent; FSharpImpulseSettings Config;
	};
	TMap<TWeakObjectPtr<ASharpEnemyCharacter>, FTriggerState> States;
	UFUNCTION() void OnBeginOverlap(UPrimitiveComponent* Component, AActor* Actor, UPrimitiveComponent* OtherComponent,
		int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	UFUNCTION() void OnEndOverlap(UPrimitiveComponent* Component, AActor* Actor, UPrimitiveComponent* OtherComponent, int32 BodyIndex);
};
