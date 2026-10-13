#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Detector/SharpMovementClassifier.h"
#include "SharpMovementDetectorComponent.generated.h"

class ASharpEnemyCharacter;
class USharpExperimentSettings;

UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpMovementDetectorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	void Start(const USharpExperimentSettings& Settings);
	void Stop();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpMovementClassifier Classifier;
	FSharpDetectorConfig Config;
	uint32 Sequence = 0;
	bool bObserving = false, bAirborneModel = false;
	FSharpMovementSample Sample() const;
	UFUNCTION() void ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
};
