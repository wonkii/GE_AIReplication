#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JointMotionExtractor.generated.h"

/** Read-only source pose extraction. Files and task lifetimes belong to the Python driver. */
UCLASS()
class GEANIMATIONANALYSISEDITOR_API UJointMotionExtractor : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** JSON contains an error field on failure. Montage support is deliberately fail-closed. */
    UFUNCTION(BlueprintCallable, Category="Animation Analysis")
    static FString Extract(const FString& AssetPath, const FString& MeshPath,
        const FString& Section, double SampleRate = 60., double PlayRate = 1.);
};
