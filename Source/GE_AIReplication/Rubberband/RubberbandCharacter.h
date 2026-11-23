#pragma once
#include "CoreMinimal.h"
#include "GE_AIReplicationCharacter.h"
#include "RubberbandCharacter.generated.h"

class URubberbandInputScriptComponent;

/**
 * Player character for the rubber-banding mitigation (B) experiments.
 * Same as the template character, plus a recording movement component and a scripted input player.
 * Create a Blueprint child (or reparent a copy of BP_ThirdPersonCharacter) to use it.
 */
UCLASS(abstract)
class GE_AIREPLICATION_API ARubberbandCharacter : public AGE_AIReplicationCharacter
{
	GENERATED_BODY()
public:
	ARubberbandCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rubberband")
	TObjectPtr<URubberbandInputScriptComponent> InputScript;
};
