#include "SharpImpulseVolume.h"
#include "SharpSplinePath.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLogger.h"

ASharpImpulseVolume::ASharpImpulseVolume()
{
	bReplicates = false; PrimaryActorTick.bCanEverTick = false;
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox")); SetRootComponent(TriggerBox);
	TriggerBox->SetBoxExtent(FVector(100, 100, 150));
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore); TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

bool ASharpImpulseVolume::AppliesTo(const ASharpExperimentManager* SessionManager) const
{
	return !Manager || Manager == SessionManager;
}

bool ASharpImpulseVolume::ValidateStart(const ASharpEnemyCharacter& Enemy, const FTransform& Start, FString& OutReason) const
{
	if (GetIsReplicated() || VolumeId.IsNone() || !TriggerBox->GetGenerateOverlapEvents() ||
		TriggerBox->GetCollisionEnabled() != ECollisionEnabled::QueryOnly || TriggerBox->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Overlap ||
		TriggerBox->GetScaledBoxExtent().GetMin() <= 0 || !Settings.IsValid(OutReason))
	{
		OutReason = GetName() + TEXT(": use a unique VolumeId, positive extent, QueryOnly, Pawn Overlap and valid settings"); return false;
	}
	if (!Enemy.GetCapsuleComponent()->GetGenerateOverlapEvents()) { OutReason = TEXT("Enemy Capsule must generate overlap events for Impulse"); return false; }
	if (TriggerBox->OverlapComponent(Start.GetLocation(), Start.GetRotation(), Enemy.GetCapsuleComponent()->GetCollisionShape()))
	{
		OutReason = GetName() + TEXT(": Enemy start capsule intersects the Impulse Volume; move the Volume farther along the path"); return false;
	}
	return true;
}

bool ASharpImpulseVolume::PrepareEnemy(ASharpEnemyCharacter& Enemy, const FTransform& Start, FString& OutReason)
{
	if (!HasAuthority() || !ValidateStart(Enemy, Start, OutReason)) { return false; }
	for (auto It = States.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	FTriggerState State;
	State.Generation = Enemy.Experiment->Status.Generation; State.PreparedId = VolumeId;
	State.Transform = TriggerBox->GetComponentTransform(); State.Extent = TriggerBox->GetUnscaledBoxExtent(); State.Config = Settings;
	if (Settings.bUseExperimentImpulse) { State.Config.ImpulseVector = Enemy.Experiment->GetPreparedSettings()->Impulse; }
	if (State.Config.ImpulseVector.ContainsNaN() || State.Config.ImpulseVector.IsNearlyZero()) { OutReason = TEXT("Resolved Impulse must be finite and nonzero"); return false; }
	States.Add(&Enemy, State); return true;
}

void ASharpImpulseVolume::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_Client) { TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); return; }
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ASharpImpulseVolume::OnBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ASharpImpulseVolume::OnEndOverlap);
}

void ASharpImpulseVolume::OnBeginOverlap(UPrimitiveComponent* Component, AActor* Actor, UPrimitiveComponent* OtherComponent,
	int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(Actor);
	if (!HasAuthority() || !Enemy || OtherComponent != Enemy->GetCapsuleComponent()) { return; }
	FTriggerState* State = States.Find(Enemy);
	if (!State || State->Generation != Enemy->Experiment->Status.Generation || !Enemy->Experiment->IsExecuting() ||
		!AppliesTo(Enemy->Experiment->GetManager()) || State->bInside) { return; }
	State->bInside = true;
	FString Reason;
	if (!Enemy->Experiment->ValidateBinding(Reason)) { Enemy->Experiment->Fail(Reason); return; }
	if (VolumeId != State->PreparedId || !State->Transform.Equals(TriggerBox->GetComponentTransform()) || !State->Extent.Equals(TriggerBox->GetUnscaledBoxExtent()))
	{
		Enemy->Experiment->Fail(TEXT("Impulse Volume geometry or identity changed; reset Enemy")); return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const FSharpImpulseSettings& Config = State->Config;
	if ((Config.TriggerPolicy == ESharpImpulseTriggerPolicy::OncePerExecution && State->bTriggered) ||
		(State->LastTrigger >= 0 && Now - State->LastTrigger < Config.RearmDelay)) { return; }
	FVector Impulse = Config.ImpulseVector;
	if (Config.DirectionSpace == ESharpImpulseDirectionSpace::VolumeLocal) { Impulse = State->Transform.GetRotation().RotateVector(Impulse); }
	else if (Config.DirectionSpace == ESharpImpulseDirectionSpace::PathTangent)
	{
		const ASharpSplinePath* Path = Enemy->Experiment->GetPath();
		FVector Forward = Path->GetDirection(Path->ProjectDistance(Enemy->GetActorLocation())); Forward.Z = 0; Forward.Normalize();
		if (Enemy->SplineFollower->IsMovingReverse()) { Forward *= -1; }
		Impulse = Forward * Impulse.X + FVector::CrossProduct(FVector::UpVector, Forward) * Impulse.Y + FVector::UpVector * Impulse.Z;
	}
	if (Impulse.ContainsNaN() || Impulse.IsNearlyZero()) { Enemy->Experiment->Fail(TEXT("Invalid resolved Impulse")); return; }
	State->bTriggered = true; State->LastTrigger = Now;
	if (Config.FollowPolicy == ESharpImpulseFollowPolicy::SuspendInput && !Enemy->Experiment->SuspendForImpulse(Config.InputSuspendSeconds)) { return; }
	const uint32 EventId = Enemy->Recorder->BeginImpulse(State->PreparedId, Impulse, Config.bVelocityChange, Config.bIsSharpEvent);
	Enemy->GetCharacterMovement()->AddImpulse(Impulse, Config.bVelocityChange);
	Enemy->Recorder->CompleteImpulse(EventId);
	if (Enemy->Experiment->Status.Recording.bDrawDebug)
	{
		DrawDebugDirectionalArrow(GetWorld(), Enemy->GetActorLocation(), Enemy->GetActorLocation() + Impulse * 0.2, 30, FColor::Red, false, 1);
	}
	if (Enemy->Experiment->Status.Recording.bVisualLogger)
	{
		UE_VLOG_SEGMENT(Enemy, LogTemp, Log, Enemy->GetActorLocation(), Enemy->GetActorLocation() + Impulse * 0.2, FColor::Red, TEXT("Impulse %s Event=%u"), *State->PreparedId.ToString(), EventId);
	}
}

void ASharpImpulseVolume::OnEndOverlap(UPrimitiveComponent* Component, AActor* Actor, UPrimitiveComponent* OtherComponent, int32 BodyIndex)
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(Actor);
	if (Enemy && OtherComponent == Enemy->GetCapsuleComponent()) { if (FTriggerState* State = States.Find(Enemy)) { State->bInside = false; } }
}

void ASharpImpulseVolume::EndPlay(const EEndPlayReason::Type Reason)
{
	States.Reset(); Super::EndPlay(Reason);
}
