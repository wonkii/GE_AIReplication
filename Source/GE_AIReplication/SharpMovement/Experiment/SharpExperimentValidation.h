#pragma once
#include "CoreMinimal.h"

class ASharpExperimentManager;
class ASharpSplinePath;
class ASharpEnemyCharacter;

// Read-only preflight; never partially starts or repositions a group.
struct GE_AIREPLICATION_API FSharpExperimentValidation
{
	static bool ValidateRegistry(const ASharpExperimentManager& Manager, FString& OutReason);
	static bool ValidateRandomTarget(const ASharpExperimentManager& Manager, const ASharpEnemyCharacter* Enemy, FString& OutReason);
	static bool ValidateRandomReadiness(const ASharpEnemyCharacter* Enemy, FString& OutReason);
	static bool ValidateTarget(const ASharpExperimentManager& Manager, const ASharpSplinePath* Path, FString& OutReason);
	static bool ValidateTargetReadiness(const ASharpSplinePath* Path, FString& OutReason);
	static bool ValidateReadiness(const ASharpExperimentManager& Manager, FString& OutReason);
	static bool BuildStartTransforms(const ASharpExperimentManager& Manager,
		const TArray<ASharpSplinePath*>& Targets, TArray<FTransform>& OutTransforms, FString& OutReason);
	// An aligned set of enemies and optional Spline paths (nullptr for RandomWalk).
	static bool BuildGroupStartTransforms(const ASharpExperimentManager& Manager,
		const TArray<ASharpEnemyCharacter*>& Enemies, const TArray<ASharpSplinePath*>& Paths,
		TArray<FTransform>& OutTransforms, FString& OutReason);
};
