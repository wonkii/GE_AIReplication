#include "SharpEnemyAIController.h"

#include "SharpEnemyCharacter.h"
#include "../Movement/SharpSplineFollowerComponent.h"

USharpSplineFollowerComponent* ASharpEnemyAIController::GetFollower() const
{
	const ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(GetPawn());
	return HasAuthority() && Enemy ? Enemy->SplineFollower.Get() : nullptr;
}

bool ASharpEnemyAIController::PrepareFollow(ASharpSplinePath* Path, FString& OutReason)
{
	if (USharpSplineFollowerComponent* Follower = GetFollower()) { return Follower->Prepare(Path, OutReason); }
	OutReason = TEXT("SharpEnemyAIController must possess a server SharpEnemyCharacter");
	return false;
}

bool ASharpEnemyAIController::StartFollow(FString& OutReason)
{
	if (USharpSplineFollowerComponent* Follower = GetFollower()) { return Follower->Start(OutReason); }
	OutReason = TEXT("Server follower is missing");
	return false;
}

void ASharpEnemyAIController::SuspendFollow()
{
	if (USharpSplineFollowerComponent* Follower = GetFollower()) { Follower->Suspend(); }
}

bool ASharpEnemyAIController::ResumeFollow(FString& OutReason)
{
	if (USharpSplineFollowerComponent* Follower = GetFollower()) { return Follower->Resume(OutReason); }
	OutReason = TEXT("Server follower is missing");
	return false;
}

void ASharpEnemyAIController::ResetFollow()
{
	if (USharpSplineFollowerComponent* Follower = GetFollower()) { Follower->Reset(); }
}

void ASharpEnemyAIController::OnUnPossess()
{
	ResetFollow();
	Super::OnUnPossess();
}
