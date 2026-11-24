#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SprintTrialCharacter.generated.h"

class UAnimInstance;
class USkeletalMesh;

UENUM()
enum class ESprintTrialVariant : uint8
{
	SeparateRpc = 1, MoveData, MoveDataBit
};

UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class USprintTrialPolicy : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr float WalkSpeed = 350.f;
	static constexpr float RunSpeed = 650.f;

	void Request(bool bRun);
	void Apply(bool bRun);
	void AcceptTransition(bool bRun, int32 Serial);
	bool IsRequested() const { return bRequested; }
	int32 GetTransition() const { return Transition; }
	ESprintTrialVariant Variant = ESprintTrialVariant::SeparateRpc;

private:
	bool bRequested = false;
	int32 Transition = 0;
};

USTRUCT()
struct FSprintTrialPresentation
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY()
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY()
	FTransform MeshOffset;
};

UCLASS(Abstract)
class ASprintTrialCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit ASprintTrialCharacter(const FObjectInitializer& Initializer);
	USprintTrialPolicy* GetSprintPolicy() const { return SprintPolicy; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& View) override;
	bool InitializePresentation(const ACharacter& Original);
	bool HasTrialPresentation() const;

	TOptional<FMinimalViewInfo> TrialCamera;
	int32 TrialPhase = INDEX_NONE;

	UPROPERTY(Replicated)
	FGuid TrialId;

	UFUNCTION(Server, Reliable)
	void ServerRequestSprint(bool bRun, int32 Serial);

protected:
	virtual void OnRep_ReplicatedMovement() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USprintTrialPolicy> SprintPolicy;

private:
	UPROPERTY(ReplicatedUsing = OnRep_TrialPresentation)
	FSprintTrialPresentation TrialPresentation;

	UFUNCTION()
	void OnRep_TrialPresentation();

	bool bPresentationApplied = false;
};

UCLASS()
class USprintObservedMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	explicit USprintObservedMovement(const FObjectInitializer& Initializer);
	void RecordOwnerMove(float Timestamp, bool bRun);
	void EnableIndependentSprintBit();
	bool IsLastAppliedSprint() const { return LastAppliedCap == USprintTrialPolicy::RunSpeed; }

protected:
	bool GetIndependentSprintIntent() const;
	virtual void CallServerMovePacked(const FSavedMove_Character* NewMove, const FSavedMove_Character* PendingMove,
		const FSavedMove_Character* OldMove) override;
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;
	virtual void PerformMovement(float DeltaTime) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 Flags, const FVector& NewAcceleration) override;
	virtual void OnClientTimeStampResetDetected() override;
	virtual void ServerMoveHandleClientError(float TimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, FMovementBaseInterfaceData* Base, FName BoneName, uint8 ClientMode) override;
	virtual bool ServerCheckClientError(float TimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* Base, FName BoneName, uint8 ClientMode) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp,
		FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* Base, FName BoneName,
		bool bHasBase, bool bBaseRelative, uint8 ServerMode, FVector GravityDirection) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;

private:
	struct FMeasuredMove : FCharacterNetworkMoveData
	{
		virtual void ClientFillNetworkMoveData(const FSavedMove_Character& Move, ENetworkMoveType Type) override;
		virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map, ENetworkMoveType Type) override;
		int64 FirstBit = 0, EndBit = 0;
		bool bUseSprintBit = false;
		bool bWantsSprint = false;
	};

	struct FMeasuredMoveContainer : FCharacterNetworkMoveDataContainer
	{
		FMeasuredMoveContainer();
		virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
		FMeasuredMove Moves[3];
		int32 Epoch = 0;
		int32 AckFlags = -1;
		uint32 Serial = 0;
	};

	struct FMeasuredResponse : FCharacterMoveResponseDataContainer
	{
		virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* Map) override;
		int32 Epoch = 0;
		uint32 Serial = 0;
	};

	FMeasuredMoveContainer TrialMoveData;
	FMeasuredResponse TrialResponse;
	int32 ServerEpoch = 0;
	int32 OwnerEpoch = 0;
	float LastOwnerTimestamp = 0.f;
	TOptional<FVector> BeforeCorrection;
	float CorrectionTimestamp = 0.f;
	int32 CorrectionEpoch = 0;
	bool bPredicting = false;
	double LastAppliedCap = -1.;
	int32 LastAppliedMode = -1;
	int32* ReplayMoves = nullptr;
};

UCLASS()
class ASprintCmcWalkCharacter : public ASprintTrialCharacter
{
	GENERATED_BODY()

public:
	explicit ASprintCmcWalkCharacter(const FObjectInitializer& Initializer);
};

UCLASS()
class USprintStateMovement : public USprintObservedMovement
{
	GENERATED_BODY()

public:
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
};

UCLASS()
class ASprintRpcCharacter : public ASprintTrialCharacter
{
	GENERATED_BODY()

public:
	explicit ASprintRpcCharacter(const FObjectInitializer& Initializer);
};

UCLASS()
class ASprintMoveDataCharacter : public ASprintRpcCharacter
{
	GENERATED_BODY()

public:
	explicit ASprintMoveDataCharacter(const FObjectInitializer& Initializer);
};

UCLASS()
class ASprintMoveDataBitCharacter : public ASprintRpcCharacter
{
	GENERATED_BODY()

public:
	explicit ASprintMoveDataBitCharacter(const FObjectInitializer& Initializer);
};
