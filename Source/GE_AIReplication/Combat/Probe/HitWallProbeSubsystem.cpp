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
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/OverlapResult.h"
#include "Combat/Reaction/ReactionWarpArtifacts.h"
#include "Combat/Reaction/ReactionWarpTools.h"
#include "Misc/FileHelper.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"

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

namespace
{
bool ParsePlanMode(const FString& Text, EReactionWarpMode& Out)
{
    for (EReactionWarpMode M : {EReactionWarpMode::Legacy,EReactionWarpMode::Off,EReactionWarpMode::Uniform,EReactionWarpMode::MiddleOnly,EReactionWarpMode::MultiPhase})
        if (StaticEnum<EReactionWarpMode>()->GetNameStringByValue(int64(M)).Equals(Text,ESearchCase::IgnoreCase)) { Out=M; return true; }
    return false;
}
}
static FAutoConsoleCommandWithWorldAndArgs GReactionWarpMode(TEXT("ReactionWarp.Mode"),
    TEXT("ReactionWarp.Mode Off|Uniform|MiddleOnly|MultiPhase|Legacy [AllowDraft=0|1] [Library=/Game/...] [Policy=/Game/...]. Next hit only; authority world required."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        using namespace HitWallProbeCommands;
        // Deliberately do not guess another PIE server when invoked on a client.
        if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client || Args.IsEmpty())
        { Report(TEXT("ReactionWarp.Mode requires the explicit authority game/PIE world and mode."),false); return; }
        EReactionWarpMode Mode;
        if (!ParsePlanMode(Args[0],Mode)) { Report(TEXT("Unknown mode"),false); return; }
        if (auto* Sub=World->GetSubsystem<UHitWallProbeSubsystem>(); Sub && Sub->IsRunning()) { Report(TEXT("Stop the current comparison first."),false); return; }
        int32 Count=0;
        for (TActorIterator<ACharacter> It(World); It; ++It)
            if (auto* Warp=It->FindComponentByClass<UReactionWarpComponent>())
            {
                Warp->Mode=Mode; Warp->bEnableWarp=Mode!=EReactionWarpMode::Off; Warp->bAllowDraftSession=false;
                for (int32 I=1; I<Args.Num(); ++I)
                {
                    FString K,V; if (!Args[I].Split(TEXT("="),&K,&V)) continue;
                    if (K.Equals(TEXT("AllowDraft"),ESearchCase::IgnoreCase)) Warp->bAllowDraftSession=V==TEXT("1");
                    if (K.Equals(TEXT("Library"),ESearchCase::IgnoreCase)) Warp->Library=LoadObject<UHitWallReactionLibrary>(nullptr,*V);
                    if (K.Equals(TEXT("Policy"),ESearchCase::IgnoreCase)) Warp->Policy=LoadObject<UReactionWarpPolicy>(nullptr,*V);
                }
                ++Count;
                Report(FString::Printf(TEXT("World=%s Actor=%s NextMode=%s %s"),*World->GetName(),*It->GetName(),*Args[0],
                    Warp->bAllowDraftSession ? TEXT("DRAFT") : TEXT("REVIEWED policies required")),true);
            }
        if (!Count) Report(TEXT("No ReactionWarpComponent in this authority world."),false);
    }));

static FAutoConsoleCommandWithWorldAndArgs GReactionWarpCompare(TEXT("ReactionWarp.Compare"),
    TEXT("ReactionWarp.Compare gaps=250,80,40,10 modes=Off,Uniform,MiddleOnly,MultiPhase reps=3 [AllowDraft=1]. Explicit authority PIE/game world; uses configured Library/Policy. hitwall.Stop cancels."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        using namespace HitWallProbeCommands;
        if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client) { Report(TEXT("Compare requires explicit authority world."),false); return; }
        auto* Sub=World->GetSubsystem<UHitWallProbeSubsystem>(); if (!Sub) return;
        UHitWallProbeSubsystem::FRunConfig C; C.bMultiPhaseCompare=true; C.Repeats=3;
        C.GapsCm={250.f,80.f,40.f,10.f}; C.PlanModes={EReactionWarpMode::Off,EReactionWarpMode::Uniform,EReactionWarpMode::MiddleOnly,EReactionWarpMode::MultiPhase};
        for (const auto& Arg : Args)
        {
            FString K,V; if (!Arg.Split(TEXT("="),&K,&V)) { Report(TEXT("Expected key=value"),false); return; }
            if (K.Equals(TEXT("gaps"),ESearchCase::IgnoreCase)) C.GapsCm=ParseFloats(V);
            else if (K.Equals(TEXT("reps"),ESearchCase::IgnoreCase)) C.Repeats=FCString::Atoi(*V);
            else if (K.Equals(TEXT("AllowDraft"),ESearchCase::IgnoreCase)) C.bAllowDraft=V==TEXT("1");
            else if (K.Equals(TEXT("modes"),ESearchCase::IgnoreCase))
            {
                TArray<FString> Parts; V.ParseIntoArray(Parts,TEXT(","),true); C.PlanModes.Reset();
                for (const auto& Part : Parts) { EReactionWarpMode M; if (!ParsePlanMode(Part,M) || M==EReactionWarpMode::Legacy) { Report(TEXT("Invalid comparison mode"),false); return; } C.PlanModes.Add(M); }
            }
            else { Report(TEXT("Unknown comparison argument"),false); return; }
        }
        if (C.PlanModes.IsEmpty() || C.Repeats<1 || C.Repeats>100 || C.GapsCm.Num()*C.PlanModes.Num()*C.Repeats>4096)
        { Report(TEXT("Invalid comparison size"),false); return; }
        for (float Gap : C.GapsCm) if (!FMath::IsFinite(Gap) || Gap<0.f) { Report(TEXT("Invalid gap"),false); return; }
        C.WarpModes.Reset(); for (int32 I=0; I<C.PlanModes.Num(); ++I) C.WarpModes.Add(I);
        FString Message; const bool OK=Sub->StartRun(C,false,Message); Report(Message,OK);
    }));

// Explicit opt-in to xHitWall_ markers keeps legacy station selection unchanged.
namespace
{
void TourCommand(const TArray<FString>& Args, UWorld* World, bool bAudit)
{
    using namespace HitWallProbeCommands;
    if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client)
    { Report(TEXT("Tour requires the authority game/PIE console."),false); return; }
    UHitWallProbeSubsystem::FRunConfig C;
    C.bTour=true; C.bAudit=bAudit; C.bMultiPhaseCompare=true; C.Repeats=1;
    C.SettleSeconds=1.2f; C.TailSeconds=1.5f; C.GapsCm={120.f,100.f,80.f};
    EReactionWarpMode Mode=EReactionWarpMode::MultiPhase; bool Baseline=true;
    for (const FString& Arg : Args)
    {
        FString K,V; if (!Arg.Split(TEXT("="),&K,&V)) { Report(TEXT("Expected key=value"),false); return; }
        if (K.Equals(TEXT("mode"),ESearchCase::IgnoreCase))
        { if (!ParsePlanMode(V,Mode) || Mode==EReactionWarpMode::Legacy) { Report(TEXT("Invalid tour mode"),false); return; } }
        else if (K.Equals(TEXT("baseline"),ESearchCase::IgnoreCase))
        { if (!V.Equals(TEXT("Off"),ESearchCase::IgnoreCase) && !V.Equals(TEXT("None"),ESearchCase::IgnoreCase)) { Report(TEXT("baseline=Off|None"),false); return; } Baseline=V.Equals(TEXT("Off"),ESearchCase::IgnoreCase); }
        else if (K.Equals(TEXT("gaps"),ESearchCase::IgnoreCase))
        {
            TArray<FString> Parts; V.ParseIntoArray(Parts,TEXT(","),false); C.GapsCm.Reset();
            for (const FString& P : Parts) { if (!P.IsNumeric()) { Report(TEXT("Invalid gap"),false); return; } C.GapsCm.Add(FCString::Atof(*P)); }
        }
        else if (K.Equals(TEXT("reps"),ESearchCase::IgnoreCase)) { if (!V.IsNumeric()) return; C.Repeats=FCString::Atoi(*V); }
        else if (K.Equals(TEXT("IncludeDisabled"),ESearchCase::IgnoreCase)) C.bIncludeDisabled=V==TEXT("1");
        else if (K.Equals(TEXT("AllowDraft"),ESearchCase::IgnoreCase)) C.bAllowDraft=V==TEXT("1");
        else if (K.Equals(TEXT("camera"),ESearchCase::IgnoreCase)) C.bCamera=V==TEXT("1");
        else { Report(TEXT("Unknown tour argument"),false); return; }
    }
    if (Baseline && Mode!=EReactionWarpMode::Off) C.PlanModes.Add(EReactionWarpMode::Off);
    C.PlanModes.Add(Mode);
    for (int32 I=0; I<C.PlanModes.Num(); ++I) C.WarpModes.Add(I);
    FString Message;
    auto* Sub=World->GetSubsystem<UHitWallProbeSubsystem>();
    const bool OK=Sub && Sub->StartRun(C,true,Message); Report(Message,OK);
}
}
static FAutoConsoleCommandWithWorldAndArgs GTour(TEXT("ReactionWarp.Tour"),
    TEXT("Tour all tagged stations: mode=MultiPhase baseline=Off gaps=120,100,80 reps=1 IncludeDisabled=1 AllowDraft=1 camera=1. hitwall.Stop restores state."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){ TourCommand(A,W,false); }));
static FAutoConsoleCommandWithWorldAndArgs GStations(TEXT("ReactionWarp.Stations"),
    TEXT("Read-only station audit: IncludeDisabled=1 gaps=120,100,80. No hit or camera changes."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){ TourCommand(A,W,true); }));
static FAutoConsoleCommandWithWorldAndArgs GTourPause(TEXT("ReactionWarp.TourPause"),TEXT("Pause after the current reaction/tail."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* W){ if(W && W->IsGameWorld() && W->GetNetMode()!=NM_Client) if(auto* S=W->GetSubsystem<UHitWallProbeSubsystem>()) S->PauseTour(true); }));
static FAutoConsoleCommandWithWorldAndArgs GTourResume(TEXT("ReactionWarp.TourResume"),TEXT("Resume the tour."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* W){ if(W && W->IsGameWorld() && W->GetNetMode()!=NM_Client) if(auto* S=W->GetSubsystem<UHitWallProbeSubsystem>()) S->PauseTour(false); }));

static FAutoConsoleCommandWithWorldAndArgs GReactionWarpNetTest(TEXT("ReactionWarp.NetTest"),
    TEXT("Print network verification status. Does not claim a configured peer fixture exists."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld*)
    { HitWallProbeCommands::Report(TEXT("BLOCKED: automatic owner correction/replay fixture unavailable. AI peer validation requires server/observer runtime_samples.csv matched by planHash and reaction_id; owner plans explicitly fallback."),false); }));

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
    Station.Normal=Hit.ImpactNormal; Station.Obstacle=GetNameSafe(Hit.GetActor());
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
	if (Config.Repeats<1 || Config.Repeats>100 || Config.GapsCm.IsEmpty() || Config.WarpModes.IsEmpty()) { OutMessage = TEXT("Run rejected: empty gaps or warp list."); return false; }
	for (float Gap : Config.GapsCm) if (!FMath::IsFinite(Gap) || Gap<0.f) { OutMessage=TEXT("Invalid gap"); return false; }
    APawn* PlayerPawn = World ? AttackerPawn(*World) : nullptr;
	ACharacter* Target = World ? NearestTarget(*World, PlayerPawn) : nullptr;
	if (!PlayerPawn || !Target) { OutMessage = TEXT("Run rejected: no player pawn or enemy."); return false; }
	const UHitReactionComponent* Reaction = Target->FindComponentByClass<UHitReactionComponent>();
	if (Reaction && Reaction->IsReacting()) { OutMessage = TEXT("Run rejected: enemy is reacting."); return false; }
	UReactionWarpComponent* Warp = Target->FindComponentByClass<UReactionWarpComponent>();
	if (!Warp && Config.WarpModes.Contains(1)) { OutMessage = TEXT("Run rejected: enemy has no ReactionWarpComponent for warp=1."); return false; }
    if (!Config.bAudit && Config.bMultiPhaseCompare && (!Warp || !Warp->Library || !Warp->Policy)) { OutMessage=TEXT("Configure Library and Policy on the target first (ReactionWarp.Mode)."); return false; }
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
				FString TagText = Tag.ToString();
                if (Config.bIncludeDisabled && TagText.StartsWith(TEXT("xHitWall_"))) TagText.RightChopInline(1);
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
		if (MeasureStation(*World, *Target, *PlayerPawn, S, Config.bTour ? 0. : MaxGap, Why)) { Stations.Add(S); }
		else { Report(FString::Printf(TEXT("Station %s skipped: %s"), S.Name.IsEmpty() ? TEXT("(here)") : *S.Name, *Why), false); if(Config.bTour) { S.Error=Why; Stations.Add(S); } }
	}
	if (Stations.IsEmpty()) { OutMessage = TEXT("Run rejected: no usable station (see messages above)."); return false; }

    TSet<FString> StationNames;
    for (const FStation& S : Stations)
    {
        if (StationNames.Contains(S.Name)) { OutMessage=TEXT("Duplicate station ID"); return false; }
        StationNames.Add(S.Name);
    }
    if (int64(Stations.Num())*Config.GapsCm.Num()*Config.WarpModes.Num()*Config.Repeats>4096)
    { OutMessage=TEXT("Too many trials (limit 4096)"); return false; }
    if (Config.bAudit)
    {
        OutMessage=FString::Printf(TEXT("Audit: %d loaded stations; IncludeDisabled=%d. No hit executed. See station lines in Output Log."),Stations.Num(),Config.bIncludeDisabled);
        return true;
    }
	Run = Config; Enemy = Target; Player = PlayerPawn;
	bOriginalWarp = Warp ? Warp->bEnableWarp : false;
    OriginalMode=Warp ? Warp->Mode : EReactionWarpMode::Legacy;
    if (!Config.bMultiPhaseCompare) WarpTaskDirectory.Reset();
    if (Config.bMultiPhaseCompare)
    {
        WarpTaskDirectory=ReactionWarpArtifacts::NewTask(Config.bTour ? TEXT("ReactionWarp.Tour") : TEXT("ReactionWarp.Compare"));
        if (WarpTaskDirectory.IsEmpty()) { OutMessage=TEXT("Cannot create task README"); return false; }
        OriginalMode=Warp->Mode; bOriginalDraft=Warp->bAllowDraftSession;
        OriginalEnemyTransform=Target->GetActorTransform(); OriginalPlayerTransform=PlayerPawn->GetActorTransform();
        OriginalLabel=IConsoleManager::Get().FindConsoleVariable(TEXT("hitwall.Label"))->GetString();
        OriginalProbe=IConsoleManager::Get().FindConsoleVariable(TEXT("hitwall.Probe"))->GetInt();
        OriginalFPS=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat();
        Warp->bAllowDraftSession=Config.bAllowDraft;
        WarpTrialRows.Reset();
        const FString Snapshot=UReactionWarpTools::Inspect(Warp->Library,Warp->Policy,100000.f,EReactionWarpMode::MultiPhase,Config.bAllowDraft);
        FFileHelper::SaveStringToFile(Snapshot,*(WarpTaskDirectory/TEXT("input.json")));
    }
	if (Config.bTour)
    {
        FString Manifest=TEXT("station,obstacle,normal_x,normal_y,normal_z,wall_distance_cm,error\n");
        for(const auto& S : Stations) Manifest+=FString::Printf(TEXT("%s,%s,%.5f,%.5f,%.5f,%.3f,%s\n"),
            *FSharpRecordingFiles::Csv(S.Name),*FSharpRecordingFiles::Csv(S.Obstacle),S.Normal.X,S.Normal.Y,S.Normal.Z,S.WallDistance,*FSharpRecordingFiles::Csv(S.Error));
        FFileHelper::SaveStringToFile(Manifest,*(WarpTaskDirectory/TEXT("stations.csv")));
    }
    // Round robin, so slow drift (heat, streaming, GC) spreads over every condition.
	Trials.Reset();
	for (int32 Rep = 0; Rep < Run.Repeats; ++Rep)
		for (int32 StationIdx = 0; StationIdx < Stations.Num(); ++StationIdx)
			for (int32 Mode : Run.WarpModes)
				for (float Gap : Run.GapsCm)
					Trials.Add({Gap, Mode, Rep, StationIdx});
    if(Config.bTour)
    {
        Trials.Reset();
        for(int32 SI=0; SI<Stations.Num(); ++SI) for(float Gap:Run.GapsCm)
            for(int32 Rep=0; Rep<Run.Repeats; ++Rep) for(int32 Mode:Run.WarpModes) Trials.Add({Gap,Mode,Rep,SI});
    }
	TrialIndex = 0; Rejected = 0;
    if (Run.bMultiPhaseCompare)
    {
        for (int32 I=0; I<Trials.Num(); ++I)
        {
            const auto& T=Trials[I];
            WarpTrialRows.Add(FString::Printf(TEXT("%d,%.3f,%s,%d,0,Pending,,,%s"),I,T.Gap,*StaticEnum<EReactionWarpMode>()->GetNameStringByValue(int64(Run.PlanModes[T.Warp])),T.Rep,*FSharpRecordingFiles::Csv(Stations[T.Station].Name)));
        }
        FFileHelper::SaveStringToFile(TEXT("trial,gap_cm,mode,rep,reaction_id,status,reason,plan_hash,station\n")+FString::Join(WarpTrialRows,TEXT("\n"))+TEXT("\n"),*(WarpTaskDirectory/TEXT("trials.csv")));
        Report(TEXT("Compare artifacts: ")+WarpTaskDirectory,true);
    }

	SetCVar(TEXT("hitwall.Probe"), TEXT("1"));
	if (GEngine) { GEngine->Exec(World, TEXT("t.MaxFPS 60")); }
    bTourPaused=false;
    if(Config.bTour && Config.bCamera)
    {
        auto* PC=Cast<APlayerController>(PlayerPawn->GetController());
        if(PC && PC->IsLocalController())
        {
            ViewController=PC; OriginalViewTarget=PC->GetViewTarget();
            TourCamera=World->SpawnActor<ACameraActor>();
            if(TourCamera.IsValid()) { TourCamera->GetCameraComponent()->SetFieldOfView(55.f); PC->SetViewTarget(TourCamera.Get()); }
            PC->SetIgnoreMoveInput(true); PC->SetIgnoreLookInput(true); bInputLocked=true;
        }
    }
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
    PlacementError=S.Error;
    if(Run.bTour && S.WallDistance<T.Gap+1.) PlacementError=TEXT("RequestedGapExceedsStationSpace");
	const FVector& Back = S.Back;

	// Gap = capsule surface to obstacle: the sweep distance is where the surface touches.
	FVector EnemyPos = S.Anchor + Back * (S.WallDistance - T.Gap);
	EnemyPos.Z = S.Anchor.Z;
    auto Ground=[&](APawn* Pawn,FVector& Position)->bool
    {
        auto* C=Cast<ACharacter>(Pawn); if(!C) return false;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(TourFloor),false,Target); Q.AddIgnoredActor(PlayerPawn);
        FHitResult H;
        if(!GetWorld()->LineTraceSingleByChannel(H,Position+FVector(0,0,100),Position-FVector(0,0,600),ECC_Visibility,Q) || !C->GetCharacterMovement()->IsWalkable(H)) return false;
        Position.Z=H.ImpactPoint.Z+C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15;
        FCollisionResponseParams Response; C->GetCapsuleComponent()->InitSweepCollisionParams(Q,Response);
        return !GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,C->GetCapsuleComponent()->GetCollisionObjectType(),C->GetCapsuleComponent()->GetCollisionShape(),Q,Response);
    };
    if(Run.bTour && PlacementError.IsEmpty() && !Ground(Target,EnemyPos)) PlacementError=TEXT("EnemyFloorOrOverlap");
	const FRotator FacePlayer(0.f, (-Back).Rotation().Yaw, 0.f);
	if(PlacementError.IsEmpty()) Target->SetActorLocationAndRotation(EnemyPos, FacePlayer, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Target->GetCharacterMovement()) { Movement->StopMovementImmediately(); }
	Target->ForceNetUpdate();

	FVector PlayerPos = EnemyPos - Back * Run.StandoffCm;
	PlayerPos.Z = PlayerPawn->GetActorLocation().Z;
    if(Run.bTour && PlacementError.IsEmpty() && !Ground(PlayerPawn,PlayerPos)) PlacementError=TEXT("PlayerFloorOrOverlap");
	if(PlacementError.IsEmpty()) PlayerPawn->SetActorLocationAndRotation(PlayerPos, FRotator(0.f, Back.Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn))
	{
		if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement()) { Movement->StopMovementImmediately(); }
	}
	PlayerPawn->ForceNetUpdate();

    bTrialRecorded=false;
    if (Run.bMultiPhaseCompare)
    {
        if (auto* Warp=Target->FindComponentByClass<UReactionWarpComponent>())
        { Warp->Mode=Run.PlanModes[T.Warp]; Warp->bEnableWarp=Warp->Mode!=EReactionWarpMode::Off; }
    }
    else
    {
        if (auto* Warp=Target->FindComponentByClass<UReactionWarpComponent>()) Warp->Mode=EReactionWarpMode::Legacy;
        SetWarp(T.Warp != 0);
    }
	const FString Prefix = S.Name.IsEmpty() ? FString() : S.Name + TEXT("_");
	HitWallProbeCommands::SetCVar(TEXT("hitwall.Label"), FString::Printf(TEXT("%sgap%03d_warp%d"), *Prefix, FMath::RoundToInt(T.Gap), T.Warp));
    if(Run.bTour && TourCamera.IsValid() && PlacementError.IsEmpty())
    {
        const FVector Focus=EnemyPos+Back*60.f;
        const FVector Side=FVector::CrossProduct(FVector::UpVector,Back);
        const FVector CameraPos=Focus+Side*500.f-Back*120.f+FVector(0,0,150);
        TourCamera->SetActorLocationAndRotation(CameraPos,(Focus-CameraPos).Rotation());
    }
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
    if(Run.bTour && GEngine && Trials.IsValidIndex(TrialIndex))
    {
        const auto& T=Trials[TrialIndex];
        const auto& P=Reaction->GetWarpPlan();
        GEngine->AddOnScreenDebugMessage(0x485754,0.1f,FColor::Cyan,FString::Printf(TEXT("%d/%d %s gap=%.0f %s | %s %s"),
            TrialIndex+1,Trials.Num(),*Stations[T.Station].Name,T.Gap,*StaticEnum<EReactionWarpMode>()->GetNameStringByValue(int64(Run.PlanModes[T.Warp])),
            bTourPaused ? TEXT("PAUSE after trial") : Phase==EPhase::Settle ? TEXT("Preparing") : *StaticEnum<EReactionWarpResult>()->GetNameStringByValue(int64(P.Result)),Phase==EPhase::Settle ? *PlacementError : *P.Reason));
    }
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
        if(!PlacementError.IsEmpty()) { RecordWarpTrial(TEXT("Skipped"),PlacementError); Report(PlacementError,false); Next(); break; }
		const UCharacterMovementComponent* Movement = Target->GetCharacterMovement();
		const bool bReady = !Reaction->IsReacting() && Movement && Movement->IsMovingOnGround();
		if (Elapsed >= Run.SettleSeconds && bReady)
		{
            if(Run.bTour)
            {
                FStation Actual=Stations[Trials[TrialIndex].Station]; Actual.Anchor=Target->GetActorLocation();
                FString Why;
                const bool Measured=MeasureStation(*GetWorld(),*Target,*PlayerPawn,Actual,0.,Why);
                const auto& Expected=Stations[Trials[TrialIndex].Station];
                if(!Measured || Actual.Obstacle!=Expected.Obstacle || FMath::Abs(Actual.WallDistance-Trials[TrialIndex].Gap)>5.)
                { RecordWarpTrial(TEXT("Skipped"),Measured ? TEXT("PlacementDriftOrDifferentObstacle") : Why); Next(); break; }
                const FString Path=WarpTaskDirectory/TEXT("placements.csv");
                FString Row=IFileManager::Get().FileExists(*Path) ? FString() : TEXT("trial,station,requested_gap_cm,measured_gap_cm,normal_x,normal_y,normal_z\n");
                Row+=FString::Printf(TEXT("%d,%s,%.3f,%.3f,%.5f,%.5f,%.5f\n"),TrialIndex,*FSharpRecordingFiles::Csv(Expected.Name),Trials[TrialIndex].Gap,Actual.WallDistance,Actual.Normal.X,Actual.Normal.Y,Actual.Normal.Z);
                FFileHelper::SaveStringToFile(Row,*Path,FFileHelper::EEncodingOptions::AutoDetect,&IFileManager::Get(),FILEWRITE_Append);
            }
			if (DeliverHit(PlayerPawn, Target)) { Phase = EPhase::WaitEnd; PhaseStart = GetWorld()->GetTimeSeconds(); }
			else { ++Rejected; RecordWarpTrial(TEXT("Rejected"),TEXT("HitRejected")); Report(FString::Printf(TEXT("Trial %d: hit rejected"), TrialIndex + 1), false); Next(); }
		}
		else if (Elapsed > 3.0)
		{
			++Rejected; RecordWarpTrial(TEXT("Rejected"),TEXT("DidNotSettle")); Report(FString::Printf(TEXT("Trial %d: enemy did not settle"), TrialIndex + 1), false); Next();
		}
		break;
	}
	case EPhase::WaitEnd:
		if (!Reaction->IsReacting() || Elapsed > 5.0)
		{
            if (Elapsed>5.0) { ++Rejected; Reaction->CancelReaction(); RecordWarpTrial(TEXT("Failed"),TEXT("Timeout")); }
            else RecordWarpTrial(Reaction->GetWarpPlan().Result==EReactionWarpResult::Fallback ? TEXT("Fallback") : TEXT("Completed"),Reaction->GetWarpPlan().Reason);
			Phase = EPhase::Tail; PhaseStart = GetWorld()->GetTimeSeconds();
		}
		break;
	case EPhase::Tail:
		if (Elapsed >= Run.TailSeconds && !bTourPaused)
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
    if(auto* PC=ViewController.Get())
    {
        if(OriginalViewTarget.IsValid()) PC->SetViewTarget(OriginalViewTarget.Get());
        if(bInputLocked) { PC->SetIgnoreMoveInput(false); PC->SetIgnoreLookInput(false); }
    }
    bInputLocked=false; ViewController.Reset(); OriginalViewTarget.Reset();
    if(TourCamera.IsValid()) TourCamera->Destroy(); TourCamera.Reset();
    if (Run.bMultiPhaseCompare)
    {
        RecordWarpTrial(TEXT("Cancelled"),Reason);
        for (int32 I=FMath::Max(0,TrialIndex+1); I<WarpTrialRows.Num(); ++I) WarpTrialRows[I].ReplaceInline(TEXT(",Pending,,"),TEXT(",Cancelled,RunStopped,"));
        FFileHelper::SaveStringToFile(TEXT("trial,gap_cm,mode,rep,reaction_id,status,reason,plan_hash,station\n")+FString::Join(WarpTrialRows,TEXT("\n"))+TEXT("\n"),*(WarpTaskDirectory/TEXT("trials.csv")));
        if (auto* Target=Enemy.Get())
        {
            if (auto* R=Target->FindComponentByClass<UHitReactionComponent>()) R->CancelReaction();
            Target->SetActorTransform(OriginalEnemyTransform,false,nullptr,ETeleportType::TeleportPhysics);
            Target->GetCharacterMovement()->StopMovementImmediately(); Target->ForceNetUpdate();
            if (auto* W=Target->FindComponentByClass<UReactionWarpComponent>()) { W->Mode=OriginalMode; W->bAllowDraftSession=bOriginalDraft; }
        }
        if (auto* Pawn=Player.Get())
        {
            Pawn->SetActorTransform(OriginalPlayerTransform,false,nullptr,ETeleportType::TeleportPhysics); Pawn->ForceNetUpdate();
            if (auto* C=Cast<ACharacter>(Pawn)) C->GetCharacterMovement()->StopMovementImmediately();
        }
        HitWallProbeCommands::SetCVar(TEXT("hitwall.Probe"),FString::FromInt(OriginalProbe));
        HitWallProbeCommands::SetCVar(TEXT("hitwall.Label"),OriginalLabel);
        HitWallProbeCommands::SetCVar(TEXT("t.MaxFPS"),FString::SanitizeFloat(OriginalFPS));
        FFileHelper::SaveStringToFile(FString(TEXT("\nOutcome: "))+Reason+TEXT(". Completed is execution only, not visual approval. Owner fixture BLOCKED.\n"),
            *(WarpTaskDirectory/TEXT("README.md")),FFileHelper::EEncodingOptions::AutoDetect,&IFileManager::Get(),FILEWRITE_Append);
        FFileHelper::SaveStringToFile(TEXT("{\"naturalness\":\"user evaluation\",\"owner_fixture\":\"BLOCKED\",\"execution\":\"stopped; see every row of trials.csv\"}"),
            *(WarpTaskDirectory/TEXT("validation.json")));
    }
	Phase = EPhase::Idle;
	SetWarp(bOriginalWarp);
    if (auto* Target=Enemy.Get()) if (auto* Warp=Target->FindComponentByClass<UReactionWarpComponent>()) Warp->Mode=OriginalMode;
    if (!Run.bMultiPhaseCompare) HitWallProbeCommands::SetCVar(TEXT("hitwall.Label"), TEXT(""));
	HitWallProbeCommands::Report(FString::Printf(TEXT("Run ended: %s. %d of %d trials done, %d rejected."),
		Reason, FMath::Min(TrialIndex, Trials.Num()), Trials.Num(), Rejected), Rejected == 0);
}

void UHitWallProbeSubsystem::RecordWarpTrial(const FString& Status, const FString& Reason)
{
    if (!Run.bMultiPhaseCompare || bTrialRecorded || !Trials.IsValidIndex(TrialIndex)) return;
    bTrialRecorded=true;
    const auto& T=Trials[TrialIndex]; const auto* Target=Enemy.Get();
    const auto* Reaction=Target ? Target->FindComponentByClass<UHitReactionComponent>() : nullptr;
    const bool HasPlan=Reaction && Status!=TEXT("Rejected") && (Phase==EPhase::WaitEnd || Phase==EPhase::Tail);
    const FReactionWarpPlan Plan=HasPlan ? Reaction->GetWarpPlan() : FReactionWarpPlan();
    WarpTrialRows[TrialIndex]=FString::Printf(TEXT("%d,%.3f,%s,%d,%d,%s,%s,%s,%s"),TrialIndex,T.Gap,
        *StaticEnum<EReactionWarpMode>()->GetNameStringByValue(int64(Run.PlanModes[T.Warp])),T.Rep,
        HasPlan ? Reaction->GetReactionId() : 0,*Status,*FSharpRecordingFiles::Csv(Reason),*Plan.PlanHash,*FSharpRecordingFiles::Csv(Stations[T.Station].Name));
    if (HasPlan && Status!=TEXT("Cancelled"))
    {
        ReactionWarpArtifacts::SavePlan(WarpTaskDirectory,FString::Printf(TEXT("plan-%04d"),TrialIndex),Plan);
        if (const auto* Warp=Target ? Target->FindComponentByClass<UReactionWarpComponent>() : nullptr)
        {
            FHitWallReactionCandidate C; FString Error;
            if (Warp->ResolveCandidate(C,Error))
            {
                const FString Path=WarpTaskDirectory/TEXT("planned_trajectory.csv");
                FString Text=IFileManager::Get().FileExists(*Path) ? FString() : TEXT("trial,reaction_id,time,scale,raw_x,raw_y,planned_x,planned_y\n");
                for (int32 I=0; I<=180; ++I)
                {
                    const float Time=C.DurationSeconds*I/180.f;
                    const FVector Raw=ReactionWarp::Integrate(C.Trajectory,{},0.f,Time);
                    const FVector Q=ReactionWarp::Integrate(C.Trajectory,Plan.Keys,0.f,Time);
                    Text+=FString::Printf(TEXT("%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),TrialIndex,Plan.ReactionId,Time,ReactionWarp::ScaleAt(Plan.Keys,Time),Raw.X,Raw.Y,Q.X,Q.Y);
                }
                FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::AutoDetect,&IFileManager::Get(),FILEWRITE_Append);
            }
        }
    }
    FFileHelper::SaveStringToFile(TEXT("trial,gap_cm,mode,rep,reaction_id,status,reason,plan_hash,station\n")+FString::Join(WarpTrialRows,TEXT("\n"))+TEXT("\n"),*(WarpTaskDirectory/TEXT("trials.csv")));
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
