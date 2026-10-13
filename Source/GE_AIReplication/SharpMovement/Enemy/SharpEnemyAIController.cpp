#include "SharpEnemyAIController.h"
#include "SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"

ASharpEnemyAIController::ASharpEnemyAIController()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ASharpEnemyAIController::OnUnPossess()
{
	if (ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetPawn()))
	{
		Enemy->Experiment->Fail(TEXT("Server AIController possession was lost; reset after possessing again"));
	}
	Super::OnUnPossess();
}
