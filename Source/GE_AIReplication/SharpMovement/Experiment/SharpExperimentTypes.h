#pragma once

#include "CoreMinimal.h"
#include "../Measurement/SharpRecordingSettings.h"
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
enum class ESharpEnemyTrialState : uint8
{
	Idle, Ready, Running, Stopped, Suspended, Completed, Failed
};

// Replicate the entire execution context together. Generation distinguishes an
// individual reset from a reset of the level-wide TrialId.
USTRUCT(BlueprintType)
struct FSharpEnemyTrialStatus
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FSharpExperimentIdentity Identity;
	UPROPERTY(BlueprintReadOnly) FName PathId;
	UPROPERTY(BlueprintReadOnly) int32 PathRevision = 0;
	UPROPERTY(BlueprintReadOnly) int32 Generation = 0;
	UPROPERTY(BlueprintReadOnly) int32 Segment = 0;
	UPROPERTY(BlueprintReadOnly) ESharpEnemyTrialState State = ESharpEnemyTrialState::Idle;
	UPROPERTY(BlueprintReadOnly) double ServerStartTime = -1.0;
	UPROPERTY(BlueprintReadOnly) double SegmentStartTime = -1.0;
	UPROPERTY(BlueprintReadOnly) double ServerEndTime = -1.0;
	UPROPERTY(BlueprintReadOnly) FString FailureReason;
	// Client behavior is frozen with the same replicated execution context.
	UPROPERTY(BlueprintReadOnly) FSharpRecordingSettings Recording;
	UPROPERTY(BlueprintReadOnly) FSharpVisualPredictionSettings Prediction;
};

USTRUCT(BlueprintType)
struct FSharpExperimentSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Total = 0;
	UPROPERTY(BlueprintReadOnly) int32 Ready = 0;
	UPROPERTY(BlueprintReadOnly) int32 Running = 0;
	UPROPERTY(BlueprintReadOnly) int32 Stopped = 0;
	UPROPERTY(BlueprintReadOnly) int32 Suspended = 0;
	UPROPERTY(BlueprintReadOnly) int32 Completed = 0;
	UPROPERTY(BlueprintReadOnly) int32 Failed = 0;
};
