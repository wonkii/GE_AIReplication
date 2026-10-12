#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Detector/SharpMovementClassifier.h"
#include "SharpMovementDetectorComponent.generated.h"

class ASharpEnemyCharacter;
DECLARE_MULTICAST_DELEGATE_TwoParams(FSharpMovementClassified, const FSharpMovementSample&, const FSharpClassification&);

UCLASS(ClassGroup = (SharpMovement), meta = (BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpMovementDetectorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	void Start(const FSharpDetectorConfig& InConfig);
	void Stop();
	FSharpMovementClassified OnClassified;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Detector") bool bRisk = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Detector") int64 DetectionCount = 0;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpMovementClassifier Classifier;
	FSharpDetectorConfig Config;
	bool bActive = false;
	UFUNCTION() void ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
};
