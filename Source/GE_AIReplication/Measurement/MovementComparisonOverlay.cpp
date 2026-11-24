#if WITH_EDITOR

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ObservedPlayerMovementComponent.h"
#include "RubberBandingSweep.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "Tickable.h"

namespace MovementComparison
{
	const TCHAR* Usage = TEXT("Movement.Compare view | enemy <ExperimentActorId> | status | off");
	const TCHAR* SprintUsage = TEXT("Movement.Sprint SeparateRpc | MoveData | Off | Status");

	ACharacter* FindAuthorityCharacter(const ACharacter& Local, const UWorld& Server)
	{
		const UNetDriver* LocalDriver = Local.GetWorld()->GetNetDriver();
		const UNetDriver* ServerDriver = Server.GetNetDriver();
		if (!LocalDriver || !ServerDriver || !LocalDriver->GetNetGuidCache() || !ServerDriver->GetNetGuidCache())
		{
			return nullptr;
		}

		// A possession change can put a different pawn under the same PlayerId.
		const FNetworkGUID Guid = LocalDriver->GetNetGuidCache()->GetNetGUID(&Local);
		const FNetGuidCacheObject* Entry = Guid.IsValid() ? ServerDriver->GetNetGuidCache()->GetCacheObject(Guid) : nullptr;
		ACharacter* Authority = Entry ? Cast<ACharacter>(Entry->Object.Get()) : nullptr;
		return IsValid(Authority) && Authority->GetWorld() == &Server && Authority->HasAuthority() ? Authority : nullptr;
	}

	const TCHAR* SprintModeName(EManualSprintMode Mode)
	{
		switch (Mode)
		{
		case EManualSprintMode::SeparateRpc: return TEXT("SeparateRpc");
		case EManualSprintMode::MoveData: return TEXT("MoveData");
		default: return TEXT("Off");
		}
	}

	FString DescribeSprint(const UObservedPlayerMovementComponent& Owner, const UObservedPlayerMovementComponent& Server)
	{
		const auto State = [](const UObservedPlayerMovementComponent& Movement) -> FString
		{
			if (!Movement.IsMovingOnGround()) { return Movement.GetMovementName(); }
			if (Movement.IsCrouching()) { return TEXT("Crouch"); }
			return Movement.IsManualSprintActive() ? TEXT("Sprint") : TEXT("Walk");
		};
		return FString::Printf(TEXT("%s | Owner %s %.0f/%.0f | Server %s %.0f/%.0f cm/s"),
			SprintModeName(Owner.GetManualSprintMode()), *State(Owner), Owner.Velocity.Size2D(), Owner.GetMaxSpeed(),
			*State(Server), Server.Velocity.Size2D(), Server.GetMaxSpeed());
	}

	UWorld* FindComparisonServer(const UWorld* World, FOutputDevice& Out)
	{
		UWorld* Server = nullptr;
		if (World && World->WorldType == EWorldType::PIE && World->GetNetMode() == NM_Client)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (!Candidate || Candidate->WorldType != EWorldType::PIE) { continue; }
				const ENetMode Mode = Candidate->GetNetMode();
				if (Mode != NM_ListenServer && Mode != NM_DedicatedServer) { continue; }
				if (Server) { Out.Log(TEXT("Select a PIE session with exactly one server.")); return nullptr; }
				Server = Candidate;
			}
		}
		if (!Server)
		{
			Out.Log(TEXT("Enter this command in a connected PIE client with Run Under One Process enabled."));
			return nullptr;
		}
		if (!World->GetNetDriver() || World->GetNetDriver()->IsUsingIrisReplication())
		{
			Out.Log(TEXT("This command needs a connected client using the project's NetGUID replication driver."));
			return nullptr;
		}
		return Server;
	}

	// One overlay follows the selected PIE viewport. All positions are read on the game thread.
	class FOverlay : public FTickableGameObject
	{
	public:
		FOverlay(UWorld* InClient, UWorld* InServer, ASharpEnemyCharacter* InEnemy)
			: Client(InClient), Server(InServer), Enemy(InEnemy), bFollowView(!InEnemy) {}

		virtual UWorld* GetTickableGameObjectWorld() const override { return Client.Get(); }
		virtual bool IsTickable() const override { return Client.IsValid() && Server.IsValid(); }
		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FOverlay, STATGROUP_Tickables); }

		FString Status = TEXT("Waiting for the first client frame.");

		virtual void Tick(float DeltaSeconds) override
		{
			UWorld* World = Client.Get();
			APlayerController* PC = World->GetFirstPlayerController();
			ACharacter* Local = bFollowView && PC ? Cast<ACharacter>(PC->GetViewTarget()) : Enemy.Get();
			if (!IsValid(Local))
			{
				Status = TEXT("Waiting for a character. Reselect the enemy if it has respawned.");
				return;
			}

			const ACharacter* Authority = FindAuthorityCharacter(*Local, *Server.Get());
			if (!Authority)
			{
				Status = TEXT("Waiting for the matching replicated character.");
				return;
			}

			const UCapsuleComponent* ServerCapsule = Authority->GetCapsuleComponent();
			const UCapsuleComponent* LocalCapsule = Local->GetCapsuleComponent();
			const FVector OriginDelta = FVector(Server->OriginLocation) - FVector(World->OriginLocation);
			const FVector ServerPos = ServerCapsule->GetComponentLocation() + OriginDelta;
			const FVector LocalPos = LocalCapsule->GetComponentLocation();
			auto DrawCapsule = [World](const UCapsuleComponent* Capsule, const FVector& Center, FColor Color)
			{
				DrawDebugCapsule(World, Center, Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(),
					Capsule->GetComponentQuat(), Color, false, 0.f, 0, 1.5f);
			};
			DrawCapsule(ServerCapsule, ServerPos, FColor::Green);
			DrawCapsule(LocalCapsule, LocalPos, FColor::Cyan);
			DrawDebugLine(World, ServerPos, LocalPos, FColor::Cyan, false, 0.f, 0, 1.5f);

			Status = FString::Printf(TEXT("%s | Server (green) to capsule (cyan): %.1f cm"),
				*Local->GetName(), FVector::Dist(ServerPos, LocalPos));
			if (const USkeletalMeshComponent* Mesh = Local->GetMesh(); Mesh && Mesh->GetSkeletalMeshAsset())
			{
				// Express the mesh origin at capsule-center height, as in SharpMovementRecorderComponent.
				const FVector MeshCenter = Mesh->GetComponentLocation()
					- LocalCapsule->GetComponentTransform().TransformVector(Local->GetBaseTranslationOffset());
				DrawDebugSphere(World, MeshCenter, 10.f, 12, FColor::Yellow, false, 0.f, 0, 2.f);
				DrawDebugLine(World, ServerPos, MeshCenter, FColor::Yellow, false, 0.f, 0, 1.f);
				Status += FString::Printf(TEXT(" | to mesh (yellow): %.1f cm"), FVector::Dist(ServerPos, MeshCenter));
			}
			const FVector LabelPos = LocalPos + FVector(0, 0, LocalCapsule->GetScaledCapsuleHalfHeight() + 20.f);
			DrawDebugString(World, LabelPos, Status, nullptr, FColor::White, 0.f, true);
			const auto* LocalMovement = Cast<UObservedPlayerMovementComponent>(Local->GetCharacterMovement());
			const auto* ServerMovement = Cast<UObservedPlayerMovementComponent>(Authority->GetCharacterMovement());
			if (Local->GetLocalRole() == ROLE_AutonomousProxy && LocalMovement && ServerMovement)
			{
				const FString SprintStatus = DescribeSprint(*LocalMovement, *ServerMovement);
				DrawDebugString(World, LabelPos + FVector(0, 0, 20.f), SprintStatus, nullptr, FColor::White, 0.f, true);
				Status += TEXT(" | ") + SprintStatus;
			}
		}

	private:
		TWeakObjectPtr<UWorld> Client;
		TWeakObjectPtr<UWorld> Server;
		TWeakObjectPtr<ASharpEnemyCharacter> Enemy;
		bool bFollowView;
	};

	TUniquePtr<FOverlay> Active;

	void Command(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
	{
		if (Args.Num() == 1 && Args[0] == TEXT("off"))
		{
			Active.Reset();
			Out.Log(TEXT("Movement comparison off."));
			return;
		}
		if (Args.Num() == 1 && Args[0] == TEXT("status"))
		{
			Out.Log(Active && Active->IsTickable() ? *Active->Status : TEXT("Movement comparison off."));
			return;
		}
		int32 ActorId = 0;
		const bool bEnemy = Args.Num() == 2 && Args[0] == TEXT("enemy") && Args[1].IsNumeric()
			&& LexTryParseString(ActorId, *Args[1]) && ActorId > 0;
		if (!bEnemy && !(Args.Num() == 1 && Args[0] == TEXT("view")))
		{
			Out.Log(Usage);
			return;
		}
		UWorld* Server = FindComparisonServer(World, Out);
		if (!Server) { return; }
		ASharpEnemyCharacter* Enemy = nullptr;
		if (bEnemy)
		{
			for (TActorIterator<ASharpEnemyCharacter> It(World); It; ++It)
			{
				if (It->ExperimentActorId != ActorId) { continue; }
				if (Enemy) { Out.Log(TEXT("Duplicate ExperimentActorId on this client.")); return; }
				Enemy = *It;
			}
			if (!Enemy) { Out.Log(TEXT("No replicated enemy with this ExperimentActorId on this client.")); return; }
		}
		Active = MakeUnique<FOverlay>(World, Server, Enemy);
		Out.Log(TEXT("Movement comparison on. Green = server, cyan = client capsule, yellow = client mesh center."));
	}

	void SprintCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
	{
		if (Args.Num() != 1)
		{
			Out.Log(SprintUsage);
			return;
		}
		const bool bStatus = Args[0] == TEXT("Status");
		TOptional<EManualSprintMode> Mode;
		for (const EManualSprintMode Candidate : {EManualSprintMode::Off,
			EManualSprintMode::SeparateRpc, EManualSprintMode::MoveData})
		{
			if (Args[0] == SprintModeName(Candidate)) { Mode = Candidate; break; }
		}
		if (!bStatus && !Mode.IsSet())
		{
			Out.Log(SprintUsage);
			return;
		}
		UWorld* Server = FindComparisonServer(World, Out);
		if (!Server) { return; }

		const APlayerController* PC = World->GetFirstPlayerController();
		const ACharacter* Local = PC && PC->IsLocalController() ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		const ACharacter* Authority = Local && Local->GetLocalRole() == ROLE_AutonomousProxy
			? FindAuthorityCharacter(*Local, *Server) : nullptr;
		auto* LocalMovement = Local ? Cast<UObservedPlayerMovementComponent>(Local->GetCharacterMovement()) : nullptr;
		auto* ServerMovement = Authority ? Cast<UObservedPlayerMovementComponent>(Authority->GetCharacterMovement()) : nullptr;
		if (!LocalMovement || !ServerMovement)
		{
			Out.Log(TEXT("Wait for your player to spawn in the owning client. This command uses the regular player character."));
			return;
		}
		if (!bStatus && LocalMovement->GetManualSprintMode() != Mode.GetValue())
		{
			if (IsRubberBandingSweepActive(World) || LocalMovement->IsInputReplayActive())
			{
				Out.Log(TEXT("Wait for the automated movement replay to finish before switching sprint modes."));
				return;
			}
			const uint32 Revision = LocalMovement->GetManualSprintModeRevision() + 1;
			ServerMovement->SetManualSprintMode(Mode.GetValue(), Revision);
			LocalMovement->SetManualSprintMode(Mode.GetValue(), Revision);
		}
		Out.Log(*DescribeSprint(*LocalMovement, *ServerMovement));
	}

	FAutoConsoleCommandWithWorldArgsAndOutputDevice CompareCommand(TEXT("Movement.Compare"), Usage,
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Command));
	FAutoConsoleCommandWithWorldArgsAndOutputDevice ManualSprintCommand(TEXT("Movement.Sprint"), SprintUsage,
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&SprintCommand));
}

#endif
