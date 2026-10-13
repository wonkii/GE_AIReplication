#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpExperimentTypes.h"
#include "SharpExperimentManager.generated.h"

class USharpExperimentSettings;
class ASharpSplinePath;
class ASharpEnemyCharacter;
class USharpConnectionRecorderComponent;

UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpExperimentManager : public AActor
{
	GENERATED_BODY()

public:
	ASharpExperimentManager();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Paths")
	TArray<TObjectPtr<ASharpSplinePath>> Paths;
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
	void RefreshSummary();

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

private:
	void LogStartup() const;
	bool CanControl();
	bool PrepareGroup(bool bReset);
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
	int32 OriginalAdaptiveValue = 0;
	uint32 OriginalAdaptivePriority = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<ASharpEnemyCharacter>> PreparedEnemies;
	FTimerHandle PreparationTimer;
	FTimerHandle AutoStartTimer;
	double PreparationDeadline = 0;
	double AutoStartEarliestTime = 0;
};

