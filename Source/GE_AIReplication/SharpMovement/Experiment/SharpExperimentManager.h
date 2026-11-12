#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpExperimentTypes.h"
#include "SharpExperimentSettings.h"
#include "../Movement/SharpRandomWalkTypes.h"
#include "SharpExperimentManager.generated.h"

class USharpExperimentSettings;
class ASharpSplinePath;
class ASharpEnemyCharacter;
class USharpConnectionRecorderComponent;
class ASharpIntegratedCourseObstacle;

UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpExperimentManager : public AActor
{
	GENERATED_BODY()

public:
	ASharpExperimentManager();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Paths")
	TArray<TObjectPtr<ASharpSplinePath>> Paths;
	// Each enemy must set ExperimentMovementSource=RandomWalk. No Spline needed.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk")
	TArray<TObjectPtr<ASharpEnemyCharacter>> RandomWalkEnemies;

    // Server assigns IDs and seeds in RandomWalkEnemies array order (index = 1, 2, ...).
    // Reordering that array changes the mapping. Spline IDs are never overwritten.
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Identity")
    bool bAutoAssignRandomWalkIdentity = true;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Identity", meta=(ClampMin="1"))
    int32 RandomWalkFirstActorId = 1;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Identity")
    int32 RandomWalkBaseSeed = 12345;

	// The auto-spawn feature is opt-in so existing Spline/manual maps do not change.
	// Set AutoSpawnEnemyClass to BP_SharpEnemy (or another SharpEnemy subclass) in the level.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn")
	bool bAutoSpawnRandomWalkEnemies = false;

	// Number of *additional* spawned enemies; leave RandomWalkEnemies empty for exactly 64.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(ClampMin="1", ClampMax="1024", EditCondition="bAutoSpawnRandomWalkEnemies"))
	int32 AutoSpawnEnemyCount = 64;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(EditCondition="bAutoSpawnRandomWalkEnemies"))
	TSubclassOf<ASharpEnemyCharacter> AutoSpawnEnemyClass;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(ClampMin="100.0", Units="cm", EditCondition="bAutoSpawnRandomWalkEnemies"))
	float AutoSpawnSpacingCm = 1000.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(EditCondition="bAutoSpawnRandomWalkEnemies"))
	ESharpRandomWalkBehaviorSelection AutoSpawnBehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed;

	// Disables net culling for spawned enemies so all clients can measure every actor.
	// Turn off only if testing relevancy/culling itself.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(EditCondition="bAutoSpawnRandomWalkEnemies"))
	bool bAutoSpawnAlwaysRelevant = true;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn", meta=(ClampMin="100.0", Units="cm", EditCondition="bAutoSpawnRandomWalkEnemies"))
	float AutoSpawnFloorTraceHalfHeightCm = 5000.0f;

	// Opt in: the existing free-roaming RandomWalk and all Spline trials stay unchanged.
	// Width/height are full extents in centimetres, centred on this Manager's XY.
	// The Manager's Yaw rotates the rectangular arena in the XY plane.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena")
	bool bEnableRandomWalkArena = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena", meta=(ClampMin="1000.0", Units="cm", EditCondition="bEnableRandomWalkArena"))
	FVector2D RandomWalkArenaSizeCm = FVector2D(20000.0f, 20000.0f);

	// Reserved inside the wall, in addition to each Enemy's capsule radius.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableRandomWalkArena"))
	float RandomWalkArenaMarginCm = 150.0f;

	// Predictive look-ahead is performed while walking and during moving turns.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena", meta=(ClampMin="100.0", Units="cm", EditCondition="bEnableRandomWalkArena"))
	float RandomWalkAvoidanceLookAheadCm = 450.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena", meta=(EditCondition="bEnableRandomWalkArena"))
	bool bRandomWalkAvoidWorldStaticWalls = true;

	// Static-wall probe is throttled; mathematical arena checks are cheap and run every tick.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Arena", meta=(ClampMin="0.02", Units="s", EditCondition="bEnableRandomWalkArena"))
	float RandomWalkWallProbeIntervalSeconds = 0.15f;

    // One mixed course, shared by ALL seven policy modes. No chase AI and no
    // per-pattern trials. Runtime obstacles appear in PIE/Standalone, not in
    // the saved editor .umap. Disable for legacy/isolated experiments.
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course")
    bool bEnableIntegratedMovementCourse = true;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="0", ClampMax="32", EditCondition="bEnableIntegratedMovementCourse"))
    int32 IntegratedObstacleCount = 8;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="100.0", Units="cm", EditCondition="bEnableIntegratedMovementCourse"))
    float IntegratedObstacleSizeCm = 300.0f;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="0.1", Units="s", EditCondition="bEnableIntegratedMovementCourse"))
    float IntegratedKnockbackPulseIntervalSeconds = 1.0f;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="1", ClampMax="64", EditCondition="bEnableIntegratedMovementCourse"))
    int32 IntegratedKnockbackActorsPerPulse = 6;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="1.0", Units="cm/s", EditCondition="bEnableIntegratedMovementCourse"))
    float IntegratedKnockbackHorizontalSpeed = 420.0f;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course",
        meta=(ClampMin="0.0", Units="cm/s", EditCondition="bEnableIntegratedMovementCourse"))
    float IntegratedKnockbackUpSpeed = 120.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course")
    int32 IntegratedSpawnedObstacleCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Random Walk|Auto Spawn")
	int32 AutoSpawnedEnemyCount = 0;

	// Read-only at runtime; resolved from Settings on the server and replicated.
	UPROPERTY(ReplicatedUsing=OnRep_OptimizationProfile, VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Optimization")
	ESharpExperimentOptimizationPreset ActiveOptimizationProfile = ESharpExperimentOptimizationPreset::Baseline;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Runtime")
	FSharpExperimentSummary Summary;
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Runtime")
	FString LastCommandError;
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Runtime") FSharpRecordingSettings RecordingConfiguration;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Startup")
	bool bPrepareOnBeginPlay = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Startup")
	bool bAutoStartTrial = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Startup", meta=(ClampMin="0.0", Units="s"))
	float AutoStartDelaySeconds = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Startup", meta=(ClampMin="0.1", Units="s"))
	float PreparationTimeoutSeconds = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Startup", meta=(ClampMin="0"))
	int32 ExpectedClientCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Measurement") TObjectPtr<USharpConnectionRecorderComponent> ConnectionRecorder;
	// One PIE/Standalone session runs Default -> EventPredictor, ten timed trials per mode.
	// This preset overrides the legacy Startup/Repeat controls at BeginPlay; disable it for manual trials.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Batch", meta=(DisplayName="Automatically Run All 7 Modes", ToolTip="Start the complete all-mode batch in one PIE session. Disable for manual or custom mode tests."))
	bool bAutoRunAllModes = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Batch", meta=(ClampMin="1", EditCondition="bAutoRunAllModes"))
	int32 AllModesRepetitionsPerMode = 10;
	// RandomWalk does not naturally complete; each trial needs a finite duration.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Batch", meta=(ClampMin="1.0", Units="s", EditCondition="bAutoRunAllModes"))
	float AllModesTrialDurationSeconds = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat") bool bAutoRepeat = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat", meta=(ClampMin="1")) int32 RepeatCount = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat") TArray<ESharpExperimentMode> ModeSequence;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat", meta=(ClampMin="0.0")) float RepeatDelaySeconds = 1.0f;
	// Zero waits for Once paths to finish. Set a duration for endless PingPong paths.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat", meta=(ClampMin="0.0")) float TrialDurationSeconds = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Repeat") bool bManageAdaptiveForModeSequence = true;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Control") void PrepareAll();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Control") void StartAll();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Control") void StopAll();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Control") void ResetAll();
	// Existing server Blueprint button calls remain valid.
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Trial") void PrepareTrial() { PrepareAll(); }
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Trial") void StartTrial() { StartAll(); }
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Trial") void SuspendTrial();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Trial") void ResumeTrial();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Sharp Experiment|Trial") void ResetTrial() { ResetAll(); }

	bool PrepareEnemy(ASharpSplinePath* Path);
	bool StartEnemy(ASharpSplinePath* Path);
	bool StopEnemy(ASharpSplinePath* Path);
	bool ResetEnemy(ASharpSplinePath* Path);
	bool IsRegistered(const ASharpSplinePath* Path) const;
	bool IsRegisteredRandom(const ASharpEnemyCharacter* Enemy) const;
	FTransform GetRandomWalkStartTransform(ASharpEnemyCharacter* Enemy) const;
	void RefreshSummary();
	void SetServerOnlyMovementDemo(bool bEnabled);
	bool IsServerOnlyMovementDemo() const { return bServerOnlyMovementDemo; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment")
	TObjectPtr<USharpExperimentSettings> Settings;

	// An empty RunId generates a new value on the server. Supply one to replay a run.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup")
	FString ConfiguredRunId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup", meta = (ClampMin = "1"))
	int32 ConfiguredTrialId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Startup")
	ESharpExperimentMode ConfiguredMode = ESharpExperimentMode::Default;

	UPROPERTY(ReplicatedUsing = OnRep_Identity, BlueprintReadOnly, Category = "Sharp Experiment")
	FSharpExperimentIdentity Identity;

	// 서버에서 확정된 실험 Identity 조회
	// 클라이언트에서 아직 Identity를 받지 못했다면 nullptr 반환
	const FSharpExperimentIdentity* GetResolvedIdentity() const
	{
		return Identity.RunId.IsEmpty() ? nullptr : &Identity;
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_Identity();
	UFUNCTION()
	void OnRep_OptimizationProfile();
	void ApplyOptimizationProfileToEnemies();

private:
	bool bServerOnlyMovementDemo = false;
	// First editor placement is frozen for deterministic repeat and mode comparisons.
	TMap<ASharpEnemyCharacter*, FTransform> RandomWalkInitialTransforms;
	void LogStartup() const;
	bool CanControl();
	bool PrepareGroup(bool bReset);
    bool AssignRandomWalkIdentities(FString& OutReason);
    bool SpawnConfiguredRandomWalkEnemies(FString& OutReason);
    bool SpawnIntegratedCourseObstacles(FString& OutReason);
    void ApplyIntegratedKnockbackPulse();
    void StopIntegratedKnockback();
    bool bIntegratedObstaclesSpawned = false;
    int32 IntegratedKnockbackPulseIndex = 0;
    FTimerHandle IntegratedKnockbackTimer;
    UPROPERTY(Transient) TArray<TObjectPtr<ASharpIntegratedCourseObstacle>> IntegratedObstacles;
	bool bAutoSpawnCompleted = false;
	bool PrepareSingle(ASharpSplinePath* Path, bool bReset);
	bool Reject(const FString& Reason);
	void CancelAutomaticControl();
	void TryAutomaticPrepare();
	void TryAutomaticStart();
	void OnGroupStarted();
	void OnGroupProgress();
	void CompleteTimedTrial();
	void RepeatNextTrial();
	void ApplyAutomaticNetworkMode();
	void RestoreAutomaticNetworkMode();
	FTimerHandle TrialDurationTimer, RepeatTimer;
	int32 SequenceIndex = 0, RepetitionsStarted = 0;
	bool bRepeatArmed = false, bRepeatContinuation = false, bOwnsAdaptiveCVar = false;
	bool bBatchFailureStopsGroup = false;
	bool bAllModesBatchActive = false;
	int32 OriginalAdaptiveValue = 0;
	uint32 OriginalAdaptivePriority = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<ASharpEnemyCharacter>> PreparedEnemies;
	FTimerHandle PreparationTimer;
	FTimerHandle AutoStartTimer;
	double PreparationDeadline = 0;
	double AutoStartEarliestTime = 0;
};

