#pragma once
#include "CoreMinimal.h"
#include "ReactionWarpPlan.h"

namespace ReactionWarpArtifacts
{
    GE_AIREPLICATION_API FString NewTask(const FString& Command);
    GE_AIREPLICATION_API void SavePlan(const FString& Directory, const FString& Name, const FReactionWarpPlan& Plan);
}
