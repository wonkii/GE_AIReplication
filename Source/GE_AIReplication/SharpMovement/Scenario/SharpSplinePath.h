#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpSplinePath.generated.h"

class USplineComponent;
class ASharpEnemyCharacter;
class ASharpExperimentManager;

/** Editable target geometry and Enemy association. Runtime control is forwarded to the Manager. */
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Path|Validation", meta=(ClampMin="1.0", Units="cm"))
	float MinimumProjectionSeparation = 60.0f;

	UFUNCTION(BlueprintPure, Category = "Sharp Path")
	bool ValidatePath(FString& OutReason) const;

	float GetLength() const;
	FVector GetLocation(float Distance) const;
	FVector GetDirection(float Distance) const;
	float ProjectDistance(const FVector& WorldPosition) const;
	FString GetGeometrySignature() const;

	// The only editable source of the Enemy -> Path association.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Path|Enemy")
	TObjectPtr<ASharpEnemyCharacter> AssignedEnemy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Path|Placement", meta=(ClampMin="0.0", Units="cm"))
	float StartHeightOffset = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Path|Placement")
	bool bAlignRotationToPath = true;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Sharp Path|Runtime")
	FString LastCommandError;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Path|Control")
	void SetEnemyAtStart();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Path|Control")
	void StartEnemy();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Path|Control")
	void StopEnemy();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Path|Control")
	void ResetEnemy();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	ASharpExperimentManager* FindRegisteredManager();
	bool PlaceEditorEnemy();
	bool ValidateProjectionGeometry(FString& OutReason) const;
};
