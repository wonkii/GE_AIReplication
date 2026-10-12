#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SharpEnemyAIController.generated.h"

class ASharpSplinePath;
class USharpSplineFollowerComponent;

/** Server commands only. Path tracking belongs to the follower component. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	bool PrepareFollow(ASharpSplinePath* Path, FString& OutReason);
	bool StartFollow(FString& OutReason);
	void SuspendFollow();
	bool ResumeFollow(FString& OutReason);
	void ResetFollow();

protected:
	virtual void OnUnPossess() override;

private:
	USharpSplineFollowerComponent* GetFollower() const;
};
