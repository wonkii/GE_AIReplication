#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReactionWarpPlan.h"
#include "ReactionWarpTools.generated.h"

/** Same pure planner for editor scripts, GUI diagnostics and console reports. */
UCLASS()
class GE_AIREPLICATION_API UReactionWarpTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Reaction Warp")
    static FString BindPolicy(UHitWallReactionLibrary* Library, UReactionWarpPolicy* Policy, FName CandidateId);
    UFUNCTION(BlueprintCallable, Category="Reaction Warp")
    static FString PolicyDigest(UReactionWarpPolicy* Policy);
    UFUNCTION(BlueprintCallable, Category="Reaction Warp")
    static FString Inspect(UHitWallReactionLibrary* Library, UReactionWarpPolicy* Policy, float BudgetCm,
        EReactionWarpMode Mode = EReactionWarpMode::MultiPhase, bool bAllowDraft = true);
    UFUNCTION(BlueprintCallable, Category="Reaction Warp")
    static FString RunSelfTest();
};
