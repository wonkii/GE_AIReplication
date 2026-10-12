#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpImpulseTypes.h"
#include "SharpImpulseVolume.generated.h"

class UBoxComponent;
class ASharpEnemyCharacter;
class ASharpExperimentManager;
class ASharpSplinePath;

UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpImpulseVolume : public AActor
{
	GENERATED_BODY()
public:
	ASharpImpulseVolume();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Impulse")
	TObjectPtr<UBoxComponent> TriggerBox;
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Sharp Impulse")
	FName VolumeId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Impulse")
	FSharpImpulseSettings Settings;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Impulse|Runtime")
	bool bArmed = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Impulse|Runtime")
	int32 TriggerCount = 0;

	bool Prepare(ASharpExperimentManager* InManager, FString& OutReason);
	bool ValidateConfiguration(FString& OutReason) const;
	bool ContainsTargetCapsule() const;
	bool IsPreparedGeometryUnchanged() const;
	void Arm();
	void Disarm();
	const FSharpImpulseSettings& GetPreparedSettings() const { return PreparedSettings; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<ASharpExperimentManager> Manager;
	UPROPERTY(Transient)
	TObjectPtr<ASharpEnemyCharacter> TargetEnemy;
	UPROPERTY(Transient)
	TObjectPtr<ASharpSplinePath> Path;
	FSharpImpulseSettings PreparedSettings;
	FName PreparedVolumeId;
	FQuat PreparedRotation = FQuat::Identity;
	FTransform PreparedTransform;
	FVector PreparedBoxExtent = FVector::ZeroVector;
	int32 PreparedTrialId = 0;
	bool bInside = false;
	double LastTriggerTime = -1.0;
	FVector ResolveImpulse() const;
	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
