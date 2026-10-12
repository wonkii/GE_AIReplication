#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SharpMoveSnapshot.h"
#include "SharpVisualPredictionSettings.h"
#include "SharpVisualPredictorComponent.generated.h"

class ASharpEnemyCharacter;

UCLASS(ClassGroup = (SharpMovement), meta = (BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpVisualPredictorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpVisualPredictorComponent();
	void QueueSnapshot(const FSharpMoveSnapshot& Snapshot);
	void NotifyCorrection(const FVector& OldMeshPosition);
	bool OwnsPresentation() const { return bOwnsPresentation; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Prediction") bool bPredicting = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Prediction") FString LastStopReason;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpMoveSnapshot Pending, Active;
	FSharpVisualPredictionSettings Settings;
	bool bPending = false, bOwnsPresentation = false, bHasSequence = false;
	uint32 LastSequence = 0;
	uint64 AcceptedMovementGeneration = 0;
	FString RunId;
	int32 TrialId = 0;
	ENetworkSmoothingMode OriginalSmoothing = ENetworkSmoothingMode::Exponential;
	FVector BaseRelativePosition = FVector::ZeroVector;
	FVector DisplayPosition = FVector::ZeroVector, ConvergenceOffset = FVector::ZeroVector, BlendStart = FVector::ZeroVector;
	float ConvergenceElapsed = 0.0f, BlendElapsed = 0.0f;
	void SetOwnership(bool bEnabled);
	FVector BaselinePosition() const;
	void StopPrediction(const FString& Reason);
};
