#include "SharpEnemyCharacter.h"
#include "SharpEnemyAIController.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Measurement/SharpMovementDiagnosticsComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Movement/SharpRandomWalkerComponent.h"
#include "../Net/SharpSnapshotChannelComponent.h"
#include "../Net/SharpUpdatePolicy.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Movement/SharpCharacterMovementComponent.h"
#include "Engine/ReplicatedState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/ReactionEnvironmentComponent.h"
#include "SharpCombatAdapterComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemyM1, Log, All);

ASharpEnemyCharacter::ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USharpCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	SetReplicates(true);
	SetReplicateMovement(true);
	AIControllerClass = ASharpEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	PrimaryActorTick.bCanEverTick = false;
	HitReaction = CreateDefaultSubobject<UHitReactionComponent>(TEXT("HitReaction"));
	ReactionEnvironment = CreateDefaultSubobject<UReactionEnvironmentComponent>(TEXT("ReactionEnvironment"));
	CombatAdapter = CreateDefaultSubobject<USharpCombatAdapterComponent>(TEXT("CombatAdapter"));
	UpdatePolicy = CreateDefaultSubobject<USharpUpdatePolicy>(TEXT("SharpUpdatePolicy"));
	SplineFollower = CreateDefaultSubobject<USharpSplineFollowerComponent>(TEXT("SplineFollower"));
	RandomWalker = CreateDefaultSubobject<USharpRandomWalkerComponent>(TEXT("RandomWalker"));
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
	DOREPLIFETIME(ASharpEnemyCharacter, ExperimentMovementSource);
}

void ASharpEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	// A replicated enemy may appear after Manager::BeginPlay/OnRep. Read the
	// latest resolved profile when this character starts, as well.
	for (TActorIterator<ASharpExperimentManager> It(GetWorld()); It; ++It)
	{
		ApplyOptimizationProfile(**It);
		break; // One experiment Manager per world.
	}
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
    // In-flight normal movement from the previous mode must never correct an
    // event-owned capsule. Mode/status replication can arrive in either order.
    if (Experiment && Experiment->Status.Generation > 0 &&
        (Experiment->Status.State == ESharpEnemyTrialState::Running || Experiment->Status.State == ESharpEnemyTrialState::Suspended) &&
        (Experiment->Status.Identity.Mode == ESharpExperimentMode::EventOnly ||
         Experiment->Status.Identity.Mode == ESharpExperimentMode::EventPredictor))
    {
        if (Recorder) { Recorder->RecordDiagnostic(TEXT("FRepMovementIgnored"), 0, 0, 0, 0, TEXT("EventTransportOwnsCapsule")); }
        return;
    }
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
