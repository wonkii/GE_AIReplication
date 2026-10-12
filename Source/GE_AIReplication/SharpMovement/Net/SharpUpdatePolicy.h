#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Detector/SharpMovementSample.h"
#include "../Experiment/SharpExperimentTypes.h"
#include "SharpUpdatePolicy.generated.h"

class USharpExperimentSettings;
class ASharpEnemyCharacter;

UCLASS(ClassGroup = (SharpMovement), meta = (BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpUpdatePolicyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpUpdatePolicyComponent();
	bool Start(ESharpExperimentMode InMode, const USharpExperimentSettings* Settings, FString& OutReason);
	void Stop();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	ESharpExperimentMode Mode = ESharpExperimentMode::Default;
	float OriginalFrequency = 0.0f, OriginalMinFrequency = 0.0f;
	float HighFrequency = 0.0f, BurstDuration = 0.0f;
	double BurstEnd = -1.0;
	bool bActive = false, bOwnsAdaptiveLease = false, bOriginalReplicateTimestamp = false;
	bool bAirborneModel = false;
	void OnClassified(const FSharpMovementSample& Sample, const FSharpClassification& Result);
};
