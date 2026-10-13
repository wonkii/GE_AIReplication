#pragma once

#include "CoreMinimal.h"

class ASharpEnemyCharacter;
class ASharpSplinePath;

// Geometry/collision checks shared by editor preview and server preparation.
// Does not own trial state, policy state, or timers.
struct GE_AIREPLICATION_API FSharpEnemyPlacement
{
	static bool FindStart(const ASharpEnemyCharacter& Enemy, const ASharpSplinePath& Path,
		const TArray<AActor*>& IgnoreActors, FTransform& OutTransform, FString& OutReason);
	static bool Place(ASharpEnemyCharacter& Enemy, const FTransform& Transform, FString& OutReason);
};
