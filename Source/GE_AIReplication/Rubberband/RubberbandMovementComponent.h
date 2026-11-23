#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RubberbandMovementComponent.generated.h"

/**
 * Character movement for the rubber-banding mitigation (B) experiments.
 * Keeps engine behaviour and reports every simulated-proxy correction to the world recorder.
 */
UCLASS()
class GE_AIREPLICATION_API URubberbandMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;
};
