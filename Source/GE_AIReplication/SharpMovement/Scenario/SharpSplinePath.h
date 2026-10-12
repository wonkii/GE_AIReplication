#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpSplinePath.generated.h"

class USplineComponent;

/** Editable target geometry. Does not move characters or execute scenario events. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpSplinePath : public AActor
{
	GENERATED_BODY()

public:
	ASharpSplinePath();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Path")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Path")
	FName PathId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Path", meta = (ClampMin = "1"))
	int32 PathRevision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Path|Validation", meta = (ClampMin = "0.1", Units = "cm"))
	float MinimumSegmentLength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Path|Validation", meta = (ClampMin = "0.0", Units = "cm"))
	float PlanarHeightTolerance = 1.0f;

	UFUNCTION(BlueprintPure, Category = "Sharp Path")
	bool ValidatePath(FString& OutReason) const;

	float GetLength() const;
	FVector GetLocation(float Distance) const;
	FVector GetDirection(float Distance) const;
	float ProjectDistance(const FVector& WorldPosition) const;
	FString GetGeometrySignature() const;
};
