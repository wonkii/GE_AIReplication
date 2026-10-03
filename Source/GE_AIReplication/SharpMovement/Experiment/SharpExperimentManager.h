#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpExperimentTypes.h"
#include "SharpExperimentManager.generated.h"

class USharpExperimentSettings;

UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpExperimentManager : public AActor
{
	GENERATED_BODY()

public:
	ASharpExperimentManager();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment")
	TObjectPtr<USharpExperimentSettings> Settings;

	// An empty RunId generates a new value on the server. Supply one to replay a run.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup")
	FString ConfiguredRunId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup", meta = (ClampMin = "1"))
	int32 ConfiguredTrialId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup")
	ESharpExperimentMode ConfiguredMode = ESharpExperimentMode::Default;

	UPROPERTY(ReplicatedUsing = OnRep_Identity, BlueprintReadOnly, Category = "Sharp Experiment")
	FSharpExperimentIdentity Identity;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Identity();

private:
	void LogStartup() const;
};

