#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "SharpMovementMetrics.generated.h"

USTRUCT(BlueprintType)
struct FSharpMetricsSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (ClampMin = "0.001", Units = "s")) float MaximumInterpolationGap = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0.001", Units = "s")) float EventWindowSeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection", meta = (ClampMin = "0.001", Units = "s")) float DetectorMatchSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (ClampMin = "0.0", Units = "s")) float TimestampToleranceSeconds = 0.001f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.0", Units = "cm")) float RecoveryError = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery", meta = (ClampMin = "0.0", Units = "s")) float RecoveryHoldSeconds = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comparison", meta = (ClampMin = "0.0", ClampMax = "100.0")) float BudgetTolerancePercent = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comparison", meta = (ClampMin = "0.0", ClampMax = "1.0")) float MinimumComparisonCoverage = 0.8f;
	bool IsValid() const;
};

/** Reads completed recordings offline; never runs tests or changes an experiment. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpMovementReport : public AActor
{
	GENERATED_BODY()
public:
	ASharpMovementReport();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Input") FDirectoryPath ServerRecordingDirectory;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Input") FDirectoryPath ClientRecordingDirectory;
	// Optional normalized Networking Insights export: RunId,TrialId,ActorId,ActorBytesPerSecond,SnapshotBytesPerSecond,RpcBytesPerSecond.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Input") FFilePath ActorCostCsv;
	// Empty: unique report folder under Saved/SharpMovementReports.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Output") FDirectoryPath ReportOutputDirectory;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Output") bool bWriteHtmlCharts = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Settings") FSharpMetricsSettings Metrics;
	// Folders containing report.json from previous runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Report|Comparison") TArray<FDirectoryPath> ComparisonReports;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Report|Runtime") FString LastOutputDirectory;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Report|Runtime") FString LastError;
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Report") void GenerateReport();
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Report") void CompareReports();
};
