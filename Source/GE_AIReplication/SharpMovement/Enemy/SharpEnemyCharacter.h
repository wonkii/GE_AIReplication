#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VisualLogger/VisualLoggerDebugSnapshotInterface.h"
#include "../Experiment/SharpExperimentTypes.h"
#include "SharpMoveSnapshot.h"
#include "SharpEnemyCharacter.generated.h"

class USharpSplineFollowerComponent;
class UTextRenderComponent;
class USharpMovementRecorderComponent;
class USharpMovementDetectorComponent;
class USharpUpdatePolicyComponent;
class USharpVisualPredictorComponent;

/** Owns CMC and standard movement replication; contains no path steering logic. */
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpEnemyCharacter : public ACharacter, public IVisualLoggerDebugSnapshotInterface
{
	GENERATED_BODY()

public:
	ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<USharpMovementDetectorComponent> MovementDetector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<USharpUpdatePolicyComponent> UpdatePolicy;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<USharpVisualPredictorComponent> VisualPredictor;
	UPROPERTY(ReplicatedUsing = OnRep_ClientState, VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Enemy|Runtime")
	FSharpExperimentClientState ClientState;
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot, VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Enemy|Runtime")
	FSharpMoveSnapshot SharpMoveSnapshot;
	uint64 MovementGeneration = 0;
	void PublishSnapshot(FSharpMoveSnapshot Snapshot);
	void InvalidateSnapshot();
	virtual void OnRep_ReplicatedMovement() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<USharpMovementRecorderComponent> MovementRecorder;

#if ENABLE_VISUAL_LOG
	virtual void GrabDebugSnapshot(FVisualLogEntry* Snapshot) const override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<USharpSplineFollowerComponent> SplineFollower;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sharp Enemy")
	TObjectPtr<UTextRenderComponent> DebugLabel;

	UPROPERTY(EditInstanceOnly, Replicated, BlueprintReadOnly, Category = "Sharp Enemy")
	FName ActorId;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Enemy|Runtime")
	FSharpEnemyExperimentContext ExperimentContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Enemy|Debug")
	bool bShowDebugLabel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Enemy|Debug")
	bool bLogMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Enemy|Debug", meta = (ClampMin = "0.1", Units = "s"))
	float DiagnosticIntervalSeconds = 1.0f;

	// Preparation only: physical state belongs to the character, never to the steering component.
	bool PrepareAtStart(const FVector& FloorPosition, const FRotator& Rotation, float HeightOffset, FString& OutReason);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void OnRep_ClientState();
	UFUNCTION()
	void OnRep_Snapshot();
	uint32 SnapshotSequence = 0;
	float TimeSinceDiagnostic = 0.0f;
};
