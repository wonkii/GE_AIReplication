#include "SharpMovementDiagnosticsComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/ReplicatedState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpDiagnostics, Log, All);

USharpMovementDiagnosticsComponent::USharpMovementDiagnosticsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USharpMovementDiagnosticsComponent::BeginPlay()
{
	Super::BeginPlay();
	Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy && Enemy->HasAuthority())
	{
		Enemy->OnCharacterMovementUpdated.AddUniqueDynamic(this, &USharpMovementDiagnosticsComponent::ObserveServerMovement);
	}
	HandleSessionChanged();
}

void USharpMovementDiagnosticsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Enemy) { Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementDiagnosticsComponent::ObserveServerMovement); }
	Super::EndPlay(EndPlayReason);
}

void USharpMovementDiagnosticsComponent::HandleSessionChanged()
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy || !Enemy->Experiment) { return; }
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	if (Generation == Status.Generation && Segment == Status.Segment) { return; }
	Generation = Status.Generation; Segment = Status.Segment;
	ReceiveCount = IntervalSamples = CorrectionSamples = 0;
	TotalIntervalMs = MaxIntervalMs = TotalCorrectionCm = MaxCorrectionCm = 0;
	LastReceiveTime = LastServerLogTime = LastClientLogTime = -1;
	bSummaryLogged = bFinalSampleLogged = bHasMovementReceipt = false;
}

bool USharpMovementDiagnosticsComponent::IsObservationWindow() const
{
	if (!Enemy || !Enemy->Experiment || Enemy->Experiment->Status.ServerStartTime < 0) { return false; }
	const ESharpEnemyTrialState State = Enemy->Experiment->Status.State;
	return State == ESharpEnemyTrialState::Running || State == ESharpEnemyTrialState::Completed;
}

double USharpMovementDiagnosticsComponent::ServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : (GetOwner()->HasAuthority() ? GetWorld()->GetTimeSeconds() : -1.0);
}

FString USharpMovementDiagnosticsComponent::ContextString() const
{
	const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
	return FString::Printf(TEXT("RunId=%s TrialId=%d Mode=%s Generation=%d Segment=%d PathId=%s PathRevision=%d State=%s"),
		*Status.Identity.RunId, Status.Identity.TrialId,
		*StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Status.Identity.Mode)),
		Status.Generation, Status.Segment, *Status.PathId.ToString(), Status.PathRevision,
		*StaticEnum<ESharpEnemyTrialState>()->GetNameStringByValue(static_cast<int64>(Status.State)));
}

void USharpMovementDiagnosticsComponent::ObserveServerMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!bEnableLogging || !Enemy || !Enemy->HasAuthority() || !IsObservationWindow()) { return; }
	HandleSessionChanged();
	const bool bCompleted = Enemy->Experiment->Status.State == ESharpEnemyTrialState::Completed;
	if (bCompleted && bFinalSampleLogged) { return; }
	const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	const FVector DeltaVelocity = Movement->Velocity - OldVelocity;
	const double DeltaSpeed = DeltaVelocity.Size();
	const double Time = GetWorld()->GetTimeSeconds();
	const bool bLargeDelta = DeltaSpeed >= 300.0;
	if (!bCompleted && !bLargeDelta && LastServerLogTime >= 0 && Time - LastServerLogTime < FMath::Max(0.01f, LogIntervalSeconds)) { return; }
	LastServerLogTime = Time;
	const bool bAngleValid = OldVelocity.SizeSquared() > 1 && Movement->Velocity.SizeSquared() > 1;
	const double Angle = bAngleValid ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(OldVelocity.GetSafeNormal(), Movement->Velocity.GetSafeNormal()), -1.0, 1.0))) : -1.0;
	UE_LOG(LogSharpDiagnostics, Display, TEXT("[M2][CMC] Reason=%s Actor=%s ActorId=%d ServerTime=%.3f DeltaSeconds=%.4f OldPosition=%s PostPosition=%s OldVelocity=%s PostVelocity=%s DeltaVelocity=%.2f DeltaVelocityVector=%s DeltaVelocityRateCmPerSec2=%.2f AngleValid=%d DeltaAngleDeg=%.2f MovementMode=%d Distance=%.2f CrossTrack=%.2f %s"),
		bCompleted ? TEXT("Completed") : bLargeDelta ? TEXT("LargeDelta") : TEXT("Periodic"), *Enemy->GetName(), Enemy->ExperimentActorId,
		ServerTime(), DeltaSeconds, *OldLocation.ToCompactString(), *Enemy->GetActorLocation().ToCompactString(),
		*OldVelocity.ToCompactString(), *Movement->Velocity.ToCompactString(), DeltaSpeed, *DeltaVelocity.ToCompactString(),
		DeltaSeconds > SMALL_NUMBER ? DeltaSpeed / DeltaSeconds : -1.0, bAngleValid, Angle, static_cast<int32>(Movement->MovementMode),
		Enemy->SplineFollower->ActualDistance, Enemy->SplineFollower->CrossTrackError, *ContextString());
	if (bCompleted) { bFinalSampleLogged = true; }
}

void USharpMovementDiagnosticsComponent::ObserveMovementReceipt(const FRepMovement& Received,
	const FVector& BeforePosition, const FVector& BeforeVelocity, uint8 BeforeMode)
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy || Enemy->HasAuthority() || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !IsObservationWindow()) { return; }
	HandleSessionChanged();
	const double LocalTime = GetWorld()->GetTimeSeconds();
	const double IntervalMs = LastReceiveTime >= 0 ? (LocalTime - LastReceiveTime) * 1000.0 : -1.0;
	if (IntervalMs >= 0) { ++IntervalSamples; TotalIntervalMs += IntervalMs; MaxIntervalMs = FMath::Max(MaxIntervalMs, IntervalMs); }
	LastReceiveTime = LocalTime;
	++ReceiveCount;
	bHasMovementReceipt = true;
	const double Correction = FVector::Distance(BeforePosition, Enemy->GetActorLocation());
	// First receipt after a generation/segment change includes initial placement
	// and is excluded from correction statistics.
	if (ReceiveCount > 1) { ++CorrectionSamples; TotalCorrectionCm += Correction; MaxCorrectionCm = FMath::Max(MaxCorrectionCm, Correction); }
	if (!bEnableLogging || (ReceiveCount > 20 && ReceiveCount % 20 != 0)) { return; }
	UE_LOG(LogSharpDiagnostics, Display, TEXT("[M3][MovementOnRep] World=%s Actor=%s ActorId=%d Count=%llu LocalTime=%.3f ServerTime=%.3f ReceiveIntervalMs=%.3f CorrectionDistanceCm=%.3f ReceivedPosition=%s ReceivedVelocity=%s BeforeCapsule=%s AfterCapsule=%s BeforeVelocity=%s AfterVelocity=%s BeforeMode=%u AfterMode=%u %s"),
		*GetWorld()->GetPathName(), *Enemy->GetName(), Enemy->ExperimentActorId, static_cast<unsigned long long>(ReceiveCount), LocalTime, ServerTime(), IntervalMs,
		Correction, *Received.Location.ToCompactString(), *Received.LinearVelocity.ToCompactString(), *BeforePosition.ToCompactString(),
		*Enemy->GetActorLocation().ToCompactString(), *BeforeVelocity.ToCompactString(), *Enemy->GetVelocity().ToCompactString(), BeforeMode,
		static_cast<uint8>(Enemy->GetCharacterMovement()->MovementMode), *ContextString());
}

void USharpMovementDiagnosticsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnableLogging || !Enemy || Enemy->HasAuthority() || Enemy->GetLocalRole() != ROLE_SimulatedProxy || !IsObservationWindow()) { return; }
	HandleSessionChanged();
	if (!bHasMovementReceipt || ServerTime() < 0) { return; }
	const double LocalTime = GetWorld()->GetTimeSeconds();
	LogCompletionSummary(LocalTime);
	if (LastClientLogTime >= 0 && LocalTime - LastClientLogTime < FMath::Max(0.01f, LogIntervalSeconds)) { return; }
	LastClientLogTime = LocalTime;
	const FVector Capsule = Enemy->GetCapsuleComponent()->GetComponentLocation();
	const FVector Mesh = Enemy->GetMesh()->GetComponentLocation();
	UE_LOG(LogSharpDiagnostics, Display, TEXT("[M3][ClientVisual] World=%s Actor=%s ActorId=%d ReceiveCount=%llu LocalTime=%.3f ServerTime=%.3f DeltaSeconds=%.4f CapsulePosition=%s MeshPosition=%s MeshRelativePosition=%s CapsuleToMesh=%s Velocity=%s MovementMode=%d %s"),
		*GetWorld()->GetPathName(), *Enemy->GetName(), Enemy->ExperimentActorId, static_cast<unsigned long long>(ReceiveCount),
		LocalTime, ServerTime(), DeltaTime, *Capsule.ToCompactString(), *Mesh.ToCompactString(), *Enemy->GetMesh()->GetRelativeLocation().ToCompactString(),
		*(Mesh - Capsule).ToCompactString(), *Enemy->GetVelocity().ToCompactString(), static_cast<int32>(Enemy->GetCharacterMovement()->MovementMode), *ContextString());
}

void USharpMovementDiagnosticsComponent::LogCompletionSummary(double LocalTime)
{
	if (bSummaryLogged || Enemy->Experiment->Status.State != ESharpEnemyTrialState::Completed ||
		Enemy->GetVelocity().Size() > 5.0 || LastReceiveTime < 0 || LocalTime - LastReceiveTime < 0.5) { return; }
	bSummaryLogged = true;
	UE_LOG(LogSharpDiagnostics, Display, TEXT("[M3][BaselineSummary] World=%s Actor=%s ActorId=%d %s FinalReceiveCount=%llu IntervalSamples=%llu AvgIntervalMs=%.3f MaxIntervalMs=%.3f CorrectionSamples=%llu AvgCorrectionCm=%.3f MaxCorrectionCm=%.3f ReceiveQuietSeconds=%.3f"),
		*GetWorld()->GetPathName(), *Enemy->GetName(), Enemy->ExperimentActorId, *ContextString(),
		static_cast<unsigned long long>(ReceiveCount), static_cast<unsigned long long>(IntervalSamples),
		IntervalSamples > 0 ? TotalIntervalMs / IntervalSamples : -1.0, MaxIntervalMs, static_cast<unsigned long long>(CorrectionSamples),
		CorrectionSamples > 0 ? TotalCorrectionCm / CorrectionSamples : -1.0, MaxCorrectionCm, LocalTime - LastReceiveTime);
}
