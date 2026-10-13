#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SharpMoveSnapshot.h"
#include "SharpEnemyCharacter.generated.h"

class USharpUpdatePolicy;
class USharpSplineFollowerComponent;
class USharpEnemyExperimentComponent;
class USharpMovementDiagnosticsComponent;
class USharpSnapshotChannelComponent;
class USharpMovementDetectorComponent;
class USharpMovementRecorderComponent;
class USharpVisualPredictorComponent;

// Physical character and standard CMC replication. Components own steering,
// execution lifetime, diagnostics and the auxiliary event channel separately.
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpEnemyCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UPROPERTY(EditInstanceOnly, Replicated, BlueprintReadOnly, Category="Sharp Experiment|Identity", meta=(ClampMin="1"))
	int32 ExperimentActorId = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy")
	TObjectPtr<USharpSplineFollowerComponent> SplineFollower;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy")
	TObjectPtr<USharpEnemyExperimentComponent> Experiment;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy")
	TObjectPtr<USharpMovementDiagnosticsComponent> Diagnostics;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy")
	TObjectPtr<USharpSnapshotChannelComponent> SnapshotChannel;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy")
	TObjectPtr<USharpUpdatePolicy> UpdatePolicy;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy") TObjectPtr<USharpMovementDetectorComponent> Detector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy") TObjectPtr<USharpMovementRecorderComponent> Recorder;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy") TObjectPtr<USharpVisualPredictorComponent> VisualPredictor;
	// Retained detector API; capture Generation and Segment at observation time.
	bool PublishDetectedSharpMoveSnapshot(const FSharpMoveSnapshot& Snapshot);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void OnRep_ReplicatedMovement() override;
};
