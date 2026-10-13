#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SharpMoveSnapshot.h"
#include "SharpVisualPredictorComponent.generated.h"

class ASharpEnemyCharacter;

/** Simulated-proxy Mesh presentation only. The capsule and CMC remain authoritative. */
UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpVisualPredictorComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpVisualPredictorComponent();
	void QueueSnapshot(const FSharpMoveSnapshot& Snapshot);
	void NotifyCorrection(const FVector& OldMeshPosition);
	void HandleSessionChanged();
	bool OwnsPresentation() const { return bOwnsPresentation; }
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Prediction") bool bPredicting = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Prediction") FString LastStopReason;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpMoveSnapshot Pending, Active;
	bool bPending = false, bOwnsPresentation = false;
	int32 Generation = 0, Segment = 0;
	ENetworkSmoothingMode OriginalSmoothing = ENetworkSmoothingMode::Exponential;
	FVector BaseRelativePosition, DisplayPosition, ConvergenceOffset, BlendStart;
	float ConvergenceElapsed = 0, BlendElapsed = 0;
	void SetOwnership(bool bEnabled);
	void StopPrediction(const FString& Reason);
	FVector BaselinePosition() const;
};
