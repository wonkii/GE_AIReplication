#include "HitWallProbeSubsystem.h"

#include "HitWallProbeComponent.h"
#include "Combat/CombatHitReceiver.h"
#include "Combat/HitReactionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHitWallProbeCmd, Log, All);

namespace HitWallProbeCommands
{
	/** The authoritative world: this one unless it is a client, else the in-process PIE server. */
	static UWorld* AuthorityWorld(UWorld* World)
	{
		if (World && World->GetNetMode() != NM_Client) { return World; }
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client) { return Candidate; }
		}
		return nullptr;
	}

	/** First local player's pawn on the server (the listen-server host, or standalone player). */
	static APawn* AttackerPawn(UWorld& World)
	{
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get(); PC && PC->GetPawn()) { return PC->GetPawn(); }
		}
		return nullptr;
	}

	/** Nearest character with a hit reaction, other than the attacker. */
	static ACharacter* NearestTarget(UWorld& World, const AActor* From)
	{
		ACharacter* Best = nullptr; double BestDist = TNumericLimits<double>::Max();
		for (TActorIterator<ACharacter> It(&World); It; ++It)
		{
			if (*It == From || !It->FindComponentByClass<UHitReactionComponent>()) { continue; }
			const double D = From ? FVector::DistSquared(It->GetActorLocation(), From->GetActorLocation()) : 0.0;
			if (D < BestDist) { BestDist = D; Best = *It; }
		}
		return Best;
	}

	static void Report(const FString& Text, bool bOk)
	{
		if (bOk) { UE_LOG(LogHitWallProbeCmd, Display, TEXT("[HitWallProbe] %s"), *Text); }
		else { UE_LOG(LogHitWallProbeCmd, Warning, TEXT("[HitWallProbe] %s"), *Text); }
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, bOk ? FColor::Green : FColor::Yellow, TEXT("[HitWallProbe] ") + Text); }
	}
}

// hitwall.Hit : the nearest enemy receives one server hit from the player's position.
// Goes through the same ReceiveCombatHit path as a real attack, but the player plays
// no attack montage, so its root motion cannot follow and push the enemy.
static FAutoConsoleCommandWithWorldAndArgs GHitWallHit(
	TEXT("hitwall.Hit"),
	TEXT("Server-side fixed hit on the nearest enemy, from the player's direction. Player does not swing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		using namespace HitWallProbeCommands;
		UWorld* World = AuthorityWorld(InWorld);
		APawn* Attacker = World ? AttackerPawn(*World) : nullptr;
		ACharacter* Target = World ? NearestTarget(*World, Attacker) : nullptr;
		if (!Attacker || !Target) { Report(TEXT("Hit rejected: no server world, player pawn or enemy."), false); return; }
		UHitReactionComponent* Reaction = Target->FindComponentByClass<UHitReactionComponent>();
		static int32 NextAttackId = 900000;
		FCombatHit Hit; Hit.Attacker = Attacker; Hit.AttackId = ++NextAttackId;
		Hit.ImpactPoint = Target->GetActorLocation();
		const bool bAccepted = ICombatHitReceiver::Execute_ReceiveCombatHit(Reaction, Hit);
		Report(FString::Printf(TEXT("Hit %s on %s (distance %.0f cm)"), bAccepted ? TEXT("accepted") : TEXT("rejected (reacting, airborne or combat disabled)"),
			*Target->GetName(), FVector::Dist2D(Attacker->GetActorLocation(), Target->GetActorLocation())), bAccepted);
	}));

static TMap<TWeakObjectPtr<ACharacter>, FTransform> GHitWallHomes;

// hitwall.SetHome : remember where the nearest enemy stands now.
static FAutoConsoleCommandWithWorldAndArgs GHitWallSetHome(
	TEXT("hitwall.SetHome"),
	TEXT("Remember the nearest enemy's current transform as the trial start position."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		using namespace HitWallProbeCommands;
		UWorld* World = AuthorityWorld(InWorld);
		APawn* Player = World ? AttackerPawn(*World) : nullptr;
		ACharacter* Target = World ? NearestTarget(*World, Player) : nullptr;
		if (!Target) { Report(TEXT("SetHome rejected: no enemy."), false); return; }
		GHitWallHomes.Add(Target, Target->GetActorTransform());
		Report(FString::Printf(TEXT("Home of %s = %s"), *Target->GetName(), *Target->GetActorLocation().ToCompactString()), true);
	}));

// hitwall.Reset : put the nearest enemy back to its home (server; movement replication moves clients).
static FAutoConsoleCommandWithWorldAndArgs GHitWallReset(
	TEXT("hitwall.Reset"),
	TEXT("Teleport the nearest enemy back to the transform saved by hitwall.SetHome."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		using namespace HitWallProbeCommands;
		UWorld* World = AuthorityWorld(InWorld);
		APawn* Player = World ? AttackerPawn(*World) : nullptr;
		ACharacter* Target = World ? NearestTarget(*World, Player) : nullptr;
		const FTransform* Home = Target ? GHitWallHomes.Find(Target) : nullptr;
		if (!Home) { Report(TEXT("Reset rejected: run hitwall.SetHome first."), false); return; }
		const UHitReactionComponent* Reaction = Target->FindComponentByClass<UHitReactionComponent>();
		if (Reaction && Reaction->IsReacting()) { Report(TEXT("Reset rejected: enemy is still reacting."), false); return; }
		Target->SetActorLocationAndRotation(Home->GetLocation(), Home->GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Movement = Target->GetCharacterMovement()) { Movement->StopMovementImmediately(); }
		Target->ForceNetUpdate();
		Report(FString::Printf(TEXT("Reset %s to %s"), *Target->GetName(), *Home->GetLocation().ToCompactString()), true);
	}));

bool UHitWallProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Super::ShouldCreateSubsystem(Outer);
#endif
}

bool UHitWallProbeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHitWallProbeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SessionStamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	// Characters placed in the level (and already-spawned ones).
	for (TActorIterator<ACharacter> It(&InWorld); It; ++It)
	{
		AttachProbe(*It);
	}
	// Characters spawned later, including replicated spawns on clients.
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
}

void UHitWallProbeSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
	}
	SpawnHandle.Reset();
	Super::Deinitialize();
}

void UHitWallProbeSubsystem::HandleActorSpawned(AActor* Actor)
{
	AttachProbe(Actor);
}

void UHitWallProbeSubsystem::AttachProbe(AActor* Actor)
{
	ACharacter* Character = Cast<ACharacter>(Actor);
	if (!Character || !Character->FindComponentByClass<UHitReactionComponent>()) { return; }
	if (Character->FindComponentByClass<UHitWallProbeComponent>()) { return; }

	UHitWallProbeComponent* Probe = NewObject<UHitWallProbeComponent>(Character, TEXT("HitWallProbe"));
	Probe->SetSessionStamp(SessionStamp);
	Character->AddInstanceComponent(Probe);
	// If the actor has already begun play, RegisterComponent also calls BeginPlay.
	Probe->RegisterComponent();
}
