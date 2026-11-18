#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CombatAssetSetup.generated.h"

UCLASS()
class GE_AIREPLICATION_API UCombatAssetSetup : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Creates missing shared assets; never edits source locomotion graphs or animation sequences. */
    UFUNCTION(BlueprintCallable, Category="Combat|Editor") static bool PrepareAnimationAssets();
    /** Adds missing hit/input windows to existing attack Montages, preserving hand-edited windows. */
    UFUNCTION(BlueprintCallable, Category="Combat|Editor") static bool EnsureAttackNotifyWindows();
    UFUNCTION(BlueprintCallable, Category="Combat|Editor") static FString DescribeAttackRootMotion();
};
