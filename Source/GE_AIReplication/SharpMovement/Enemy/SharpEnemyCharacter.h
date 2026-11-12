#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SharpMoveSnapshot.h"
#include "../Movement/SharpRandomWalkTypes.h"
#include "SharpEnemyCharacter.generated.h"

class USharpUpdatePolicy;
class ASharpExperimentManager;
enum class ESharpExperimentOptimizationPreset : uint8;
class USharpSplineFollowerComponent;
class USharpRandomWalkerComponent;
class USharpEnemyExperimentComponent;
class USharpMovementDiagnosticsComponent;
class USharpSnapshotChannelComponent;
class USharpMovementDetectorComponent;
class USharpMovementRecorderComponent;
class USharpVisualPredictorComponent;
class UHitReactionComponent;
class UReactionEnvironmentComponent;
class USharpCombatAdapterComponent;

// Physical character and standard CMC replication. Components own steering,
// execution lifetime, diagnostics and the auxiliary event channel separately.
UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpEnemyCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<UHitReactionComponent> HitReaction;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<UReactionEnvironmentComponent> ReactionEnvironment;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<USharpCombatAdapterComponent> CombatAdapter;
	UPROPERTY(EditInstanceOnly, Replicated, BlueprintReadOnly, Category="Sharp Experiment|Identity", meta=(ClampMin="1"))
	int32 ExperimentActorId = 0;
	// Set RandomWalk for Enemies listed in ExperimentManager.RandomWalkEnemies.
	UPROPERTY(EditInstanceOnly, Replicated, BlueprintReadOnly, Category="Sharp Enemy|Movement")
	ESharpEnemyMovementSource ExperimentMovementSource = ESharpEnemyMovementSource::Spline;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy|Movement")
	TObjectPtr<USharpSplineFollowerComponent> SplineFollower;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Enemy|Movement")
	TObjectPtr<USharpRandomWalkerComponent> RandomWalker;
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
	// Applied by the level Manager (and again on replicated client spawn).
	void ApplyOptimizationProfile(const ASharpExperimentManager& Manager);
	ESharpExperimentOptimizationPreset GetOptimizationProfile() const;
	// Retained detector API; capture Generation and Segment at observation time.
	bool PublishDetectedSharpMoveSnapshot(const FSharpMoveSnapshot& Snapshot);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	bool bOptimizationDefaultsSaved = false;
	bool bInitialDiagnosticLogging = true;
	bool bInitialAnimationURO = false;
	uint8 InitialAnimationVisibilityOption = 0;
	bool bInitialAlwaysRelevant = false;
	float InitialNetCullDistanceSquared = 0.0f;
	uint8 AppliedOptimizationProfileValue = 0;
	virtual void BeginPlay() override;
	virtual void OnRep_ReplicatedMovement() override;
};
