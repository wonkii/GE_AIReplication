#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpRecordingFiles.h"
#include "SharpConnectionRecorderComponent.generated.h"

class ASharpExperimentManager;

/** One recorder per Manager/world. Connection totals must never be added per Enemy. */
UCLASS()
class GE_AIREPLICATION_API USharpConnectionRecorderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpConnectionRecorderComponent();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpExperimentManager> Manager;
	FSharpRecordingFiles Files;
	FString RunId; int32 TrialId = 0, Part = 0;
	bool bRecording = false;
	double LastSample = -1, FinishAt = -1;
	float SampleSeconds = 0.25f, TailSeconds = 0.5f;
	void Sample();
};
