#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpExperimentTypes.h"
#include "../Movement/SharpSplineMovementTypes.h"
#include "SharpExperimentManager.generated.h"

class USharpExperimentSettings;
class ASharpEnemyCharacter;
class ASharpSplinePath;
class ASharpEnemyAIController;
class ASharpImpulseVolume;

UCLASS(Blueprintable)
class GE_AIREPLICATION_API ASharpExperimentManager : public AActor
{
	GENERATED_BODY()

public:
	ASharpExperimentManager();

	// Empty list is a valid Control scenario. Only listed volumes may fire.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Sharp Experiment|M2 Setup")
	TArray<TObjectPtr<ASharpImpulseVolume>> ImpulseVolumes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation")
	bool bAutoStartTrial = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation", meta = (ClampMin = "0.0", Units = "s"))
	float AutoStartDelaySeconds = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation")
	bool bAutoRepeat = false;
	// Includes the first trial; applies to automatic and manually started successful trials.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation", meta = (ClampMin = "1"))
	int32 RepeatCount = 10;
	// Empty: repeat ConfiguredMode. Nonempty: RepeatCount trials per listed mode, sequentially.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation")
	TArray<ESharpExperimentMode> ModeSequence;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Automation", meta = (ClampMin = "0.0", Units = "s"))
	float RepeatDelaySeconds = 1.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Experiment|Runtime")
	bool bFinishingRecording = false;

	bool CanTriggerImpulse(const ASharpImpulseVolume* Volume, int32 TrialId) const;
	void SuspendForImpulse(float Duration);
	void FailImpulseTrial(const FString& Reason);
	virtual void Tick(float DeltaSeconds) override;

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

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Sharp Experiment|M1 Setup")
	TObjectPtr<ASharpEnemyCharacter> Enemy;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Sharp Experiment|M1 Setup")
	TObjectPtr<ASharpSplinePath> Path;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|M1 Setup")
	ESharpBaselineProfile BaselineProfile = ESharpBaselineProfile::EngineDefault;

	// Offset above the path's floor plane, added to the scaled capsule half-height.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Reset", meta = (ClampMin = "0.0", Units = "cm"))
	float StartHeightOffset = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sharp Experiment|Reset")
	bool bAlignRotationToPath = true;

	UPROPERTY(ReplicatedUsing = OnRep_TrialStatus, VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Experiment|Runtime")
	FSharpTrialStatus TrialStatus;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Experiment|Trial")
	void PrepareTrial();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Experiment|Trial")
	void StartTrial();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Experiment|Trial")
	void SuspendTrial();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Experiment|Trial")
	void ResumeTrial();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Sharp Experiment|Trial")
	void ResetTrial();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_Identity();

	UFUNCTION()
	void OnRep_TrialStatus();

	UFUNCTION()
	void OnFollowStateChanged(ESharpSplineFollowState State, const FString& Reason);

private:
	void LogStartup() const;
	bool CanControlTrial() const;
	bool ValidateTrial(FString& OutReason) const;
	void SetTrialState(ESharpTrialState State, const FString& Reason = FString());
	ASharpEnemyAIController* GetEnemyController() const;
	void RestoreReplicationProfile();
	void StopM2(const FString& Reason);
	void DisarmVolumes();
	void ScheduleStart();
	void AutoPrepare();
	void RepeatTrial();
	void FinishRecording();
	void CancelInputResume();
	void ResumeAfterImpulse();
	FTimerHandle AutoStartTimer;
	FTimerHandle RepeatTimer;
	FTimerHandle InputResumeTimer;
	int32 InputResumeTrialId = 0;
	int32 CompletedRepetitions = 0;
	int32 ModeSequenceIndex = 0;
	TArray<ESharpExperimentMode> ActiveModeSequence;
	float ActivePostTrialSeconds = 0.0f;
	uint64 FinishRequestFrame = 0;
	double FinishDeadline = 0.0;
	FString FinishReason;
	TArray<TWeakObjectPtr<ASharpImpulseVolume>> PreparedVolumes;

	UPROPERTY(Transient)
	TObjectPtr<ASharpEnemyCharacter> PreparedEnemy;

	float OriginalNetFrequency = 0.0f;
	float OriginalMinNetFrequency = 0.0f;
	bool bHasOriginalReplicationProfile = false;
};
