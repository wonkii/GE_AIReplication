#include "ObservedPlayerMovementComponent.h"

#include "RubberBandingCaptureSubsystem.h"
#include "RubberBandingSweep.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "GE_AIReplicationCharacter.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogRubberBandingReplay, Log, All);

class FOwnerTestSavedMove final : public FSavedMove_Character
{
	using Super = FSavedMove_Character;
public:
	virtual void Clear() override
	{
		Super::Clear();
		SpeedMode = EOwnerTestSpeedMode::Off;
		bRun = false;
		TrialSerial = 0;
		ManualSprintMode = EManualSprintMode::Off;
		ManualSprintModeRevision = 0;
		bManualSprintHeld = false;
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& Accel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, Accel, ClientData);
		const auto& Movement = *CastChecked<UObservedPlayerMovementComponent>(Character->GetCharacterMovement());
		SpeedMode = Movement.OwnerTestMode;
		bRun = Movement.bOwnerTestRun;
		TrialSerial = Movement.OwnerTestTrialSerial;
		ManualSprintMode = Movement.ManualSprintMode;
		ManualSprintModeRevision = Movement.ManualSprintModeRevision;
		bManualSprintHeld = Movement.bManualSprintHeld;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		return Super::GetCompressedFlags()
			| (SpeedMode == EOwnerTestSpeedMode::Improved && bRun ? FLAG_Custom_0 : 0)
			| (SpeedMode == EOwnerTestSpeedMode::Off && ManualSprintMode == EManualSprintMode::MoveData && bManualSprintHeld
				? FLAG_Custom_1 : 0);
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const auto& Next = static_cast<const FOwnerTestSavedMove&>(*NewMove);
		return TrialSerial == Next.TrialSerial && SpeedMode == Next.SpeedMode && bRun == Next.bRun
			&& ManualSprintModeRevision == Next.ManualSprintModeRevision && bManualSprintHeld == Next.bManualSprintHeld
			&& Super::CanCombineWith(NewMove, Character, MaxDelta);
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);
		auto& Movement = *CastChecked<UObservedPlayerMovementComponent>(Character->GetCharacterMovement());
		if (SpeedMode == EOwnerTestSpeedMode::Improved && Movement.OwnerTestTrialSerial == TrialSerial)
		{
			Movement.bOwnerTestRun = bRun;
		}
		Movement.bManualSprintHeld = bManualSprintHeld;
	}

	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode Mode) override
	{
		Super::PostUpdate(Character, Mode);
		if (Mode == PostUpdate_Record)
		{
			CastChecked<UObservedPlayerMovementComponent>(Character->GetCharacterMovement())->ObservePredictedMove(*this);
		}
	}

private:
	EOwnerTestSpeedMode SpeedMode = EOwnerTestSpeedMode::Off;
	bool bRun = false;
	int32 TrialSerial = 0;
	EManualSprintMode ManualSprintMode = EManualSprintMode::Off;
	uint32 ManualSprintModeRevision = 0;
	bool bManualSprintHeld = false;
};

namespace
{
	class FOwnerTestClientPrediction final : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FOwnerTestClientPrediction(const UCharacterMovementComponent& Movement)
			: FNetworkPredictionData_Client_Character(Movement) {}

		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FOwnerTestSavedMove()); }
	};

	struct FReplayPhase
	{
		const TCHAR* Event;
		float Seconds;
		FVector LocalInput;
		bool bRun = false;
	};

	const FReplayPhase StandardPhases[] = {
		{TEXT("ReplayForward"), 2.f, FVector::ForwardVector},
		{TEXT("ReplayPause"), 1.f, FVector::ZeroVector},
		{TEXT("ReplayRight"), 1.f, FVector::RightVector},
		{TEXT("ReplaySettle"), 2.f, FVector::ZeroVector},
	};

	const FReplayPhase Turn90Phases[] = {
		{TEXT("ReplayForward"), 2.f, FVector::ForwardVector},
		{TEXT("ReplayRight"), 1.f, FVector::RightVector},
		{TEXT("ReplaySettle"), 2.f, FVector::ZeroVector},
	};
	const FReplayPhase Turn180Phases[] = {
		{TEXT("ReplayForward"), 2.f, FVector::ForwardVector},
		{TEXT("ReplayReverse"), 1.f, -FVector::ForwardVector},
		{TEXT("ReplaySettle"), 2.f, FVector::ZeroVector},
	};
	const FReplayPhase StatePhases[] = {
		{TEXT("ReplayWalk"), 1.f, FVector::ForwardVector},
		{TEXT("ReplayRun"), 1.f, FVector::ForwardVector, true},
		{TEXT("ReplayBackToWalk"), 1.f, FVector::ForwardVector},
		{TEXT("ReplaySettle"), 2.f, FVector::ZeroVector},
	};

	TConstArrayView<FReplayPhase> GetPhases(ERubberBandingReplay Pattern)
	{
		switch (Pattern)
		{
		case ERubberBandingReplay::Turn90: return MakeArrayView(Turn90Phases);
		case ERubberBandingReplay::Turn180: return MakeArrayView(Turn180Phases);
		case ERubberBandingReplay::StateTransition: return MakeArrayView(StatePhases);
		default: return MakeArrayView(StandardPhases);
		}
	}

	UObservedPlayerMovementComponent* FindLocalMovement(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const AGE_AIReplicationCharacter* Player = PC && PC->IsLocalController() ? Cast<AGE_AIReplicationCharacter>(PC->GetPawn()) : nullptr;
		return Player && Player->GetLocalRole() == ROLE_AutonomousProxy
			? Cast<UObservedPlayerMovementComponent>(Player->GetCharacterMovement()) : nullptr;
	}

	FAutoConsoleCommandWithWorld ReplayCommand(TEXT("RubberBanding.Replay"),
		TEXT("Replay forward, pause, right and settle on the owning client during a capture."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (IsRubberBandingSweepActive(World))
			{
				UE_LOG(LogRubberBandingReplay, Warning, TEXT("A sweep owns input replay. Use RubberBanding.CancelSweep to stop it."));
				return;
			}
			if (UObservedPlayerMovementComponent* Movement = FindLocalMovement(World))
			{
				Movement->StartInputReplay();
				return;
			}
			UE_LOG(LogRubberBandingReplay, Warning, TEXT("Run RubberBanding.Replay in a connected client with an observed player."));
		}));

	FAutoConsoleCommandWithWorld CancelCommand(TEXT("RubberBanding.CancelReplay"), TEXT("Cancel this player's input replay."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!IsRubberBandingSweepActive(World))
			{
				if (UObservedPlayerMovementComponent* Movement = FindLocalMovement(World)) { Movement->CancelInputReplay(); }
			}
		}));
}

FNetworkPredictionData_Client* UObservedPlayerMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		const_cast<UObservedPlayerMovementComponent*>(this)->ClientPredictionData = new FOwnerTestClientPrediction(*this);
	}
	return ClientPredictionData;
}

float UObservedPlayerMovementComponent::GetMaxSpeed() const
{
	if (OwnerTestMode != EOwnerTestSpeedMode::Off && IsMovingOnGround())
	{
		return bOwnerTestRun ? OwnerTestRunSpeed : OwnerTestWalkSpeed;
	}
	if (IsManualSprintActive())
	{
		return FMath::Max(SprintSpeed, MaxWalkSpeed);
	}
	return Super::GetMaxSpeed();
}

bool UObservedPlayerMovementComponent::IsManualSprintActive() const
{
	return OwnerTestMode == EOwnerTestSpeedMode::Off && bManualSprintHeld
		&& CharacterOwner && IsMovingOnGround() && !CharacterOwner->IsCrouched();
}

void UObservedPlayerMovementComponent::SetManualSprintMode(EManualSprintMode Mode, uint32 Revision)
{
	ManualSprintMode = Mode;
	ManualSprintModeRevision = Revision;
	bManualSprintHeld = false;
}

void UObservedPlayerMovementComponent::ReceiveManualSprint(bool bHeld, uint32 ModeRevision)
{
	if (CharacterOwner && CharacterOwner->HasAuthority() && OwnerTestMode == EOwnerTestSpeedMode::Off
		&& ManualSprintMode == EManualSprintMode::SeparateRpc && ModeRevision == ManualSprintModeRevision)
	{
		bManualSprintHeld = bHeld;
	}
}

void UObservedPlayerMovementComponent::UpdateManualSprintInput()
{
	const APlayerController* Controller = CharacterOwner ? Cast<APlayerController>(CharacterOwner->GetController()) : nullptr;
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}

	// Sample the aggregate key state before CMC saves the move, including after input is flushed.
	const bool bHeld = ManualSprintMode != EManualSprintMode::Off && OwnerTestMode == EOwnerTestSpeedMode::Off
		&& !IsInputReplayActive() && (Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift));
	if (bHeld != bManualSprintHeld)
	{
		bManualSprintHeld = bHeld;
		if (ManualSprintMode == EManualSprintMode::SeparateRpc && !CharacterOwner->HasAuthority())
		{
			CastChecked<AGE_AIReplicationCharacter>(CharacterOwner)->ServerSetManualSprint(bHeld, ManualSprintModeRevision);
		}
	}
}

void UObservedPlayerMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	if (OwnerTestMode == EOwnerTestSpeedMode::Improved && CharacterOwner && CharacterOwner->HasAuthority())
	{
		bOwnerTestRun = IsMovingOnGround() && (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	}
	if (ManualSprintMode == EManualSprintMode::MoveData && CharacterOwner && CharacterOwner->HasAuthority())
	{
		bManualSprintHeld = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	}
}

void UObservedPlayerMovementComponent::ConfigureOwnerTestSpeed(EOwnerTestSpeedMode Mode, float Walk, float Run, int32 TrialSerial)
{
	OwnerTestMode = Mode;
	OwnerTestWalkSpeed = Walk;
	OwnerTestRunSpeed = Run;
	OwnerTestTrialSerial = TrialSerial;
	OwnerTestTransition = 0;
	bOwnerTestRun = false;
	PendingCorrections.Reset();
}

void UObservedPlayerMovementComponent::RecordRunRequest(const TCHAR* Event, bool bRun, int32 TransitionId)
{
	if (auto* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()))
	{
		const FVector Position = CharacterOwner->GetActorLocation();
		Capture->Record(*CharacterOwner, Event, Position, Position, {}, {}, bRun ? 1 : 0, TransitionId);
	}
}

void UObservedPlayerMovementComponent::ReceiveOwnerTestRun(bool bRun, int32 TrialSerial, int32 TransitionId)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || OwnerTestMode != EOwnerTestSpeedMode::Baseline ||
		TrialSerial != OwnerTestTrialSerial || TransitionId <= OwnerTestTransition)
	{
		return;
	}
	OwnerTestTransition = TransitionId;
	const bool bAllowed = !bRun || IsMovingOnGround();
	bOwnerTestRun = bRun && bAllowed;
	RecordRunRequest(bAllowed ? TEXT("ServerRunRequestApplied") : TEXT("ServerRunRequestRejected"), bRun, TransitionId);
}

void UObservedPlayerMovementComponent::SetReplayRun(bool bRun)
{
	if (OwnerTestMode == EOwnerTestSpeedMode::Off || bOwnerTestRun == bRun) { return; }
	bOwnerTestRun = bRun;
	++OwnerTestTransition;
	RecordRunRequest(TEXT("InputStateRequested"), bRun, OwnerTestTransition);
	if (OwnerTestMode == EOwnerTestSpeedMode::Baseline)
	{
		CastChecked<AGE_AIReplicationCharacter>(CharacterOwner)->ServerSetOwnerTestRun(bRun, OwnerTestTrialSerial, OwnerTestTransition);
	}
}

void UObservedPlayerMovementComponent::OnClientTimeStampResetDetected()
{
	Super::OnClientTimeStampResetDetected();
	++MoveEpoch;
}

void UObservedPlayerMovementComponent::ObservePredictedMove(const FSavedMove_Character& Move)
{
	if (LastPredictedTimestamp - Move.TimeStamp > MinTimeBetweenTimeStampResets * 0.5f) { ++MoveEpoch; }
	LastPredictedTimestamp = Move.TimeStamp;
	if (auto* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()); Capture && Capture->IsTrialMover(*CharacterOwner))
	{
		Capture->Record(*CharacterOwner, TEXT("OwnerMovePredicted"), Move.StartLocation, Move.SavedLocation,
			Move.TimeStamp, Move.DeltaTime, Move.GetCompressedFlags(), {}, MoveEpoch);
	}
}

double UObservedPlayerMovementComponent::GetPendingMoveAge() const
{
	if (!ClientPredictionData || ClientPredictionData->SavedMoves.IsEmpty()) { return 0; }
	const auto& Oldest = *ClientPredictionData->SavedMoves[0];
	return ClientPredictionData->CurrentTimeStamp - Oldest.TimeStamp +
		(Oldest.bOldTimeStampBeforeReset ? MinTimeBetweenTimeStampResets : 0.f);
}

bool UObservedPlayerMovementComponent::StartInputReplay(ERubberBandingReplay Pattern)
{
	if (ReplayPhase != INDEX_NONE || !IsValid(CharacterOwner) || !CharacterOwner->IsLocallyControlled() ||
		CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy || !URubberBandingCaptureSubsystem::GetActive(GetWorld()) ||
		!IsMovingOnGround() || !Velocity.IsNearlyZero(1.f) || !GetPendingInputVector().IsNearlyZero())
	{
		UE_LOG(LogRubberBandingReplay, Warning, TEXT("Replay requires an active capture and a stationary player on the ground with no active replay."));
		return false;
	}
	bReplayCompleted = false;
	bInvalidReplayTiming = false;
	ObservedCorrectionTimestamps.Reset();
	ReplayYaw = CharacterOwner->GetControlRotation().Yaw;
	ReplayPattern = Pattern;
	ReplayPhase = 0;
	bReplayPhaseStarted = false;
	ReplayPhaseTime = 0.f;
	ReplayExpectedTime = 0.f;
	UE_LOG(LogRubberBandingReplay, Display, TEXT("Replay started at %s, camera yaw %.2f."),
		*CharacterOwner->GetActorLocation().ToString(), ReplayYaw);
	return true;
}

void UObservedPlayerMovementComponent::RequestDiagnosticCorrection()
{
	DiagnosticCorrectionTimestamp.Reset();
	bDiagnosticCorrectionPending = true;
}

void UObservedPlayerMovementComponent::CancelInputReplay()
{
	if (ReplayPhase == INDEX_NONE) { return; }
	ReplayPhase = INDEX_NONE;
	SetReplayRun(false);
	if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()); Capture && IsValid(CharacterOwner))
	{
		const FVector Position = CharacterOwner->GetActorLocation();
		Capture->Record(*CharacterOwner, TEXT("ReplayCancelled"), Position, Position);
	}
	UE_LOG(LogRubberBandingReplay, Display, TEXT("Replay cancelled."));
}

void UObservedPlayerMovementComponent::ApplyReplayInput(float DeltaTime)
{
	if (ReplayPhase == INDEX_NONE || DeltaTime <= 0.f) { return; }
	URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld());
	AGE_AIReplicationCharacter* Player = Cast<AGE_AIReplicationCharacter>(CharacterOwner);
	if (!Capture || !IsValid(Player) || !Player->IsLocallyControlled() || Player->GetLocalRole() != ROLE_AutonomousProxy ||
		!IsMovingOnGround() || !GetPendingInputVector().IsNearlyZero())
	{
		CancelInputReplay();
		return;
	}
	const TConstArrayView<FReplayPhase> ReplayPhases = GetPhases(ReplayPattern);
	if (ReplayPhaseTime >= ReplayPhases[ReplayPhase].Seconds)
	{
		const float Duration = ReplayPhases[ReplayPhase].Seconds;
		ReplayExpectedTime += Duration;
		ReplayPhaseTime = ReplayPattern == ERubberBandingReplay::Standard ? 0.f : ReplayPhaseTime - Duration;
		++ReplayPhase;
		bReplayPhaseStarted = false;
		if (ReplayPhase == ReplayPhases.Num())
		{
			ReplayPhase = INDEX_NONE;
			bReplayCompleted = true;
			const FVector Position = Player->GetActorLocation();
			Capture->Record(*Player, TEXT("ReplayCompleted"), Position, Position);
			UE_LOG(LogRubberBandingReplay, Display, TEXT("Replay completed."));
			return;
		}
		if (ReplayPhaseTime >= ReplayPhases[ReplayPhase].Seconds)
		{
			bInvalidReplayTiming = true;
			const FVector Position = Player->GetActorLocation();
			Capture->Record(*Player, TEXT("ReplayInvalidTiming"), Position, Position, {}, ReplayPhaseTime);
			CancelInputReplay();
			return;
		}
	}
	const FReplayPhase& Phase = ReplayPhases[ReplayPhase];
	const FVector Direction = FRotator(0.f, ReplayYaw, 0.f).RotateVector(Phase.LocalInput);
	const FVector Input = FRotator(0.f, Player->GetControlRotation().Yaw, 0.f).UnrotateVector(Direction);
	if (!bReplayPhaseStarted)
	{
		bReplayPhaseStarted = true;
		const float BeforeSpeed = GetMaxSpeed();
		const bool bAtSpeed = Velocity.Size2D() >= BeforeSpeed * 0.95f;
		SetReplayRun(Phase.bRun);
		Capture->Record(*Player, Phase.Event, Player->GetActorLocation(), Player->GetActorLocation(), {}, Phase.Seconds);
		if (ReplayPattern != ERubberBandingReplay::Standard)
		{
			Capture->Record(*Player, TEXT("ReplayPhaseTiming"),
				FVector(ReplayExpectedTime, ReplayExpectedTime + ReplayPhaseTime, ReplayPhaseTime),
				Direction, {}, Phase.Seconds, bAtSpeed ? 1 : 0);
		}
	}
	if (!Phase.LocalInput.IsZero()) { Player->DoMove(Input.Y, Input.X); }
	ReplayPhaseTime += DeltaTime;
}
#endif

void UObservedPlayerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
#if !UE_BUILD_SHIPPING
	UpdateManualSprintInput();
	ApplyReplayInput(DeltaTime);
#endif
	if (IsValid(CharacterOwner) && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
	{
		const FVector Input = GetPendingInputVector();
		if (!Input.Equals(LastPendingInput, 0.01))
		{
			if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()))
			{
				Capture->Record(*CharacterOwner, TEXT("OwnerInputChanged"), LastPendingInput, Input);
			}
		}
		LastPendingInput = Input;
	}
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()); Capture && IsValid(CharacterOwner))
	{
		const USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
		Capture->Record(*CharacterOwner, TEXT("MovementSample"), CharacterOwner->GetActorLocation(),
			Mesh ? Mesh->GetComponentLocation() : CharacterOwner->GetActorLocation(), {}, DeltaTime);
	}
}

void UObservedPlayerMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime,
	uint8 CompressedFlags, const FVector& NewAccel)
{
	URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld());
	const bool bObserve = Capture && IsValid(CharacterOwner) && CharacterOwner->HasAuthority();
	const FVector Before = bObserve ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	if (bObserve && IsValid(CharacterOwner))
	{
		TOptional<int32> Epoch;
#if !UE_BUILD_SHIPPING
		Epoch = MoveEpoch;
#endif
		Capture->Record(*CharacterOwner, TEXT("ServerMoveProcessed"), Before, CharacterOwner->GetActorLocation(),
			ClientTimeStamp, DeltaTime, CompressedFlags, {}, Epoch);
	}
}

bool UObservedPlayerMovementComponent::ServerCheckClientError(float ClientTimeStamp, float DeltaTime,
	const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
	FMovementBaseInterfaceData* ClientBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	const bool bError = Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation,
		RelativeClientLocation, ClientBase, ClientBaseBoneName, ClientMovementMode);
	if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()); Capture && IsValid(CharacterOwner))
	{
		TOptional<int32> Epoch;
#if !UE_BUILD_SHIPPING
		Epoch = MoveEpoch;
#endif
		Capture->Record(*CharacterOwner, TEXT("ServerErrorCheck"), ClientWorldLocation, CharacterOwner->GetActorLocation(),
			ClientTimeStamp, DeltaTime, bError ? 1 : 0, {}, Epoch);
#if !UE_BUILD_SHIPPING
		if (CharacterOwner->HasAuthority() && bDiagnosticCorrectionPending)
		{
			ClearDiagnosticCorrection();
			DiagnosticCorrectionTimestamp = ClientTimeStamp;
			Capture->Record(*CharacterOwner, TEXT("ServerDiagnosticCorrection"), ClientWorldLocation,
				CharacterOwner->GetActorLocation(), ClientTimeStamp, DeltaTime);
			return true;
		}
#endif
	}
	return bError;
}

void UObservedPlayerMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
	float TimeStamp, FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewBase,
	FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode,
	FVector ServerGravityDirection)
{
	if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld()); Capture && IsValid(CharacterOwner))
	{
		TOptional<int32> Id, Epoch;
#if !UE_BUILD_SHIPPING
		ObservedCorrectionTimestamps.Add(TimeStamp);
		Id = ++CorrectionId;
		Epoch = MoveEpoch - (ClientData.LastAckedMove.IsValid() && ClientData.LastAckedMove->bOldTimeStampBeforeReset ? 1 : 0);
		PendingCorrections.Add({CharacterOwner->GetActorLocation(), TimeStamp, Epoch.GetValue(), Id.GetValue()});
#endif
		Capture->Record(*CharacterOwner, TEXT("OwnerCorrectionReceived"), CharacterOwner->GetActorLocation(),
			NewLocation, TimeStamp, {}, ServerMovementMode, Id, Epoch);
	}
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName,
		bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

bool UObservedPlayerMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld());
	const FVector Before = Capture && IsValid(CharacterOwner) ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
#if !UE_BUILD_SHIPPING
	const bool bLiveRun = bOwnerTestRun;
	const bool bLiveManualSprint = bManualSprintHeld;
	const double Started = FPlatformTime::Seconds();
#endif
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
#if !UE_BUILD_SHIPPING
	bOwnerTestRun = bLiveRun;
	bManualSprintHeld = bLiveManualSprint;
	if (Capture && IsValid(CharacterOwner) && !PendingCorrections.IsEmpty())
	{
		const FVector After = CharacterOwner->GetActorLocation();
		const auto& First = PendingCorrections[0];
		for (int32 Index = 0; Index < PendingCorrections.Num() - 1; ++Index)
		{
			const auto& Entry = PendingCorrections[Index];
			Capture->Record(*CharacterOwner, TEXT("OwnerCorrectionSuperseded"), Entry.Before, Entry.Before,
				Entry.Timestamp, {}, {}, Entry.Id, Entry.Epoch);
		}
		const auto& Last = PendingCorrections.Last();
		Capture->Record(*CharacterOwner, TEXT("OwnerReconciliation"), First.Before, After, Last.Timestamp,
			static_cast<float>(FPlatformTime::Seconds() - Started), bReplayed ? 1 : 0, Last.Id, Last.Epoch);
	}
	PendingCorrections.Reset();
#endif
	if (Capture && bReplayed && IsValid(CharacterOwner))
	{
		Capture->Record(*CharacterOwner, TEXT("OwnerReplay"), Before, CharacterOwner->GetActorLocation());
	}
	return bReplayed;
}

void UObservedPlayerMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation,
	const FVector& NewLocation, const FQuat& NewRotation)
{
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
	if (URubberBandingCaptureSubsystem* Capture = URubberBandingCaptureSubsystem::GetActive(GetWorld());
		Capture && IsValid(CharacterOwner) && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		Capture->Record(*CharacterOwner, TEXT("RemoteCorrectionTarget"), OldLocation, NewLocation);
		Capture->Record(*CharacterOwner, TEXT("RemoteCapsuleApplied"), OldLocation, CharacterOwner->GetActorLocation());
	}
}
