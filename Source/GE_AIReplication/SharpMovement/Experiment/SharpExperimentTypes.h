#pragma once

#include "CoreMinimal.h"
#include "../Measurement/SharpMovementRecordTypes.h"
#include "../Prediction/SharpVisualPredictionSettings.h"
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

UENUM(BlueprintType)
enum class ESharpTrialState : uint8
{
	Idle,
	Ready,
	Running,
	Suspended,
	Completed,
	Failed
};

UENUM(BlueprintType)
enum class ESharpBaselineProfile : uint8
{
	EngineDefault,
	ControlledLowRate
};

USTRUCT(BlueprintType)
struct FSharpTrialStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	FSharpExperimentIdentity Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	ESharpTrialState State = ESharpTrialState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	double ServerStartTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	FString FailureReason;
};

// Client diagnostics wait for this complete metadata value, not actor/manager replication order.
USTRUCT(BlueprintType)
struct FSharpEnemyExperimentContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	FSharpExperimentIdentity Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	FName PathId;

	UPROPERTY(BlueprintReadOnly, Category = "Sharp Experiment")
	int32 PathRevision = 0;
};

USTRUCT(BlueprintType)
struct FSharpExperimentClientState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FSharpEnemyExperimentContext Context;
	UPROPERTY(BlueprintReadOnly) ESharpTrialState State = ESharpTrialState::Idle;
	UPROPERTY(BlueprintReadOnly) FSharpRecordingSettings Recording;
	UPROPERTY(BlueprintReadOnly) FSharpVisualPredictionSettings Prediction;
	UPROPERTY(BlueprintReadOnly) double ServerStartTime = 0.0;
	UPROPERTY(BlueprintReadOnly) double ServerEndTime = 0.0;
	UPROPERTY(BlueprintReadOnly) bool bHasStarted = false;
};
