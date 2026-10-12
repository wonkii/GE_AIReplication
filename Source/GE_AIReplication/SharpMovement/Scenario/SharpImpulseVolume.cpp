#include "SharpImpulseVolume.h"
#include "SharpSplinePath.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

ASharpImpulseVolume::ASharpImpulseVolume()
{
	bReplicates = false;
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	// Defaults only. Box extent, transform and collision responses remain editable.
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

void ASharpImpulseVolume::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) { TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); return; }
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ASharpImpulseVolume::OnBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ASharpImpulseVolume::OnEndOverlap);
}

void ASharpImpulseVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disarm();
	TriggerBox->OnComponentBeginOverlap.RemoveDynamic(this, &ASharpImpulseVolume::OnBeginOverlap);
	TriggerBox->OnComponentEndOverlap.RemoveDynamic(this, &ASharpImpulseVolume::OnEndOverlap);
	Super::EndPlay(EndPlayReason);
}

bool ASharpImpulseVolume::ValidateConfiguration(FString& OutReason) const
{
	if (GetIsReplicated() || VolumeId.IsNone() || !TriggerBox->GetGenerateOverlapEvents() ||
		TriggerBox->GetCollisionEnabled() != ECollisionEnabled::QueryOnly ||
		TriggerBox->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Overlap ||
		TriggerBox->GetScaledBoxExtent().GetMin() <= 0.0)
	{
		OutReason = TEXT("Volume needs Replicates disabled, unique VolumeId, positive box extent, QueryOnly and Pawn Overlap with overlap events enabled");
		return false;
	}
	return Settings.IsValid(OutReason);
}

bool ASharpImpulseVolume::Prepare(ASharpExperimentManager* InManager, FString& OutReason)
{
	Disarm();
	if (!HasAuthority() || !IsValid(InManager) || !ValidateConfiguration(OutReason)) { return false; }
	Manager = InManager;
	TargetEnemy = InManager->Enemy;
	Path = InManager->Path;
	PreparedTrialId = InManager->Identity.TrialId;
	PreparedSettings = Settings;
	PreparedVolumeId = VolumeId;
	PreparedRotation = GetActorQuat();
	PreparedTransform = TriggerBox->GetComponentTransform();
	PreparedBoxExtent = TriggerBox->GetUnscaledBoxExtent();
	TriggerCount = 0;
	LastTriggerTime = -1.0;
	bInside = false;
	if (!IsValid(TargetEnemy) || !IsValid(Path) || ContainsTargetCapsule())
	{
		OutReason = TEXT("Volume target/path is invalid or the prepared start Capsule intersects the Volume");
		return false;
	}
	OutReason.Empty();
	return true;
}

bool ASharpImpulseVolume::ContainsTargetCapsule() const
{
	if (!IsValid(TargetEnemy)) { return false; }
	// Geometric query also works immediately after teleport before overlap lists settle.
	const UCapsuleComponent* Capsule = TargetEnemy->GetCapsuleComponent();
	return TriggerBox->OverlapComponent(Capsule->GetComponentLocation(), Capsule->GetComponentQuat(), Capsule->GetCollisionShape());
}

void ASharpImpulseVolume::Arm()
{
	if (HasAuthority() && IsValid(Manager) && Manager->Identity.TrialId == PreparedTrialId) { bArmed = true; }
}

bool ASharpImpulseVolume::IsPreparedGeometryUnchanged() const
{
	return TriggerBox->GetComponentTransform().Equals(PreparedTransform) && TriggerBox->GetUnscaledBoxExtent().Equals(PreparedBoxExtent) &&
		VolumeId == PreparedVolumeId;
}

void ASharpImpulseVolume::Disarm() { bArmed = false; }

FVector ASharpImpulseVolume::ResolveImpulse() const
{
	if (PreparedSettings.DirectionSpace == ESharpImpulseDirectionSpace::VolumeLocal)
	{
		return PreparedRotation.RotateVector(PreparedSettings.ImpulseVector);
	}
	if (PreparedSettings.DirectionSpace == ESharpImpulseDirectionSpace::PathTangent && IsValid(Path))
	{
		FVector Forward = Path->GetDirection(Path->ProjectDistance(TargetEnemy->GetActorLocation()));
		Forward *= TargetEnemy->SplineFollower->GetPreparedSettings().bReverseDirection ? -1.0f : 1.0f;
		Forward.Z = 0.0;
		Forward.Normalize();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		return Forward * PreparedSettings.ImpulseVector.X + Right * PreparedSettings.ImpulseVector.Y +
			FVector::UpVector * PreparedSettings.ImpulseVector.Z;
	}
	return PreparedSettings.ImpulseVector;
}

void ASharpImpulseVolume::OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !IsValid(TargetEnemy) || OtherActor != TargetEnemy || OtherComp != TargetEnemy->GetCapsuleComponent()) { return; }
	if (bInside) { return; }
	bInside = true;
	if (!bArmed || !IsValid(Manager) || !Manager->CanTriggerImpulse(this, PreparedTrialId) ||
		(PreparedSettings.TriggerPolicy == ESharpImpulseTriggerPolicy::OncePerTrial && TriggerCount > 0)) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (!IsPreparedGeometryUnchanged()) { Manager->FailImpulseTrial(TEXT("Volume geometry/identity changed after Prepare; reset the trial")); return; }
	if (LastTriggerTime >= 0.0 && Now - LastTriggerTime < PreparedSettings.RearmDelay) { return; }
	const FVector WorldImpulse = ResolveImpulse();
	if (WorldImpulse.ContainsNaN() || WorldImpulse.IsNearlyZero()) { Manager->FailImpulseTrial(TEXT("Resolved impulse is invalid")); return; }
	// Mark before callbacks; recursive overlaps cannot apply twice.
	++TriggerCount;
	LastTriggerTime = Now;
	const int64 EventId = TargetEnemy->MovementRecorder->BeginImpulse(PreparedVolumeId, WorldImpulse, PreparedSettings, Now);
	if (EventId == 0) { Manager->FailImpulseTrial(TEXT("Recorder could not reserve an impulse event")); return; }
	if (PreparedSettings.FollowPolicy == ESharpImpulseFollowPolicy::SuspendInput)
	{
		Manager->SuspendForImpulse(PreparedSettings.InputSuspendSeconds);
	}
	TargetEnemy->GetCharacterMovement()->AddImpulse(WorldImpulse, PreparedSettings.bVelocityChange);
	TargetEnemy->MovementRecorder->CompleteImpulseCall(EventId);
}

void ASharpImpulseVolume::OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (IsValid(TargetEnemy) && OtherActor == TargetEnemy && OtherComp == TargetEnemy->GetCapsuleComponent()) { bInside = false; }
}
