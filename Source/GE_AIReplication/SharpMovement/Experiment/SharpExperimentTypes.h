#pragma once

#include "CoreMinimal.h"
#include "SharpExperimentTypes.generated.h"

UENUM(BlueprintType)
enum class ESharpExperimentMode : uint8
{
	Default,
	Adaptive,
	AlwaysHigh,
	SingleShot,
	Burst,
	EventOnly,
	EventPredictor
};

// One replicated value keeps the identifiers and selected mode together.
USTRUCT(BlueprintType)
struct FSharpExperimentIdentity
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	FString RunId;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	int32 TrialId = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	ESharpExperimentMode Mode = ESharpExperimentMode::Default;
};
