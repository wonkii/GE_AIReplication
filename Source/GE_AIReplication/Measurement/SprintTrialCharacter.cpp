#include "SprintTrialCharacter.h"

#include "SprintTrialCapture.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/NetConnection.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/BitWriter.h"

namespace
{
	USprintTrialPolicy* FindSprintPolicy(const AActor* Owner)
	{
		return Owner ? Owner->FindComponentByClass<USprintTrialPolicy>() : nullptr;
	}

	USprintTrialCapture* ActiveProfile(const ACharacter* Character)
	{
		const auto* Subject = Cast<ASprintTrialCharacter>(Character);
		auto* Capture = Subject ? USprintTrialCapture::Find(Subject->GetWorld()) : nullptr;
		return Capture && Capture->IsProfiling(*Subject) ? Capture : nullptr;
	}

	class FSprintSavedMove final : public FSavedMove_Character
	{
	public:
		bool WantsSprint() const { return bRun; }

		virtual void Clear() override
		{
			FSavedMove_Character::Clear();
			bRun = false;
			bSendRun = false;
		}

		virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& Accel,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			FSavedMove_Character::SetMoveFor(Character, InDeltaTime, Accel, ClientData);
			if (const USprintTrialPolicy* Policy = FindSprintPolicy(Character))
			{
				bRun = Policy->IsRequested();
				bSendRun = Policy->Variant == ESprintTrialVariant::MoveData;
			}
		}

		virtual uint8 GetCompressedFlags() const override
		{
			return FSavedMove_Character::GetCompressedFlags() | (bRun && bSendRun ? FLAG_Custom_0 : 0);
		}

		virtual bool CanCombineWith(const FSavedMovePtr& Next, ACharacter* Character, float MaxDelta) const override
		{
			return bRun == static_cast<const FSprintSavedMove&>(*Next).bRun
				&& FSavedMove_Character::CanCombineWith(Next, Character, MaxDelta);
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			FSavedMove_Character::PrepMoveFor(Character);
			if (USprintTrialPolicy* Policy = FindSprintPolicy(Character))
			{
				Policy->Apply(bRun);
			}
		}

		virtual void PostUpdate(ACharacter* Character, EPostUpdateMode Mode) override
		{
			FSavedMove_Character::PostUpdate(Character, Mode);
			if (Mode == PostUpdate_Record && !ActiveProfile(Character))
			{
				CastChecked<USprintStateMovement>(Character->GetCharacterMovement())->RecordOwnerMove(TimeStamp, bRun);
			}
		}

	private:
		bool bRun = false;
		bool bSendRun = false;
	};

	class FSprintPrediction final : public FNetworkPredictionData_Client_Character
	{
	public:
		using FNetworkPredictionData_Client_Character::FNetworkPredictionData_Client_Character;
		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSprintSavedMove); }
	};
}

USprintObservedMovement::USprintObservedMovement(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetNetworkMoveDataContainer(TrialMoveData);
	SetMoveResponseDataContainer(TrialResponse);
}

USprintObservedMovement::FMeasuredMoveContainer::FMeasuredMoveContainer()
{
	NewMoveData = &Moves[0];
	PendingMoveData = &Moves[1];
	OldMoveData = &Moves[2];
}

void USprintObservedMovement::EnableIndependentSprintBit()
{
	for (FMeasuredMove& Move : TrialMoveData.Moves) { Move.bUseSprintBit = true; }
}

bool USprintObservedMovement::GetIndependentSprintIntent() const
{
	const auto* Move = static_cast<const FMeasuredMove*>(GetCurrentNetworkMoveData());
	return Move && Move->bWantsSprint;
}

void USprintObservedMovement::FMeasuredMove::ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type)
{
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(Move, Type);
	const auto* Subject = Cast<ASprintTrialCharacter>(Move.CharacterOwner);
	bWantsSprint = Subject && Subject->GetSprintPolicy() && static_cast<const FSprintSavedMove&>(Move).WantsSprint();
}

bool USprintObservedMovement::FMeasuredMove::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar,
	UPackageMap* Map, ENetworkMoveType Type)
{
	const bool bSaving = Ar.IsSaving();
	FirstBit = bSaving ? static_cast<const FBitWriter&>(Ar).GetNumBits() : 0;
	if (!FCharacterNetworkMoveData::Serialize(Movement, Ar, Map, Type)) { return false; }
	if (bUseSprintBit)
	{
		uint8 Sprint = bSaving && bWantsSprint ? 1 : 0;
		Ar.SerializeBits(&Sprint, 1);
		bWantsSprint = Sprint != 0;
	}
	else if (Ar.IsLoading())
	{
		bWantsSprint = (CompressedMoveFlags & FSavedMove_Character::FLAG_Custom_0) != 0;
	}
	EndBit = bSaving ? static_cast<const FBitWriter&>(Ar).GetNumBits() : 0;
	return !Ar.IsError();
}

bool USprintObservedMovement::FMeasuredMoveContainer::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map)
{
	if (!FCharacterNetworkMoveDataContainer::Serialize(Movement, Ar, Map)) { return false; }
	const auto* Character = Movement.GetCharacterOwner();
	if (auto* Capture = Ar.IsSaving() ? ActiveProfile(Character) : nullptr)
	{
		const auto CopyMove = [](const FMeasuredMove& Move)
		{
			return FSprintPackedMove{Move.TimeStamp, Move.CompressedMoveFlags, Move.bWantsSprint, Move.FirstBit, Move.EndBit};
		};
		FSprintPackedSample Sample;
		Sample.Serial = ++Serial;
		Sample.Epoch = Epoch;
		Sample.AckFlags = AckFlags;
		Sample.Bits = static_cast<const FBitWriter&>(Ar).GetNumBits();
		Sample.NewMove = CopyMove(Moves[0]);
		if (bHasPendingMove) { Sample.Pending = CopyMove(Moves[1]); }
		if (bHasOldMove) { Sample.Old = CopyMove(Moves[2]); }
		const UNetConnection* Connection = Character->GetNetConnection();
		Capture->RecordPacked(Sample, Connection ? Connection->OutPacketId : INDEX_NONE);
	}
	return true;
}

bool USprintObservedMovement::FMeasuredResponse::Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, Map)) { return false; }
	const auto* Character = Movement.GetCharacterOwner();
	if (auto* Capture = Ar.IsSaving() ? ActiveProfile(Character) : nullptr)
	{
		FSprintPackedSample Sample;
		Sample.Serial = ++Serial;
		Sample.Epoch = Epoch;
		Sample.Bits = static_cast<const FBitWriter&>(Ar).GetNumBits();
		Sample.Ack = IsGoodMove() ? 1 : 0;
		Sample.NewMove.Timestamp = ClientAdjustment.TimeStamp;
		const UNetConnection* Connection = Character->GetNetConnection();
		Capture->RecordPacked(Sample, Connection ? Connection->OutPacketId : INDEX_NONE);
	}
	return true;
}

void USprintObservedMovement::CallServerMovePacked(const FSavedMove_Character* NewMove,
	const FSavedMove_Character* PendingMove, const FSavedMove_Character* OldMove)
{
	if (ActiveProfile(CharacterOwner))
	{
		const auto& LastAck = GetPredictionData_Client_Character()->LastAckedMove;
		TrialMoveData.AckFlags = LastAck.IsValid() ? LastAck->GetCompressedFlags() : INDEX_NONE;
		TrialMoveData.Epoch = OwnerEpoch + (NewMove && LastOwnerTimestamp - NewMove->TimeStamp > MinTimeBetweenTimeStampResets * 0.5f);
	}
	Super::CallServerMovePacked(NewMove, PendingMove, OldMove);
}

ASprintTrialCharacter::ASprintTrialCharacter(const FObjectInitializer& Initializer) : Super(Initializer)
{
	bAlwaysRelevant = true;
	SetReplicates(true);
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = USprintTrialPolicy::WalkSpeed;
	Movement->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;
}

ASprintCmcWalkCharacter::ASprintCmcWalkCharacter(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<USprintObservedMovement>(CharacterMovementComponentName))
{
}

bool ASprintTrialCharacter::InitializePresentation(const ACharacter& Original)
{
	USkeletalMeshComponent* OriginalMesh = Original.GetMesh();
	if (!OriginalMesh || !OriginalMesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	FVector Offset = Original.GetBaseTranslationOffset();
	Offset.Z += Original.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	TrialPresentation.Mesh = OriginalMesh->GetSkeletalMeshAsset();
	TrialPresentation.AnimClass = OriginalMesh->GetAnimationMode() == EAnimationMode::AnimationBlueprint
		? OriginalMesh->GetAnimClass() : nullptr;
	TrialPresentation.MeshOffset = FTransform(Original.GetBaseRotationOffset(), Offset, OriginalMesh->GetRelativeScale3D());
	OnRep_TrialPresentation();
	return HasTrialPresentation();
}

bool ASprintTrialCharacter::HasTrialPresentation() const
{
	return bPresentationApplied && (!TrialPresentation.AnimClass || GetNetMode() == NM_DedicatedServer
		|| GetMesh()->GetAnimInstance() != nullptr);
}

void ASprintTrialCharacter::OnRep_TrialPresentation()
{
	if (!TrialPresentation.Mesh)
	{
		return;
	}

	USkeletalMeshComponent* TrialMesh = GetMesh();
	TrialMesh->SetSkeletalMesh(TrialPresentation.Mesh);
	TrialMesh->SetRelativeTransform(TrialPresentation.MeshOffset);
	CacheInitialMeshOffset(TrialPresentation.MeshOffset.GetLocation(), TrialPresentation.MeshOffset.Rotator());
	TrialMesh->SetAnimInstanceClass(TrialPresentation.AnimClass);
	if (UAnimInstance* AnimInstance = TrialMesh->GetAnimInstance())
	{
		// The trial measures CMC movement. Use the animation pose without applying root motion.
		AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	}
	bPresentationApplied = true;
}

ASprintRpcCharacter::ASprintRpcCharacter(const FObjectInitializer& Initializer)
	: Super(Initializer.SetDefaultSubobjectClass<USprintStateMovement>(CharacterMovementComponentName))
{
	SprintPolicy = CreateDefaultSubobject<USprintTrialPolicy>(TEXT("SprintPolicy"));
	SprintPolicy->Variant = ESprintTrialVariant::SeparateRpc;
}

ASprintMoveDataCharacter::ASprintMoveDataCharacter(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SprintPolicy->Variant = ESprintTrialVariant::MoveData;
}

ASprintMoveDataBitCharacter::ASprintMoveDataBitCharacter(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SprintPolicy->Variant = ESprintTrialVariant::MoveDataBit;
	CastChecked<USprintObservedMovement>(GetCharacterMovement())->EnableIndependentSprintBit();
}

void ASprintTrialCharacter::ServerRequestSprint_Implementation(bool bRun, int32 Serial)
{
	if (!SprintPolicy) { return; }
	if (auto* Profile = ActiveProfile(this))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SprintProfile_Server_StateRpc);
		const double Start = FPlatformTime::Seconds();
		Profile->Record(*this, TEXT("SprintRpcReceived"), 0.f, 0, FVector::ZeroVector, Serial, -1, 0., bRun);
		SprintPolicy->AcceptTransition(bRun, Serial);
		Profile->RecordTiming(ESprintProfileEvent::StateRpc, Start, FPlatformTime::Seconds());
		return;
	}
	SprintPolicy->AcceptTransition(bRun, Serial);
}

void ASprintTrialCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASprintTrialCharacter, TrialId);
	DOREPLIFETIME_CONDITION(ASprintTrialCharacter, TrialPresentation, COND_InitialOnly);
}

void ASprintTrialCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& View)
{
	if (TrialCamera.IsSet())
	{
		View = TrialCamera.GetValue();
		View.Location += GetActorLocation();
		return;
	}
	Super::CalcCamera(DeltaTime, View);
	const FVector Focus = GetActorLocation() + FVector(0, 0, 30);
	const FVector Offset = GetLocalRole() == ROLE_SimulatedProxy ? FVector(-170, -900, 510) : FVector(-460, -270, 210);
	View.Location = Focus + GetActorRotation().RotateVector(Offset);
	View.Rotation = (Focus - View.Location).Rotation();
	View.FOV = 65.f;
}

void ASprintTrialCharacter::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()))
		{
			Capture->Record(*this, TEXT("RemoteMovement"));
		}
	}
}

void USprintTrialPolicy::Apply(bool bRun)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->MaxWalkSpeed = bRun && Movement->IsMovingOnGround() && !Character->IsCrouched() ? RunSpeed : WalkSpeed;
	}
}

void USprintTrialPolicy::Request(bool bRun)
{
	auto* Character = Cast<ASprintTrialCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled())
	{
		return;
	}
	Apply(bRun);
	if (bRequested != bRun)
	{
		bRequested = bRun;
		if (Variant == ESprintTrialVariant::SeparateRpc && !Character->HasAuthority())
		{
			++Transition;
			if (auto* Profile = ActiveProfile(Character))
			{
				const auto* Connection = Character->GetNetConnection();
				Profile->Record(*Character, TEXT("SprintRpcSent"), 0.f, 0, FVector::ZeroVector, Transition, Character->TrialPhase,
					0., bRun, -1, -1, -1, -1., Connection ? Connection->OutPacketId : INDEX_NONE);
			}
			Character->ServerRequestSprint(bRun, Transition);
		}
	}
}

void USprintTrialPolicy::AcceptTransition(bool bRun, int32 Serial)
{
	auto* Character = Cast<ASprintTrialCharacter>(GetOwner());
	if (!Character || !Character->HasAuthority() || Variant != ESprintTrialVariant::SeparateRpc || Serial <= Transition)
	{
		return;
	}
	Transition = Serial;
	bRequested = bRun && Character->GetCharacterMovement()->IsMovingOnGround() && !Character->IsCrouched();
	Apply(bRequested);
	if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()))
	{
		Capture->Record(*Character, TEXT("SprintRpc"), 0.f, 0, FVector::ZeroVector, Serial, -1, 0., bRequested);
	}
}

FNetworkPredictionData_Client* USprintStateMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		const_cast<USprintStateMovement*>(this)->ClientPredictionData = new FSprintPrediction(*this);
	}
	return ClientPredictionData;
}

void USprintStateMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		if (USprintTrialPolicy* Policy = FindSprintPolicy(CharacterOwner))
		{
			bool bRun = Policy->IsRequested();
			switch (Policy->Variant)
			{
			case ESprintTrialVariant::MoveData:
				bRun = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
				break;
			case ESprintTrialVariant::MoveDataBit:
				bRun = GetIndependentSprintIntent();
				break;
			default:
				break;
			}
			Policy->Apply(bRun);
		}
	}
}

bool USprintStateMovement::ClientUpdatePositionAfterServerUpdate()
{
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	if (USprintTrialPolicy* Policy = FindSprintPolicy(CharacterOwner))
	{
		Policy->Apply(Policy->IsRequested());
	}
	return bResult;
}

void USprintObservedMovement::RecordOwnerMove(float Timestamp, bool bRun)
{
	if (LastOwnerTimestamp - Timestamp > MinTimeBetweenTimeStampResets * 0.5f) { ++OwnerEpoch; }
	LastOwnerTimestamp = Timestamp;
	if (const auto* Character = Cast<ASprintTrialCharacter>(CharacterOwner); Character && Character->TrialPhase != INDEX_NONE)
	{
		if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()))
		{
			Capture->Record(*Character, TEXT("OwnerMove"), Timestamp, OwnerEpoch, FVector::ZeroVector, bRun ? 1. : 0., Character->TrialPhase);
		}
	}
}

void USprintObservedMovement::ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration)
{
	auto* Profile = ActiveProfile(CharacterOwner);
	if (!Profile)
	{
		Super::ReplicateMoveToServer(DeltaTime, NewAcceleration);
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(SprintProfile_Owner_Prediction);
	const float Before = GetPredictionData_Client_Character()->CurrentTimeStamp;
	const double Start = FPlatformTime::Seconds();
	{
		TGuardValue<bool> Predicting(bPredicting, true);
		Super::ReplicateMoveToServer(DeltaTime, NewAcceleration);
	}
	const double End = FPlatformTime::Seconds();
	const float After = GetPredictionData_Client_Character()->CurrentTimeStamp;
	if (After != Before)
	{
		const auto* Subject = CastChecked<ASprintTrialCharacter>(CharacterOwner);
		const auto* Policy = Subject->GetSprintPolicy();
		RecordOwnerMove(After, Policy && Policy->IsRequested());
		Profile->RecordTiming(ESprintProfileEvent::Prediction, Start, End, After, OwnerEpoch);
	}
}

void USprintObservedMovement::PerformMovement(float DeltaTime)
{
	LastAppliedCap = GetMaxSpeed();
	LastAppliedMode = PackNetworkMovementMode();
	if (auto* Profile = bPredicting ? ActiveProfile(CharacterOwner) : nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SprintProfile_Owner_PredictionPhysics);
		const double Start = FPlatformTime::Seconds();
		const FVector Before = CharacterOwner->GetActorLocation();
		Super::PerformMovement(DeltaTime);
		const float Timestamp = GetPredictionData_Client_Character()->CurrentTimeStamp;
		const int32 MoveEpoch = OwnerEpoch + (LastOwnerTimestamp - Timestamp > MinTimeBetweenTimeStampResets * 0.5f);
		Profile->RecordTiming(ESprintProfileEvent::PredictionPhysics, Start, FPlatformTime::Seconds(), Timestamp, MoveEpoch);
		const auto* Subject = CastChecked<ASprintTrialCharacter>(CharacterOwner);
		const auto* Policy = Subject->GetSprintPolicy();
		Profile->Record(*Subject, TEXT("OwnerAppliedMove"), Timestamp, MoveEpoch, Before, 0., Subject->TrialPhase,
			DeltaTime, Policy && Policy->IsRequested(), LastAppliedMode, -1, -1, LastAppliedCap);
		return;
	}
	Super::PerformMovement(DeltaTime);
}

void USprintObservedMovement::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 Flags, const FVector& NewAcceleration)
{
	if (ReplayMoves) { ++*ReplayMoves; }
	const bool bAuthority = CharacterOwner && CharacterOwner->HasAuthority();
	if (auto* Profile = (bAuthority || ReplayMoves) ? ActiveProfile(CharacterOwner) : nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SprintProfile_Server_Move);
		const FVector Before = CharacterOwner->GetActorLocation();
		const auto* Subject = CastChecked<ASprintTrialCharacter>(CharacterOwner);
		const auto* Policy = Subject->GetSprintPolicy();
		int32 Intent = -1;
		if (Policy && bAuthority)
		{
			Intent = Policy->Variant == ESprintTrialVariant::SeparateRpc ? Policy->IsRequested()
				: Policy->Variant == ESprintTrialVariant::MoveDataBit ? GetIndependentSprintIntent()
				: (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
		}
		LastAppliedCap = -1.;
		const double Start = FPlatformTime::Seconds();
		Super::MoveAutonomous(ClientTimeStamp, DeltaTime, Flags, NewAcceleration);
		if (bAuthority) { Profile->RecordTiming(ESprintProfileEvent::ServerMove, Start, FPlatformTime::Seconds(), ClientTimeStamp, ServerEpoch); }
		const int32 ReplayEpoch = OwnerEpoch - (GetCurrentReplayedSavedMove() && GetCurrentReplayedSavedMove()->bOldTimeStampBeforeReset ? 1 : 0);
		if (LastAppliedCap >= 0.)
		{
			Profile->Record(*Subject, bAuthority ? TEXT("ServerAppliedMove") : TEXT("OwnerReplayMove"),
				ClientTimeStamp, bAuthority ? ServerEpoch : ReplayEpoch, Before, FVector::Dist(Before, CharacterOwner->GetActorLocation()),
				-1, DeltaTime, Intent, LastAppliedMode, -1, -1, LastAppliedCap);
		}
		return;
	}
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, Flags, NewAcceleration);
}

void USprintObservedMovement::OnClientTimeStampResetDetected()
{
	Super::OnClientTimeStampResetDetected();
	++ServerEpoch;
	TrialResponse.Epoch = ServerEpoch;
}

void USprintObservedMovement::ServerMoveHandleClientError(float TimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* Base, FName BoneName, uint8 ClientMode)
{
	auto* Profile = ActiveProfile(CharacterOwner);
	if (!Profile)
	{
		Super::ServerMoveHandleClientError(TimeStamp, DeltaTime, Accel, RelativeClientLocation, Base, BoneName, ClientMode);
		return;
	}
	const auto* Before = GetPredictionData_Server_Character();
	const float PreviousAdjustment = Before->PendingAdjustment.TimeStamp;
	const bool bForce = Before->bForceClientUpdate;
	Super::ServerMoveHandleClientError(TimeStamp, DeltaTime, Accel, RelativeClientLocation, Base, BoneName, ClientMode);
	const auto* After = GetPredictionData_Server_Character();
	if (After == Before && PreviousAdjustment != TimeStamp && After->PendingAdjustment.TimeStamp == TimeStamp)
	{
		Profile->Record(*CastChecked<ASprintTrialCharacter>(CharacterOwner), TEXT("ServerResponseQueued"), TimeStamp, ServerEpoch,
			FVector::ZeroVector, After->PendingAdjustment.bAckGoodMove ? 1. : 0., -1, DeltaTime, -1, -1, ClientMode, bForce);
	}
}

bool USprintObservedMovement::ServerCheckClientError(float TimeStamp, float DeltaTime, const FVector& Accel,
	const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
	FMovementBaseInterfaceData* Base, FName BoneName, uint8 ClientMode)
{
	const bool bError = Super::ServerCheckClientError(TimeStamp, DeltaTime, Accel, ClientWorldLocation,
		RelativeClientLocation, Base, BoneName, ClientMode);
	if (const auto* Character = Cast<ASprintTrialCharacter>(CharacterOwner))
	{
		if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()))
		{
			Capture->Record(*Character, TEXT("ServerCheck"), TimeStamp, ServerEpoch, ClientWorldLocation, bError ? 1.0 : 0.0,
				-1, DeltaTime, -1, -1, ClientMode, 0);
		}
	}
	return bError;
}

void USprintObservedMovement::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp,
	FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* Base, FName BoneName,
	bool bHasBase, bool bBaseRelative, uint8 ServerMode, FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, Base, BoneName,
		bHasBase, bBaseRelative, ServerMode, ServerGravityDirection);
	if (const auto* Character = Cast<ASprintTrialCharacter>(CharacterOwner))
	{
		if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()); Capture && Capture->IsRecording())
		{
			CorrectionTimestamp = TimeStamp;
			CorrectionEpoch = OwnerEpoch - (ClientData.LastAckedMove.IsValid() && ClientData.LastAckedMove->bOldTimeStampBeforeReset ? 1 : 0);
			Capture->Record(*Character, TEXT("OwnerCorrection"), TimeStamp, CorrectionEpoch, NewLocation);
			if (Capture->IsProfiling(*Character))
			{
				const double Now = FPlatformTime::Seconds();
				Capture->RecordTiming(ESprintProfileEvent::Correction, Now, Now, TimeStamp, CorrectionEpoch);
			}
			if (!BeforeCorrection.IsSet())
			{
				BeforeCorrection = Character->GetActorLocation();
			}
		}
	}
}

bool USprintObservedMovement::ClientUpdatePositionAfterServerUpdate()
{
	const double Start = FPlatformTime::Seconds();
	bool bReplayed;
	int32 Moves = 0;
	auto* Profile = BeforeCorrection.IsSet() ? ActiveProfile(CharacterOwner) : nullptr;
	if (Profile)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SprintProfile_Owner_Reconciliation);
		TGuardValue<int32*> Counting(ReplayMoves, &Moves);
		bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
		if (bReplayed)
		{
			Profile->RecordTiming(ESprintProfileEvent::Replay, Start, FPlatformTime::Seconds(), CorrectionTimestamp, CorrectionEpoch, Moves);
		}
	}
	else
	{
		bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	}
	if (BeforeCorrection.IsSet())
	{
		if (const auto* Character = Cast<ASprintTrialCharacter>(CharacterOwner))
		{
			if (USprintTrialCapture* Capture = USprintTrialCapture::Find(GetWorld()))
			{
				Capture->Record(*Character, bReplayed ? TEXT("OwnerReplay") : TEXT("OwnerReconciled"),
					CorrectionTimestamp, CorrectionEpoch, BeforeCorrection.GetValue(), (FPlatformTime::Seconds() - Start) * 1000.0);
			}
		}
		BeforeCorrection.Reset();
	}
	return bReplayed;
}
