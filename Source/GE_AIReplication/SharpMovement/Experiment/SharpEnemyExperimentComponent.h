#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpExperimentTypes.h"
#include "../Movement/SharpSplineMovementTypes.h"
#include "SharpEnemyExperimentComponent.generated.h"

class ASharpEnemyCharacter;
class ASharpSplinePath;
class ASharpExperimentManager;
class USharpExperimentSettings;
class USharpSplineFollowerComponent;

// Per-Enemy execution lifetime. The Manager only coordinates these sessions.
UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpEnemyExperimentComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpEnemyExperimentComponent();
	UPROPERTY(ReplicatedUsing=OnRep_Status, VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Experiment|Runtime")
	FSharpEnemyTrialStatus Status;

	bool Prepare(ASharpExperimentManager* InManager, ASharpSplinePath* InPath,
		const FSharpExperimentIdentity& Identity, const FTransform& StartTransform, FString& OutReason);
	bool CanStart(FString& OutReason) const;
	bool Start(FString& OutReason);
	void Stop();
	void SuspendInput();
	bool ResumeInput(FString& OutReason);
	void Fail(const FString& Reason);
	void Release();
	bool ValidateBinding(FString& OutReason) const;
	bool IsInitialized() const { return Enemy != nullptr; }
	bool IsExecuting() const { return Status.State == ESharpEnemyTrialState::Running; }
	bool IsObservingMovement() const { return IsExecuting() || Status.State == ESharpEnemyTrialState::Suspended; }
	const USharpExperimentSettings* GetPreparedSettings() const { return PreparedSettings.Get(); }
	float GetPreparedNetFrequency() const { return PreparedNetHz; }
	float GetPreparedMinNetFrequency() const { return PreparedMinNetHz; }
	bool SuspendForImpulse(float Seconds);
	void Complete();
	bool Owns(const ASharpExperimentManager* InManager, const ASharpSplinePath* InPath) const;
	ASharpExperimentManager* GetManager() const { return Manager.Get(); }
	ASharpSplinePath* GetPath() const { return Path.Get(); }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	UPROPERTY(Transient) TObjectPtr<ASharpExperimentManager> Manager;
	UPROPERTY(Transient) TObjectPtr<ASharpSplinePath> Path;
	UPROPERTY(Transient) TObjectPtr<USharpExperimentSettings> PreparedSettings;
	int32 PreparedActorId = 0;
	FTimerHandle ImpulseResumeTimer;
	void CancelImpulseResume();
	float OriginalNetHz = 0, OriginalMinNetHz = 0;
	float PreparedNetHz = 0, PreparedMinNetHz = 0;
	bool bBaselineOverridden = false;
	void ApplyBaseline();
	void SetState(ESharpEnemyTrialState State, const FString& Reason = FString());
	void RestorePolicy();
	UFUNCTION() void OnRep_Status();
	UFUNCTION() void OnFollowStateChanged(USharpSplineFollowerComponent* Follower, ESharpSplineFollowState State, const FString& Reason);
};
