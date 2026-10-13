#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "SharpEnemyAIController.generated.h"

// Possession only. Steering and experiment control live in Enemy components.
UCLASS()
class GE_AIREPLICATION_API ASharpEnemyAIController : public AAIController
{
	GENERATED_BODY()
public:
	ASharpEnemyAIController();
protected:
	virtual void OnUnPossess() override;
};
