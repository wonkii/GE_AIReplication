#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SharpCharacterMovementComponent.generated.h"

UCLASS()
class GE_AIREPLICATION_API USharpCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void ApplyAccumulatedForces(float DeltaSeconds) override;
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;
	virtual void SmoothClientPosition(float DeltaSeconds) override;
};
