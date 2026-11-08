#include "HitWallProbeSubsystem.h"

#include "HitWallProbeComponent.h"
#include "Combat/CombatHitReceiver.h"
#include "Combat/HitReactionComponent.h"
#include "Combat/ReactionWarpComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHitWallRun, Log, All);

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

	/** Same server path as a real attack (ReceiveCombatHit), without the attacker swinging. */
	static bool DeliverHit(AActor* Attacker, ACharacter* Target)
	{
		UHitReactionComponent* Reaction = Target ? Target->FindComponentByClass<UHitReactionComponent>() : nullptr;
		if (!Attacker || !Reaction) { return false; }
		static int32 NextAttackId = 900000;
		FCombatHit Hit; Hit.Attacker = Attacker; Hit.AttackId = ++NextAttackId;
		Hit.ImpactPoint = Target->GetActorLocation();
		return ICombatHitReceiver::Execute_ReceiveCombatHit(Reaction, Hit);
	}

	static void SetCVar(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) { Var->Set(*Value, ECVF_SetByConsole); }
	}

	static void Report(const FString& Text, bool bOk)
	{
		if (bOk) { UE_LOG(LogHitWallRun, Display, TEXT("[HitWallRun] %s"), *Text); }
		else { UE_LOG(LogHitWallRun, Warning, TEXT("[HitWallRun] %s"), *Text); }
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Green : FColor::Yellow, TEXT("[HitWallRun] ") + Text); }
	}

	static TArray<float> ParseFloats(const FString& Csv)
	{
		TArray<FString> Parts; Csv.ParseIntoArray(Parts, TEXT(","), true);
		TArray<float> Out; for (const FString& P : Parts) { Out.Add(FCString::Atof(*P)); }
		return Out;
	}
}

// hitwall.Hit : one manual fixed hit on the nearest enemy, from the player's direction.
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
		const bool bAccepted = DeliverHit(Attacker, Target);
		Report(FString::Printf(TEXT("Hit %s on %s"), bAccepted ? TEXT("accepted") : TEXT("rejected (reacting, airborne or combat disabled)"),
			*Target->GetName()), bAccepted);
	}));

// hitwall.Run gaps=250,80,40,10 warp=0,1 reps=10 standoff=200
static FAutoConsoleCommandWithWorldAndArgs GHitWallRun(
	TEXT("hitwall.Run"),
	TEXT("Automated trials. Stand the enemy in open space with a wall behind it (away from the player), then: ")
	TEXT("hitwall.Run gaps=250,80,40,10 warp=0,1 reps=10 standoff=200 [stations]. ")
	TEXT("With 'stations', every actor tagged HitWall_<Name> is used in turn (red arrow = retreat direction)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace HitWallProbeCommands;
		UWorld* World = AuthorityWorld(InWorld);
		UHitWallProbeSubsystem* Sub = World ? World->GetSubsystem<UHitWallProbeSubsystem>() : nullptr;
		if (!Sub) { Report(TEXT("Run rejected: no server world."), false); return; }
		UHitWallProbeSubsystem::FRunConfig Config;
		Config.GapsCm = {250.f, 80.f, 40.f, 10.f};
		Config.WarpModes = {0, 1};
		for (const FString& Arg : Args)
		{
			FString Key, Value;
			if (!Arg.Split(TEXT("="), &Key, &Value)) { continue; }
			if (Key == TEXT("gaps")) { Config.GapsCm = ParseFloats(Value); }
			else if (Key == TEXT("warp"))
			{
				Config.WarpModes.Reset();
				for (float W : ParseFloats(Value)) { Config.WarpModes.Add(W != 0.f ? 1 : 0); }
			}
			else if (Key == TEXT("reps")) { Config.Repeats = FMath::Max(1, FCString::Atoi(*Value)); }
			else if (Key == TEXT("standoff")) { Config.StandoffCm = FMath::Max(60.f, FCString::Atof(*Value)); }
		}
		FString Message;
		const bool bOk = Sub->StartRun(Config, Args.Contains(TEXT("stations")), Message);
		Report(Message, bOk);
	}));

static FAutoConsoleCommandWithWorldAndArgs GHitWallStop(
	TEXT("hitwall.Stop"),
	TEXT("Stop an automated hitwall.Run."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* InWorld)
	{
		UWorld* World = HitWallProbeCommands::AuthorityWorld(InWorld);
		if (UHitWallProbeSubsystem* Sub = World ? World->GetSubsystem<UHitWallProbeSubsystem>() : nullptr) { Sub->StopRun(TEXT("Stopped by user")); }
	}));

bool UHitWallProbeSubsystem::MeasureStation(UWorld& World, ACharacter& Target, APawn& PlayerPawn, FStation& Station,
	double MaxGap, FString& OutWhy) const
{
	const UCapsuleComponent* Capsule = Target.GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HitWallRunSweep), false, &Target);
	Params.AddIgnoredActor(&PlayerPawn);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	// Lift 2 cm so a slightly uneven floor does not count as the obstacle.
	const FVector Start = Station.Anchor + FVector(0.0, 0.0, 2.0);
	FHitResult Hit;
	if (!World.SweepSingleByChannel(Hit, Start, Start + Station.Back * 1500.0, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Params, Response))
	{
		OutWhy = TEXT("no obstacle within 15 m along the retreat direction"); return false;
	}
	if (Hit.bStartPenetrating) { OutWhy = TEXT("enemy capsule overlaps something at the station"); return false; }
	Station.WallDistance = (Hit.Location - Start).Size2D();
	if (Station.WallDistance < MaxGap + 1.0)
	{
		OutWhy = FString::Printf(TEXT("obstacle is %.0f cm away, need more than the largest gap %.0f cm"), Station.WallDistance, MaxGap);
		return false;
	}
	UE_LOG(LogHitWallRun, Display, TEXT("[HitWallRun] Station %s Anchor=%s Back=%s Obstacle=%s (%s) Normal=%s Distance=%.1f"),
		*Station.Name, *Station.Anchor.ToCompactString(), *Station.Back.ToCompactString(), *Hit.ImpactPoint.ToCompactString(),
		*GetNameSafe(Hit.GetActor()), *Hit.ImpactNormal.ToCompactString(), Station.WallDistance);
	return true;
}

bool UHitWallProbeSubsystem::StartRun(const FRunConfig& Config, bool bStations, FString& OutMessage)
{
	using namespace HitWallProbeCommands;
	UWorld* World = GetWorld();
	if (IsRunning()) { OutMessage = TEXT("Run rejected: already running (hitwall.Stop first)."); return false; }
	if (Config.GapsCm.IsEmpty() || Config.WarpModes.IsEmpty()) { OutMessage = TEXT("Run rejected: empty gaps or warp list."); return false; }
	APawn* PlayerPawn = World ? AttackerPawn(*World) : nullptr;
	ACharacter* Target = World ? NearestTarget(*World, PlayerPawn) : nullptr;
	if (!PlayerPawn || !Target) { OutMessage = TEXT("Run rejected: no player pawn or enemy."); return false; }
	const UHitReactionComponent* Reaction = Target->FindComponentByClass<UHitReactionComponent>();
	if (Reaction && Reaction->IsReacting()) { OutMessage = TEXT("Run rejected: enemy is reacting."); return false; }
	UReactionWarpComponent* Warp = Target->FindComponentByClass<UReactionWarpComponent>();
	if (!Warp && Config.WarpModes.Contains(1)) { OutMessage = TEXT("Run rejected: enemy has no ReactionWarpComponent for warp=1."); return false; }
	const double MaxGap = FMath::Max(Config.GapsCm);

	TArray<FStation> Candidates;
	if (!bStations)
	{
		FVector ToPlayer = PlayerPawn->GetActorLocation() - Target->GetActorLocation(); ToPlayer.Z = 0.0;
		if (!ToPlayer.Normalize()) { OutMessage = TEXT("Run rejected: player is on top of the enemy."); return false; }
		FStation Here; Here.Name = TEXT(""); Here.Anchor = Target->GetActorLocation(); Here.Back = -ToPlayer;
		Candidates.Add(Here);
	}
	else
	{
		// Stand the enemy on the floor under each station marker, at its current floor clearance.
		const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
		const double ClearanceZ = Capsule->GetScaledCapsuleHalfHeight() + 2.15;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			for (const FName& Tag : It->Tags)
			{
				const FString TagText = Tag.ToString();
				if (!TagText.StartsWith(TEXT("HitWall_"))) { continue; }
				FStation S; S.Name = TagText.RightChop(8);
				S.Back = It->GetActorForwardVector().GetSafeNormal2D();
				FHitResult Floor;
				FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(HitWallRunFloor), false, *It);
				FloorParams.AddIgnoredActor(Target); FloorParams.AddIgnoredActor(PlayerPawn);
				const FVector From = It->GetActorLocation() + FVector(0.0, 0.0, 100.0);
				if (S.Back.IsNearlyZero() || !World->LineTraceSingleByChannel(Floor, From, From - FVector(0.0, 0.0, 600.0), ECC_Visibility, FloorParams))
				{
					Report(FString::Printf(TEXT("Station %s skipped: no floor under the marker"), *S.Name), false);
					continue;
				}
				S.Anchor = FVector(It->GetActorLocation().X, It->GetActorLocation().Y, Floor.ImpactPoint.Z + ClearanceZ);
				Candidates.Add(S);
			}
		}
		Candidates.Sort([](const FStation& A, const FStation& B) { return A.Name < B.Name; });
		if (Candidates.IsEmpty()) { OutMessage = TEXT("Run rejected: no actor tagged HitWall_<Name>. Add a Target Point with that tag, red arrow toward the obstacle."); return false; }
	}

	Stations.Reset();
	for (FStation& S : Candidates)
	{
		FString Why;
		if (MeasureStation(*World, *Target, *PlayerPawn, S, MaxGap, Why)) { Stations.Add(S); }
		else { Report(FString::Printf(TEXT("Station %s skipped: %s"), S.Name.IsEmpty() ? TEXT("(here)") : *S.Name, *Why), false); }
	}
	if (Stations.IsEmpty()) { OutMessage = TEXT("Run rejected: no usable station (see messages above)."); return false; }

	Run = Config; Enemy = Target; Player = PlayerPawn;
	bOriginalWarp = Warp ? Warp->bEnableWarp : false;
	// Round robin, so slow drift (heat, streaming, GC) spreads over every condition.
	Trials.Reset();
	for (int32 Rep = 0; Rep < Run.Repeats; ++Rep)
		for (int32 StationIdx = 0; StationIdx < Stations.Num(); ++StationIdx)
			for (int32 Mode : Run.WarpModes)
				for (float Gap : Run.GapsCm)
					Trials.Add({Gap, Mode, Rep, StationIdx});
	TrialIndex = 0; Rejected = 0;

	SetCVar(TEXT("hitwall.Probe"), TEXT("1"));
	if (GEngine) { GEngine->Exec(World, TEXT("t.MaxFPS 60")); }
	PlaceForTrial();
	World->GetTimerManager().SetTimer(RunTimer, FTimerDelegate::CreateUObject(this, &ThisClass::TickRun), 0.02f, true);

	FString Names;
	for (const FStation& S : Stations) { Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + (S.Name.IsEmpty() ? FString(TEXT("here")) : S.Name); }
	OutMessage = FString::Printf(TEXT("Run started: %d trials (stations %d [%s] x gaps %d x warp %d x reps %d), enemy %s, hit by %s."),
		Trials.Num(), Stations.Num(), *Names, Run.GapsCm.Num(), Run.WarpModes.Num(), Run.Repeats, *Target->GetName(), *PlayerPawn->GetName());
	return true;
}

void UHitWallProbeSubsystem::SetWarp(bool bOn) const
{
	if (ACharacter* Target = Enemy.Get())
	{
		if (UReactionWarpComponent* Warp = Target->FindComponentByClass<UReactionWarpComponent>()) { Warp->bEnableWarp = bOn; }
	}
}

void UHitWallProbeSubsystem::PlaceForTrial()
{
	ACharacter* Target = Enemy.Get();
	APawn* PlayerPawn = Player.Get();
	if (!Target || !PlayerPawn) { StopRun(TEXT("Enemy or player was destroyed")); return; }
	const FTrial& T = Trials[TrialIndex];
	const FStation& S = Stations[T.Station];
	const FVector& Back = S.Back;

	// Gap = capsule surface to obstacle: the sweep distance is where the surface touches.
	FVector EnemyPos = S.Anchor + Back * (S.WallDistance - T.Gap);
	EnemyPos.Z = S.Anchor.Z;
	const FRotator FacePlayer(0.f, (-Back).Rotation().Yaw, 0.f);
	Target->SetActorLocationAndRotation(EnemyPos, FacePlayer, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Target->GetCharacterMovement()) { Movement->StopMovementImmediately(); }
	Target->ForceNetUpdate();

	FVector PlayerPos = EnemyPos - Back * Run.StandoffCm;
	PlayerPos.Z = PlayerPawn->GetActorLocation().Z;
	PlayerPawn->SetActorLocationAndRotation(PlayerPos, FRotator(0.f, Back.Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn))
	{
		if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement()) { Movement->StopMovementImmediately(); }
	}
	PlayerPawn->ForceNetUpdate();

	SetWarp(T.Warp != 0);
	const FString Prefix = S.Name.IsEmpty() ? FString() : S.Name + TEXT("_");
	HitWallProbeCommands::SetCVar(TEXT("hitwall.Label"), FString::Printf(TEXT("%sgap%03d_warp%d"), *Prefix, FMath::RoundToInt(T.Gap), T.Warp));
	Phase = EPhase::Settle;
	PhaseStart = GetWorld()->GetTimeSeconds();
}

void UHitWallProbeSubsystem::TickRun()
{
	using namespace HitWallProbeCommands;
	ACharacter* Target = Enemy.Get();
	APawn* PlayerPawn = Player.Get();
	UHitReactionComponent* Reaction = Target ? Target->FindComponentByClass<UHitReactionComponent>() : nullptr;
	if (!Target || !PlayerPawn || !Reaction) { StopRun(TEXT("Enemy or player was destroyed")); return; }
	const double Elapsed = GetWorld()->GetTimeSeconds() - PhaseStart;
	const auto Next = [this]()
	{
		if (++TrialIndex >= Trials.Num()) { StopRun(TEXT("Completed")); return; }
		PlaceForTrial();
	};

	switch (Phase)
	{
	case EPhase::Settle:
	{
		const UCharacterMovementComponent* Movement = Target->GetCharacterMovement();
		const bool bReady = !Reaction->IsReacting() && Movement && Movement->IsMovingOnGround();
		if (Elapsed >= Run.SettleSeconds && bReady)
		{
			if (DeliverHit(PlayerPawn, Target)) { Phase = EPhase::WaitEnd; PhaseStart = GetWorld()->GetTimeSeconds(); }
			else { ++Rejected; Report(FString::Printf(TEXT("Trial %d: hit rejected"), TrialIndex + 1), false); Next(); }
		}
		else if (Elapsed > 3.0)
		{
			++Rejected; Report(FString::Printf(TEXT("Trial %d: enemy did not settle"), TrialIndex + 1), false); Next();
		}
		break;
	}
	case EPhase::WaitEnd:
		if (!Reaction->IsReacting() || Elapsed > 5.0)
		{
			Phase = EPhase::Tail; PhaseStart = GetWorld()->GetTimeSeconds();
		}
		break;
	case EPhase::Tail:
		if (Elapsed >= Run.TailSeconds)
		{
			if ((TrialIndex + 1) % 10 == 0) { Report(FString::Printf(TEXT("%d / %d trials"), TrialIndex + 1, Trials.Num()), true); }
			Next();
		}
		break;
	default:
		break;
	}
}

void UHitWallProbeSubsystem::StopRun(const TCHAR* Reason)
{
	if (!IsRunning()) { return; }
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(RunTimer); }
	Phase = EPhase::Idle;
	SetWarp(bOriginalWarp);
	HitWallProbeCommands::SetCVar(TEXT("hitwall.Label"), TEXT(""));
	HitWallProbeCommands::Report(FString::Printf(TEXT("Run ended: %s. %d of %d trials done, %d rejected."),
		Reason, FMath::Min(TrialIndex, Trials.Num()), Trials.Num(), Rejected), Rejected == 0);
}

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
	for (TActorIterator<ACharacter> It(&InWorld); It; ++It)
	{
		AttachProbe(*It);
	}
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleActorSpawned));
}

void UHitWallProbeSubsystem::Deinitialize()
{
	StopRun(TEXT("World closed"));
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
	Probe->RegisterComponent();
}
