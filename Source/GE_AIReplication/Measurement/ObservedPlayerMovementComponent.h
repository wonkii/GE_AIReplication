#pragma once

#include "GameFramework/CharacterMovementComponent.h"
#include "ObservedPlayerMovementComponent.generated.h"

enum class ERubberBandingReplay : uint8 { Standard, Turn90, Turn180, StateTransition };
enum class EOwnerTestSpeedMode : uint8 { Off, Baseline, Improved };
enum class EManualSprintMode : uint8 { Off, SeparateRpc, MoveData };

UCLASS()
class UObservedPlayerMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation,
		const FVector& NewLocation, const FQuat& NewRotation) override;

protected:
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
		float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewBase,
		FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode,
		FVector ServerGravityDirection) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;

private:
	FVector LastPendingInput = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Movement|Sprint", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SprintSpeed = 650.f;

#if !UE_BUILD_SHIPPING
public:
	void SetManualSprintMode(EManualSprintMode Mode, uint32 Revision);
	void ReceiveManualSprint(bool bHeld, uint32 ModeRevision);
	EManualSprintMode GetManualSprintMode() const { return ManualSprintMode; }
	uint32 GetManualSprintModeRevision() const { return ManualSprintModeRevision; }
	bool IsManualSprintActive() const;
	bool StartInputReplay(ERubberBandingReplay Pattern = ERubberBandingReplay::Standard);
	void CancelInputReplay();
	bool HadInvalidReplayTiming() const { return bInvalidReplayTiming; }
	void ConfigureOwnerTestSpeed(EOwnerTestSpeedMode Mode, float Walk, float Run, int32 TrialSerial);
	void ReceiveOwnerTestRun(bool bRun, int32 TrialSerial, int32 TransitionId);
	bool IsOwnerTestRunning() const { return bOwnerTestRun; }
	int32 GetOwnerTestTransition() const { return OwnerTestTransition; }
	double GetPendingMoveAge() const;
	virtual float GetMaxSpeed() const override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void OnClientTimeStampResetDetected() override;
	void ObservePredictedMove(const FSavedMove_Character& Move);
	bool IsInputReplayActive() const { return ReplayPhase != INDEX_NONE; }
	bool DidInputReplayComplete() const { return bReplayCompleted; }
	void RequestDiagnosticCorrection();
	void ClearDiagnosticCorrection() { bDiagnosticCorrectionPending = false; }
	TOptional<float> GetDiagnosticCorrectionTimestamp() const { return DiagnosticCorrectionTimestamp; }
	const TArray<float>& GetObservedCorrectionTimestamps() const { return ObservedCorrectionTimestamps; }

private:
	friend class FOwnerTestSavedMove;
	void UpdateManualSprintInput();
	void ApplyReplayInput(float DeltaTime);
	void SetReplayRun(bool bRun);
	void RecordRunRequest(const TCHAR* Event, bool bRun, int32 TransitionId);
	struct FPendingCorrection
	{
		FVector Before;
		float Timestamp;
		int32 Epoch;
		int32 Id;
	};
	TArray<FPendingCorrection> PendingCorrections;
	int32 CorrectionId = 0;
	int32 MoveEpoch = 0;
	float LastPredictedTimestamp = 0.f;
	EManualSprintMode ManualSprintMode = EManualSprintMode::MoveData;
	uint32 ManualSprintModeRevision = 0;
	bool bManualSprintHeld = false;
	EOwnerTestSpeedMode OwnerTestMode = EOwnerTestSpeedMode::Off;
	bool bOwnerTestRun = false;
	float OwnerTestWalkSpeed = 0.f;
	float OwnerTestRunSpeed = 0.f;
	int32 OwnerTestTrialSerial = 0;
	int32 OwnerTestTransition = 0;
	ERubberBandingReplay ReplayPattern = ERubberBandingReplay::Standard;
	bool bReplayPhaseStarted = false;
	bool bInvalidReplayTiming = false;
	int32 ReplayPhase = INDEX_NONE;
	float ReplayPhaseTime = 0.f;
	float ReplayExpectedTime = 0.f;
	float ReplayYaw = 0.f;
	bool bReplayCompleted = false;
	bool bDiagnosticCorrectionPending = false;
	TOptional<float> DiagnosticCorrectionTimestamp;
	TArray<float> ObservedCorrectionTimestamps;
#endif
};
