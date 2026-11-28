#pragma once
#include "CoreMinimal.h"
#include "GE_AIReplicationCharacter.h"
#include "LocalRubberbandCharacter.generated.h"
class ULocalRubberbandInputScriptComponent;
UCLASS(abstract)
class GE_AIREPLICATION_API ALocalRubberbandCharacter : public AGE_AIReplicationCharacter
{
    GENERATED_BODY()
public:
    ALocalRubberbandCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LocalRubberband")
    TObjectPtr<ULocalRubberbandInputScriptComponent> InputScript;
};
