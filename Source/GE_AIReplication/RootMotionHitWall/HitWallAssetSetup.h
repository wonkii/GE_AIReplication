#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HitWallAssetSetup.generated.h"

UCLASS()
class GE_AIREPLICATION_API UHitWallAssetSetup : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Editor-only implementation; updates owned animation assets, preserves source sequences/montages.
    UFUNCTION(BlueprintCallable, Category="HitWall|Editor") static bool PrepareAnimationAssets();
};
