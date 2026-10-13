#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpMovementReport.generated.h"

/** Editor entry point for the existing offline Python analysis, using native CSV schema. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpMovementReport : public AActor
{
	GENERATED_BODY()
public:
	ASharpMovementReport();
	UPROPERTY(EditAnywhere, Category="Sharp Report") FString PythonExecutable = TEXT("python");
	UPROPERTY(EditAnywhere, Category="Sharp Report") TArray<FString> RecordingDirectories;
	UPROPERTY(EditAnywhere, Category="Sharp Report") FString OutputDirectory;
	UPROPERTY(EditAnywhere, Category="Sharp Report") bool bSameHostWallClock = true;
	UPROPERTY(EditAnywhere, Category="Sharp Report", meta=(ClampMin="0.0")) float BudgetTolerance = 0.05f;
	UPROPERTY(EditAnywhere, Category="Sharp Report", meta=(ClampMin="0.0", ClampMax="1.0")) float MinimumCoverage = 0.8f;
	UPROPERTY(VisibleInstanceOnly, Category="Sharp Report") FString LastResult;
	UFUNCTION(CallInEditor, Category="Sharp Report") void GenerateReport();
	UFUNCTION(CallInEditor, Category="Sharp Report") void CompareReports() { GenerateReport(); }
	UFUNCTION(CallInEditor, Category="Sharp Report") void OpenReport();
};
