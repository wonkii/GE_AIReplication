#pragma once
#include "CoreMinimal.h"

class ASharpExperimentManager;
class ASharpSplinePath;

// Read-only preflight; never partially starts or repositions a group.
struct GE_AIREPLICATION_API FSharpExperimentValidation
{
	static bool ValidateRegistry(const ASharpExperimentManager& Manager, FString& OutReason);
	static bool ValidateTarget(const ASharpExperimentManager& Manager, const ASharpSplinePath* Path, FString& OutReason);
	static bool ValidateTargetReadiness(const ASharpSplinePath* Path, FString& OutReason);
	static bool ValidateReadiness(const ASharpExperimentManager& Manager, FString& OutReason);
	static bool BuildStartTransforms(const ASharpExperimentManager& Manager,
		const TArray<ASharpSplinePath*>& Targets, TArray<FTransform>& OutTransforms, FString& OutReason);
};
