#if !UE_BUILD_SHIPPING
#include "Combat/Probe/HitWallProbeSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "SharpMovement/Experiment/SharpExperimentManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
struct FHitWallTourSmoke
{
    TWeakObjectPtr<UWorld> World;
    double Began=FPlatformTime::Seconds();
    int32 Stage=-1;
    FTransform OriginalPawn;
    TWeakObjectPtr<AActor> OriginalView;
    bool Tick(float)
    {
        UWorld* W=World.Get();
        if(!W || FPlatformTime::Seconds()-Began>150.) { Finish(false,TEXT("world/timeout")); return false; }
        if(Stage==-1)
        {
            // This fixture exercises hit reactions, not the map's independent
            // spline experiment manager. Only this disposable test world changes.
            for(TActorIterator<ASharpExperimentManager> It(W); It; ++It) W->GetTimerManager().ClearAllTimersForObject(*It);
            Stage=0;
        }
        auto* PC=W->GetFirstPlayerController();
        if(PC && !PC->GetPawn() && W->GetAuthGameMode()) W->GetAuthGameMode()->RestartPlayer(PC);
        if(!PC || !PC->GetPawn()) return true;
        auto* S=W->GetSubsystem<UHitWallProbeSubsystem>();
        if(!S) { Finish(false,TEXT("subsystem")); return false; }
        if(Stage==0)
        {
            OriginalPawn=PC->GetPawn()->GetActorTransform(); OriginalView=PC->GetViewTarget();
            GEngine->Exec(W,TEXT("ReactionWarp.Mode MultiPhase AllowDraft=1 Library=/Game/Combat/Analysis/DA_HitReaction_Front_PELT Policy=/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft"));
            GEngine->Exec(W,TEXT("ReactionWarp.Stations IncludeDisabled=1"));
            GEngine->Exec(W,TEXT("ReactionWarp.Tour mode=MultiPhase baseline=Off gaps=100 IncludeDisabled=1 AllowDraft=1"));
            if(!S->IsRunning()) { Finish(false,TEXT("start")); return false; }
            GEngine->Exec(W,TEXT("hitwall.Stop"));
            if(!PC->GetPawn()->GetActorTransform().Equals(OriginalPawn,0.1) || PC->GetViewTarget()!=OriginalView.Get() || PC->IsMoveInputIgnored())
            { Finish(false,TEXT("cancel restoration")); return false; }
            GEngine->Exec(W,TEXT("ReactionWarp.Tour mode=MultiPhase baseline=Off gaps=100 IncludeDisabled=1 AllowDraft=1"));
            Stage=1;
        }
        else if(!S->IsRunning())
        {
            FString Rows; FFileHelper::LoadFileToString(Rows,*(S->GetWarpTaskDirectory()/TEXT("trials.csv")));
            TArray<FString> Lines; Rows.ParseIntoArrayLines(Lines);
            bool OK=Lines.Num()==13 && !Rows.Contains(TEXT(",Pending,")) && !Rows.Contains(TEXT(",Rejected,")) && !Rows.Contains(TEXT(",Failed,")) && !Rows.Contains(TEXT(",Skipped,"));
            for(const TCHAR* Name : {TEXT("Wall"),TEXT("Ledge30"),TEXT("Ledge60"),TEXT("Slope30"),TEXT("Slope55"),TEXT("Wall45")}) OK &= Rows.Contains(Name);
            OK &= PC->GetPawn()->GetActorTransform().Equals(OriginalPawn,0.1) && PC->GetViewTarget()==OriginalView.Get() && !PC->IsMoveInputIgnored();
            UE_LOG(LogTemp,Display,TEXT("[HitWallTourSmoke] Artifact=%s"),*S->GetWarpTaskDirectory());
            Finish(OK,TEXT("12 trials / six stations / cancel and natural completion restore")); return false;
        }
        return true;
    }
    static void Finish(bool OK,const TCHAR* Why)
    {
        UE_LOG(LogTemp,Display,TEXT("[HitWallTourSmoke] %s %s"),OK ? TEXT("PASS") : TEXT("FAIL"),Why);
        FPlatformMisc::RequestExitWithStatus(false,OK ? 0 : 1);
    }
};
static FAutoConsoleCommandWithWorldAndArgs GTourSmoke(TEXT("ReactionWarp.TourSmoke"),
    TEXT("Standalone YHJ map integration test; exits this test process. Do not run in a working Editor."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* W)
    {
        if(!FApp::IsGame() || !W || !W->IsGameWorld() || W->GetNetMode()!=NM_Standalone) return;
        UE_LOG(LogTemp,Display,TEXT("[HitWallTourSmoke] Started World=%s"),*W->GetName());
        auto Test=MakeShared<FHitWallTourSmoke>(); Test->World=W;
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float D){ return Test->Tick(D); }));
    }));
}
#endif
