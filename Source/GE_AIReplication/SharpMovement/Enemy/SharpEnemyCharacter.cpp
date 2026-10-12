#include "SharpEnemyCharacter.h"

#include "SharpEnemyAIController.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "../Detector/SharpMovementDetectorComponent.h"
#include "../Net/SharpUpdatePolicy.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Movement/SharpCharacterMovementComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpEnemy, Log, All);

ASharpEnemyCharacter::ASharpEnemyCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<USharpCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	AIControllerClass = ASharpEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	SplineFollower = CreateDefaultSubobject<USharpSplineFollowerComponent>(TEXT("SplineFollower"));
	MovementRecorder = CreateDefaultSubobject<USharpMovementRecorderComponent>(TEXT("MovementRecorder"));
	MovementDetector = CreateDefaultSubobject<USharpMovementDetectorComponent>(TEXT("MovementDetector"));
	UpdatePolicy = CreateDefaultSubobject<USharpUpdatePolicyComponent>(TEXT("UpdatePolicy"));
	VisualPredictor = CreateDefaultSubobject<USharpVisualPredictorComponent>(TEXT("VisualPredictor"));
	DebugLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugLabel"));
	DebugLabel->SetupAttachment(RootComponent);
}

void ASharpEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		const UCharacterMovementComponent* CMC = GetCharacterMovement();
		UE_LOG(LogSharpEnemy, Display, TEXT("ActorId=%s NetHz=%.2f MinNetHz=%.2f AlwaysRelevant=%d Dormancy=%d Acceleration=%.2f Braking=%.2f GroundFriction=%.2f BrakingFactor=%.2f RotationRate=%s OrientToMovement=%d AirControl=%.2f GravityScale=%.2f Smoothing=%d"),
			*ActorId.ToString(), GetNetUpdateFrequency(), GetMinNetUpdateFrequency(), bAlwaysRelevant, static_cast<int32>(NetDormancy),
			CMC->MaxAcceleration, CMC->BrakingDecelerationWalking, CMC->GroundFriction, CMC->BrakingFrictionFactor,
			*CMC->RotationRate.ToString(), CMC->bOrientRotationToMovement, CMC->AirControl, CMC->GravityScale, static_cast<int32>(CMC->NetworkSmoothingMode));
	}
}

void ASharpEnemyCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DebugLabel->SetVisibility(bShowDebugLabel && GetNetMode() != NM_DedicatedServer);
	TimeSinceDiagnostic += DeltaSeconds;
	if (TimeSinceDiagnostic < FMath::Max(DiagnosticIntervalSeconds, KINDA_SMALL_NUMBER)) { return; }
	TimeSinceDiagnostic = 0.0f;
	const UCharacterMovementComponent* CMC = GetCharacterMovement();
	const FString RoleName = UEnum::GetValueAsString(GetLocalRole());
	const FString Display = FString::Printf(TEXT("%s | %s\nSpeed %.1f | Mode %d\n%s"), *ActorId.ToString(), *RoleName,
		CMC->Velocity.Size(), static_cast<int32>(CMC->MovementMode), *GetActorLocation().ToCompactString());
	if (bShowDebugLabel) { DebugLabel->SetText(FText::FromString(Display)); }
	if (bLogMovement && !ActorId.IsNone() && !ExperimentContext.Identity.RunId.IsEmpty())
	{
		const AGameStateBase* GameState = GetWorld()->GetGameState();
		const double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		UE_LOG(LogSharpEnemy, Display, TEXT("RunId=%s TrialId=%d Mode=%s PathId=%s PathRevision=%d ActorId=%s Role=%s NetMode=%d ServerTime=%.3f Position=%s Velocity=%s MovementMode=%d Mesh=%s Follow=%s Distance=%.2f Target=%.2f CrossTrack=%.2f"),
			*ExperimentContext.Identity.RunId, ExperimentContext.Identity.TrialId, *UEnum::GetValueAsString(ExperimentContext.Identity.Mode),
			*ExperimentContext.PathId.ToString(), ExperimentContext.PathRevision, *ActorId.ToString(), *RoleName, static_cast<int32>(GetNetMode()), ServerTime, *GetActorLocation().ToCompactString(),
			*CMC->Velocity.ToCompactString(), static_cast<int32>(CMC->MovementMode), *GetMesh()->GetComponentLocation().ToCompactString(),
			HasAuthority() ? *UEnum::GetValueAsString(SplineFollower->State) : TEXT("ServerOnly"),
			SplineFollower->ActualDistance, SplineFollower->TargetDistance, SplineFollower->CrossTrackError);
	}
}

void ASharpEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASharpEnemyCharacter, ActorId);
	DOREPLIFETIME(ASharpEnemyCharacter, ExperimentContext);
	DOREPLIFETIME(ASharpEnemyCharacter, ClientState);
	DOREPLIFETIME(ASharpEnemyCharacter, SharpMoveSnapshot);
}

void ASharpEnemyCharacter::PublishSnapshot(FSharpMoveSnapshot Snapshot)
{
	if (!HasAuthority()) { return; }
	Snapshot.SequenceId = ++SnapshotSequence;
	SharpMoveSnapshot = MoveTemp(Snapshot);
	MovementRecorder->RecordDiagnostic(TEXT("SnapshotPublished"), SharpMoveSnapshot.SequenceId, SharpMoveSnapshot.ServerTime, 0.0, 0.0, TEXT("LatestStateProperty"));
}

void ASharpEnemyCharacter::InvalidateSnapshot()
{
	if (!HasAuthority()) { return; }
	SharpMoveSnapshot = FSharpMoveSnapshot(); SharpMoveSnapshot.SequenceId = ++SnapshotSequence;
}

void ASharpEnemyCharacter::OnRep_ClientState()
{
	MovementRecorder->SynchronizeClientRecording();
}

void ASharpEnemyCharacter::OnRep_Snapshot()
{
	MovementRecorder->SynchronizeClientRecording();
	MovementRecorder->RecordDiagnostic(TEXT("SnapshotReceived"), SharpMoveSnapshot.SequenceId, SharpMoveSnapshot.ServerTime, 0.0, 0.0,
		SharpMoveSnapshot.bValid ? TEXT("Valid") : TEXT("Invalidated"));
	VisualPredictor->QueueSnapshot(SharpMoveSnapshot);
}

void ASharpEnemyCharacter::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement(); ++MovementGeneration;
	MovementRecorder->SynchronizeClientRecording();
	MovementRecorder->RecordDiagnostic(TEXT("MovementReceived"), 0, GetReplicatedServerLastTransformUpdateTimeStamp(),
		GetReplicatedMovement().LinearVelocity.Size(), 0.0, FString::Printf(TEXT("StandardReplicatedMovement Position=%s Velocity=%s TransformTimestampNotAlwaysVelocityTimestamp"),
			*GetReplicatedMovement().Location.ToString(), *GetReplicatedMovement().LinearVelocity.ToString()));
}

bool ASharpEnemyCharacter::PrepareAtStart(const FVector& FloorPosition, const FRotator& Rotation, float HeightOffset, FString& OutReason)
{
	if (!HasAuthority() || !HasActorBegunPlay() || FloorPosition.ContainsNaN() || Rotation.ContainsNaN() ||
		!FMath::IsFinite(HeightOffset) || HeightOffset < 0.0f)
	{
		OutReason = TEXT("Character preparation requires the server and a finite start transform");
		return false;
	}
	SplineFollower->Reset();
	ConsumeMovementInputVector();
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	CMC->StopMovementImmediately();
	CMC->ClearAccumulatedForces();
	FVector Location = FloorPosition;
	Location.Z += GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + HeightOffset;
	if (!TeleportTo(Location, Rotation))
	{
		OutReason = TEXT("Start position is blocked; adjust the path/floor/Start Height Offset");
		return false;
	}
	CMC->SetMovementMode(MOVE_Walking);
	TimeSinceDiagnostic = 0.0f;
	OutReason.Empty();
	return true;
}

#if ENABLE_VISUAL_LOG
void ASharpEnemyCharacter::GrabDebugSnapshot(FVisualLogEntry* Snapshot) const
{
	FVisualLogStatusCategory& Category = Snapshot->Status.AddDefaulted_GetRef();
	Category.Category = TEXT("Sharp Movement");
	Category.Add(TEXT("ActorId"), ActorId.ToString());
	Category.Add(TEXT("World"), GetWorld() ? GetWorld()->GetName() : TEXT("None"));
	Category.Add(TEXT("Role"), UEnum::GetValueAsString(GetLocalRole()));
	Category.Add(TEXT("RunId"), ExperimentContext.Identity.RunId);
	Category.Add(TEXT("TrialId"), FString::FromInt(ExperimentContext.Identity.TrialId));
	Category.Add(TEXT("Velocity"), GetCharacterMovement()->Velocity.ToString());
	Category.Add(TEXT("MovementMode"), FString::FromInt(GetCharacterMovement()->MovementMode));
	Category.Add(TEXT("FollowState"), UEnum::GetValueAsString(SplineFollower->State));
	Category.Add(TEXT("LastEventId"), FString::Printf(TEXT("%lld"), static_cast<long long>(MovementRecorder->LastEventId)));
}
#endif
