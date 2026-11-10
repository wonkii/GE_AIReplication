#if !UE_BUILD_SHIPPING
#include "Combat/HitReactionComponent.h"
#include "Combat/ReactionWarpComponent.h"
#include "Combat/ReactionEnvironmentComponent.h"
#include "Combat/ReactionMultiPhaseWarpModifier.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "MotionWarpingComponent.h"
#include "Combat/Probe/HitWallProbeSubsystem.h"
#include "Misc/FileHelper.h"

namespace
{
struct FReactionWarpSmoke : TSharedFromThis<FReactionWarpSmoke>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UGameInstance> GameInstance;
    TWeakObjectPtr<ASharpEnemyCharacter> Enemy;
    TWeakObjectPtr<ATargetPoint> Attacker;
    TWeakObjectPtr<AStaticMeshActor> Wall;
    TWeakObjectPtr<AStaticMeshActor> Corner;
    FString Role;
    bool bFailed=false, bPeerPassed=false;
    bool bCompare=false;
    int32 Stage=0, Trial=0, LastPeerId=0, PeerPlans=0;
    double Began=FPlatformTime::Seconds(), StageTime=0.;
    FVector Start, Direction, ReactionStart;
    float Natural=0.f;
    FString LastPlanHash;

    bool Check(bool OK,const TCHAR* Reason)
    {
        if (!OK) { UE_LOG(LogTemp,Error,TEXT("[ReactionWarpSmoke] FAIL %s Stage=%d Trial=%d Role=%s"),Reason,Stage,Trial,*Role); bFailed=true; }
        return OK;
    }
    double Elapsed() const { return World->GetTimeSeconds()-StageTime; }
    void Next(int32 NewStage) { Stage=NewStage; StageTime=World->GetTimeSeconds(); }
    AStaticMeshActor* Box(const FVector& Position,const FVector& Scale)
    {
        auto* Actor=World->SpawnActor<AStaticMeshActor>(Position,FRotator::ZeroRotator);
        auto* Mesh=Actor->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Actor->SetActorScale3D(Scale); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetMobility(EComponentMobility::Static); Actor->SetReplicates(true); Mesh->SetIsReplicated(true);
        return Actor;
    }
    int32 ActiveModifiers() const
    {
        int32 Count=0;
        if (auto* W=Enemy->FindComponentByClass<UMotionWarpingComponent>())
            for (auto* M : W->GetModifiers())
                if (M && M->IsA<UReactionMultiPhaseWarpModifier>() && M->GetState()!=ERootMotionModifierState::MarkedForRemoval && M->GetState()!=ERootMotionModifierState::Disabled) ++Count;
        return Count;
    }
    void MoveBox(AStaticMeshActor* Actor,const FVector& Location)
    {
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Actor->SetActorLocation(Location);
        Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
        Actor->ForceNetUpdate();
    }
    bool PeerTick()
    {
        World=GameInstance.IsValid() ? GameInstance->GetWorld() : nullptr;
        if (!World.IsValid()) return true;
        for (TActorIterator<ASharpEnemyCharacter> It(World.Get()); It; ++It)
        {
            const auto& P=It->HitReaction->GetWarpPlan();
            if (!It->HitReaction->IsReacting() || P.Result!=EReactionWarpResult::Planned || P.ReactionId==LastPeerId) continue;
            LastPeerId=P.ReactionId; ++PeerPlans;
            Check(It->HitReaction->GetPlayedReactionId()==P.ReactionId,TEXT("Peer consumed server plan"));
            Check(ReactionWarp::PlanHash(P)==P.PlanHash,TEXT("Peer canonical plan hash"));
            auto* W=It->FindComponentByClass<UMotionWarpingComponent>();
            int32 Count=0;
            if (W) for (auto* M : W->GetModifiers()) if (M && M->IsA<UReactionMultiPhaseWarpModifier>() && M->GetState()!=ERootMotionModifierState::MarkedForRemoval) ++Count;
            Check(Count==1,TEXT("Exactly one peer curve modifier"));
            UE_LOG(LogTemp,Display,TEXT("[ReactionWarpSmoke] PeerPlan Role=%s Id=%d Plan=%s"),*Role,P.ReactionId,*P.PlanHash);
            if (PeerPlans>=3 && !bFailed) { bPeerPassed=true; UE_LOG(LogTemp,Display,TEXT("[ReactionWarpSmoke] PASS %s plans=%d (AI observer only)"),*Role,PeerPlans); }
        }
        return true;
    }
    bool Tick(float)
    {
        if (FPlatformTime::Seconds()-Began>120.) Check(false,TEXT("Timeout"));
        if (bFailed) { FPlatformMisc::RequestExitWithStatus(false,1); return false; }
        if (Role!=TEXT("Server")) return PeerTick();
        if (!World.IsValid()) return false;
        if (Stage==0)
        {
            for (TActorIterator<ASharpEnemyCharacter> It(World.Get()); It; ++It) if (It->ExperimentActorId==1) Enemy=*It;
            if (!Enemy.IsValid() || !Enemy->HitReaction->bCombatEnabled) return true;
            if (World->GetNetMode()!=NM_Standalone && World->GetNumPlayerControllers()<2) return true;
            auto* Warp=Enemy->FindComponentByClass<UReactionWarpComponent>();
            if (!Check(Warp!=nullptr,TEXT("Warp component fixture"))) return true;
            Warp->Library=LoadObject<UHitWallReactionLibrary>(nullptr,TEXT("/Game/Combat/Analysis/DA_HitReaction_Front_PELT"));
            Warp->Policy=LoadObject<UReactionWarpPolicy>(nullptr,TEXT("/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft"));
            Warp->bAllowDraftSession=true; Warp->bEnableWarp=true; Warp->Mode=EReactionWarpMode::MultiPhase;
            FHitWallReactionCandidate C; FString Error;
            if (!Check(Warp->ResolveCandidate(C,Error),*Error)) return true;
            Enemy->HitReaction->HitMontage=Cast<UAnimMontage>(C.Animation.LoadSynchronous());
            Enemy->HitReaction->DirectionalHitMontages.Reset();
            Direction=Enemy->GetBaseRotationOffset().RotateVector(C.Trajectory.Last().RootMotion.GetTranslation()).GetSafeNormal2D();
            Natural=C.Trajectory.Last().RootMotion.GetTranslation().Size2D();
            Start=FVector(0.,0.,5000.+Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.15);
            Box(FVector(0.,0.,4950.),FVector(30.,30.,1.));
            Wall=Box(Start+Direction*1000.,FVector(.2,15.,4.));
            Corner=Box(Start+Direction*1300.,FVector(.2,.2,4.));
            Attacker=World->SpawnActor<ATargetPoint>(Start-Direction*500.,FRotator::ZeroRotator);
            Enemy->ReactionEnvironment->DetectionRadiusCm=1000.f;
            for (auto It=World->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC=It->Get();
                if (!PC->GetPawn() && World->GetNetMode()==NM_Standalone) World->GetAuthGameMode()->RestartPlayer(PC);
                if (APawn* Pawn=PC->GetPawn()) Pawn->SetActorLocation(Start-Direction*400.+FVector(0.,PC->GetUniqueID()%2 ? 150. : -150.,0.));
            }
            Next(1);
        }
        else if (Stage==1)
        {
            // Cases: clear, feasible compress, impossible capacity, cancel, replacement,
            // initial overlap and a separate path blocker (safe endpoint is insufficient).
            Enemy->HitReaction->CancelReaction();
            Enemy->SetActorLocationAndRotation(Start,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
            Enemy->GetCharacterMovement()->StopMovementImmediately(); Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Enemy->ForceNetUpdate();
            const float Gap=Trial==0 ? Natural+50.f : Trial==2 ? 10.f : Trial==5 ? -5.f : Natural*.8f+5.f;
            MoveBox(Wall.Get(),Start+Direction*(Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius()+Gap+10.f));
            MoveBox(Corner.Get(),Start+Direction*(Trial==6 ? Natural*.45 : 1300.));
            ReactionStart=Enemy->GetActorLocation(); Next(2);
        }
        else if (Stage==2 && Elapsed()>.4)
        {
            FCombatHit Hit; Hit.Attacker=Attacker.Get(); Hit.AttackId=930000+Trial; Hit.ImpactPoint=Start;
            if (!Check(ICombatHitReceiver::Execute_ReceiveCombatHit(Enemy->HitReaction,Hit),TEXT("Fixture hit accepted"))) return true;
            const auto& P=Enemy->HitReaction->GetWarpPlan();
            UE_LOG(LogTemp,Display,TEXT("[ReactionWarpSmoke] Trial=%d Result=%d Reason=%s Plan=%s Min=%.3f Budget=%.3f"),Trial,int32(P.Result),*P.Reason,*P.PlanHash,P.MinimumCm,P.BudgetCm);
            if (Trial==0) Check(P.Result==EReactionWarpResult::Unmodified,TEXT("Clear preserves original"));
            if (Trial==1 || Trial==3 || Trial==4) Check(P.Result==EReactionWarpResult::Planned && ActiveModifiers()==1,TEXT("Feasible compression installs one modifier"));
            if (Trial==2) Check(P.Result==EReactionWarpResult::Fallback,TEXT("Too close fallback"));
            if (Trial==5) Check(P.Result==EReactionWarpResult::Fallback && P.Reason.Contains(TEXT("Overlap")),TEXT("Initial overlap fallback"));
            if (Trial==6) Check(P.Result==EReactionWarpResult::Fallback || P.bPathChecked,TEXT("Intermediate blocker cannot bypass path sweep"));
            LastPlanHash=P.PlanHash; Next(3);
        }
        else if (Stage==3)
        {
            if ((Trial==3 || Trial==4) && Elapsed()>.2 && Enemy->HitReaction->IsReacting()) Enemy->HitReaction->CancelReaction();
            if (Elapsed()<1.2) return true;
            Check(!Enemy->HitReaction->IsReacting() && ActiveModifiers()==0,TEXT("End/cancel removes modifier"));
            if (Trial==1) Check(FVector::Dist2D(ReactionStart,Enemy->GetActorLocation())>20.,TEXT("Plan moves physical capsule"));
            // Trial 4 starts a new hit after a cancelled curve, without changing saved assets.
            if (++Trial<=6) Next(1);
            else Next(4);
        }
        else if (Stage==4 && Elapsed()>2.)
        {
            if (bCompare)
            {
                Enemy->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
                MoveBox(Wall.Get(),Start+Direction*1000.); MoveBox(Corner.Get(),Start+Direction*1300.);
                for (auto It=World->GetPlayerControllerIterator(); It; ++It)
                    if (APawn* Pawn=It->Get()->GetPawn()) Pawn->SetActorLocation(Start-Direction*400.);
                Next(5); return true;
            }
            UE_LOG(LogTemp,Display,TEXT("[ReactionWarpSmoke] PASS Server cases=7 naturalness=user owner=BLOCKED"));
            FPlatformMisc::RequestExitWithStatus(false,0); return false;
        }
        else if (Stage==5 && Elapsed()>.5)
        {
            auto* Sub=World->GetSubsystem<UHitWallProbeSubsystem>();
            UHitWallProbeSubsystem::FRunConfig Config; Config.bMultiPhaseCompare=true; Config.bAllowDraft=true; Config.Repeats=3;
            Config.GapsCm={250.f,80.f,40.f,10.f};
            Config.PlanModes={EReactionWarpMode::Off,EReactionWarpMode::Uniform,EReactionWarpMode::MiddleOnly,EReactionWarpMode::MultiPhase};
            Config.WarpModes={0,1,2,3};
            FString Error;
            if (Check(Sub && Sub->StartRun(Config,false,Error),*Error)) Next(6);
        }
        else if (Stage==6)
        {
            auto* Sub=World->GetSubsystem<UHitWallProbeSubsystem>();
            if (Sub->IsRunning()) return true;
            FString Text; FFileHelper::LoadFileToString(Text,*(Sub->GetWarpTaskDirectory()/TEXT("trials.csv")));
            TArray<FString> Rows; Text.ParseIntoArrayLines(Rows);
            Check(Rows.Num()==49 && !Text.Contains(TEXT(",Pending,")),TEXT("All 48 comparison trials recorded"));
            Check(Text.Contains(TEXT(",Fallback,")) && Text.Contains(TEXT(",Completed,")),TEXT("Separate fallback and completion results"));
            Check(Enemy->GetActorLocation().Equals(Start,1.),TEXT("Comparison restores placement"));
            Check(Enemy->FindComponentByClass<UReactionWarpComponent>()->Mode==EReactionWarpMode::MultiPhase,TEXT("Comparison restores mode"));
            if (!bFailed) { UE_LOG(LogTemp,Display,TEXT("[ReactionWarpSmoke] PASS Server cases=7 compare=48 naturalness=user owner=BLOCKED")); FPlatformMisc::RequestExitWithStatus(false,0); return false; }
        }
        return true;
    }
};

static FAutoConsoleCommandWithWorldAndArgs GReactionWarpSmoke(TEXT("ReactionWarp.Smoke"),
    TEXT("Transient Lv_Jang integration fixture: Server|Client|Observer. Exits test process. Use Scripts/TestReactionWarp.ps1 -Network."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if (!World || Args.IsEmpty() || Args.Num()>2 || (Args[0]!=TEXT("Server") && Args[0]!=TEXT("Client") && Args[0]!=TEXT("Observer"))) return;
        auto Test=MakeShared<FReactionWarpSmoke>(); Test->World=World; Test->GameInstance=World->GetGameInstance(); Test->Role=Args[0];
        Test->bCompare=Args.Contains(TEXT("Compare"));
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Delta) { return Test->Tick(Delta); }));
    }));
}
#endif
