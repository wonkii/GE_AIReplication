#include "SharpEnemyCharacter.h"
#include "SharpEnemyAIController.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Measurement/SharpMovementDiagnosticsComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Net/SharpSnapshotChannelComponent.h"
#include "../Net/SharpUpdatePolicy.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Movement/SharpCharacterMovementComponent.h"
#include "Engine/ReplicatedState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyM1, Log, All);

ASharpEnemyCharacter::ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USharpCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	SetReplicates(true);
	SetReplicateMovement(true);
	AIControllerClass = ASharpEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	PrimaryActorTick.bCanEverTick = false;
	UpdatePolicy = CreateDefaultSubobject<USharpUpdatePolicy>(TEXT("SharpUpdatePolicy"));
	SplineFollower = CreateDefaultSubobject<USharpSplineFollowerComponent>(TEXT("SplineFollower"));
	Experiment = CreateDefaultSubobject<USharpEnemyExperimentComponent>(TEXT("Experiment"));
	Diagnostics = CreateDefaultSubobject<USharpMovementDiagnosticsComponent>(TEXT("Diagnostics"));
	SnapshotChannel = CreateDefaultSubobject<USharpSnapshotChannelComponent>(TEXT("SnapshotChannel"));
	Detector = CreateDefaultSubobject<USharpMovementDetectorComponent>(TEXT("Detector"));
	VisualPredictor = CreateDefaultSubobject<USharpVisualPredictorComponent>(TEXT("VisualPredictor"));
	Recorder = CreateDefaultSubobject<USharpMovementRecorderComponent>(TEXT("Recorder"));
	GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp = true;
}

void ASharpEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASharpEnemyCharacter, ExperimentActorId);
}

void ASharpEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const TCHAR* Mode = GetNetMode() == NM_Client ? TEXT("Client") :
		GetNetMode() == NM_DedicatedServer ? TEXT("DedicatedServer") :
		GetNetMode() == NM_ListenServer ? TEXT("ListenServer") : TEXT("Standalone");
	UE_LOG(LogSharpEnemyM1, Display, TEXT("[M1][Enemy] Actor=%s NetMode=%s Role=%s ActorId=%d Authority=%d Replicates=%d ReplicateMovement=%d NetUpdateHz=%.1f MinNetUpdateHz=%.1f Smoothing=%d"),
		*GetName(), Mode, GetLocalRole() == ROLE_SimulatedProxy ? TEXT("SimulatedProxy") : TEXT("Authority"),
		ExperimentActorId, HasAuthority(), GetIsReplicated(), IsReplicatingMovement(), GetNetUpdateFrequency(), GetMinNetUpdateFrequency(),
		Movement ? static_cast<int32>(Movement->NetworkSmoothingMode) : -1);
}

void ASharpEnemyCharacter::OnRep_ReplicatedMovement()
{
	const FRepMovement Received = GetReplicatedMovement();
	const FVector BeforePosition = GetActorLocation();
	const FVector BeforeVelocity = GetVelocity();
	const uint8 BeforeMode = static_cast<uint8>(GetCharacterMovement()->MovementMode);
	Super::OnRep_ReplicatedMovement();
	if (Diagnostics) { Diagnostics->ObserveMovementReceipt(Received, BeforePosition, BeforeVelocity, BeforeMode); }
	if (Recorder) { Recorder->ObserveReceipt(GetReplicatedServerLastTransformUpdateTimeStamp()); }
}

bool ASharpEnemyCharacter::PublishDetectedSharpMoveSnapshot(const FSharpMoveSnapshot& Snapshot)
{
	return SnapshotChannel && SnapshotChannel->Publish(Snapshot);
}
