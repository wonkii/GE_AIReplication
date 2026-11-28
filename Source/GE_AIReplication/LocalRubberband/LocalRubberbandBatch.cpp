// Independent baseline runner. Commands affect only the owning client in Lv_LocalRubberband.
#include "LocalRubberbandCharacter.h"
#include "LocalRubberbandInputScriptComponent.h"
#include "LocalRubberbandMovementComponent.h"
#include "LocalRubberbandRecordingSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
DEFINE_LOG_CATEGORY_STATIC(LogLocalRubberbandBatch, Log, All);
namespace LocalRubberbandBatchDetail
{
    struct FJob { FString Name; int32 Min=0,Max=0,Loss=0, BurstMs=0; };
    struct FRun
    {
        TWeakObjectPtr<UWorld> World, Server;
        TWeakObjectPtr<ALocalRubberbandCharacter> Character;
        TArray<FJob> Jobs;
        FString BatchId, Direction, Directory;
        int32 Index=0, Loops=3, Phase=0, PlayerId=-1;
        float Desync=0;
        bool bInjected=false, bBurstStarted=false, bBurstActive=false;
        double BurstStart=0;
        double PhaseTime=0;
        FTimerHandle Timer;
#if DO_ENABLE_NET_TEST
        FPacketSimulationSettings Original;
#endif
    };
    TUniquePtr<FRun> Run;
    bool IsLocalMap(const UWorld* W) { return W && W->GetMapName().EndsWith(TEXT("Lv_LocalRubberband")); }
    bool Parse(const FString& Name,FJob& Job)
    {
        // Same seven profiles as the remote baseline. No unit conversion: one-way ms.
        Job.Name=Name;
        if (Name==TEXT("N0")) return true;
        // Local-only stress profiles; deliberately not part of the common seven.
        if (Name==TEXT("B500")) { Job.BurstMs=500; return true; }
        if (Name==TEXT("B1000")) { Job.BurstMs=1000; return true; }
        if (Name==TEXT("B2000")) { Job.BurstMs=2000; return true; }
        if (Name==TEXT("L100")) { Job.Min=Job.Max=100; return true; }
        if (Name==TEXT("L200")) { Job.Min=Job.Max=200; return true; }
        if (Name==TEXT("L300")) { Job.Min=Job.Max=300; return true; }
        if (Name==TEXT("J50-150")) { Job.Min=50; Job.Max=150; return true; }
        if (Name==TEXT("J100-300")) { Job.Min=100; Job.Max=300; return true; }
        if (Name==TEXT("L200-P3")) { Job.Min=Job.Max=200; Job.Loss=3; return true; }
        return false;
    }
    ULocalRubberbandRecordingSubsystem* Recorder(UWorld* W)
    { return W ? W->GetSubsystem<ULocalRubberbandRecordingSubsystem>() : nullptr; }
    void StopRecorders(const FString& Reason)
    {
        if (!Run) return;
        if (auto* R=Recorder(Run->World.Get())) R->Stop(Reason);
        if (auto* R=Recorder(Run->Server.Get())) R->Stop(Reason);
    }
    void Finish(const FString& Reason)
    {
        if (!Run) return;
        if (UWorld* W=Run->World.Get())
        {
            W->GetTimerManager().ClearTimer(Run->Timer);
#if DO_ENABLE_NET_TEST
            if (W->GetNetDriver()) W->GetNetDriver()->SetPacketSimulationSettings(Run->Original);
#endif
        }
        if (auto* C=Run->Character.Get()) if (C->InputScript) C->InputScript->StopScript(TEXT("BatchEnd"));
        StopRecorders(Reason);
        UE_LOG(LogLocalRubberbandBatch,Display,TEXT("lrb: %s (%d/%d); %s"),*Reason,Run->Index,Run->Jobs.Num(),*Run->BatchId);
        Run.Reset();
    }
    bool Apply(UWorld* W,const FJob& Job,const FString& Dir)
    {
#if DO_ENABLE_NET_TEST
        if (!W || !W->GetNetDriver()) return false;
        FPacketSimulationSettings S;
        if (Dir==TEXT("In") || Dir==TEXT("Both"))
        { S.PktIncomingLagMin=Job.Min; S.PktIncomingLagMax=Job.Max; S.PktIncomingLoss=Job.Loss; }
        if (Dir==TEXT("Out") || Dir==TEXT("Both"))
        {
            // UE outgoing min/max random lag (same range, different engine path from incoming).
            S.PktLagMin=Job.Min; S.PktLagMax=Job.Max; S.PktLoss=Job.Loss;
        }
        W->GetNetDriver()->SetPacketSimulationSettings(S);
        return true;
#else
        return false;
#endif
    }
    bool OpenJob(ALocalRubberbandCharacter& C)
    {
        const FJob& J=Run->Jobs[Run->Index];
        Run->Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Experiments/LocalRubberband"),Run->BatchId,
            FString::Printf(TEXT("%03d_%s_%s%s"),Run->Index+1,*J.Name,*Run->Direction,Run->Desync>0?TEXT("_Synthetic"):TEXT("")));
        if (!IFileManager::Get().MakeDirectory(*Run->Directory,true)) return false;
        if (!FFileHelper::SaveStringToFile(TEXT("# Local player baseline\nOnly the selected autonomous proxy and its server actor are recorded.\nUse Scripts/Analysis/local_rubberband_metrics.py.\n"),
            *FPaths::Combine(Run->Directory,TEXT("README.md")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
        const FVector Start=C.GetActorLocation();
        const FString Meta=FString::Printf(TEXT("Condition=%s\nDirection=%s\nLagMinMs=%d\nLagMaxMs=%d\nLossPct=%d\nBurstMs=%d\nBurstTriggerSeconds=0.75\nLoops=%d\nSyntheticDesyncCm=%.1f\nPlayerId=%d\nInitialClientLocation=%s\nVariant=NativeBaseline\n"),
            *J.Name,*Run->Direction,J.Min,J.Max,J.Loss,J.BurstMs,Run->Loops,Run->Desync,Run->PlayerId,*Start.ToString());
        if (!FFileHelper::SaveStringToFile(Meta,*FPaths::Combine(Run->Directory,TEXT("condition.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
        auto* L=Recorder(Run->World.Get()); auto* S=Recorder(Run->Server.Get());
        if (!L || !S || !L->Start(Run->Directory,Run->PlayerId,true) || !S->Start(Run->Directory,Run->PlayerId,false)) return false;
        CastChecked<ULocalRubberbandMovementComponent>(C.GetCharacterMovement())->ResetObservation();
        return true;
    }
    void Step()
    {
        if (!Run) return;
        UWorld* W=Run->World.Get(); auto* C=Run->Character.Get();
        if (!W || !Run->Server.IsValid() || !C || !C->InputScript || !C->IsLocallyControlled() || !W->GetNetDriver()) { Finish(TEXT("Aborted: world or pawn lost")); return; }
        const double Now=W->GetRealTimeSeconds(), Elapsed=Now-Run->PhaseTime;
        auto Enter=[Now](int32 P){Run->Phase=P;Run->PhaseTime=Now;};
        if (Run->Phase==0)
        {
            if (Run->Index>=Run->Jobs.Num()) { Finish(TEXT("Completed")); return; }
            if (!Apply(W,Run->Jobs[Run->Index],Run->Direction)) { Finish(TEXT("Net emulation unavailable")); return; }
            Run->bInjected=false; Run->bBurstStarted=false; Run->bBurstActive=false; Enter(1); return;
        }
        if (Run->Phase==1)
        {
            if (Elapsed<1.5) return;
            if (!OpenJob(*C)) { Finish(TEXT("Recording start failed")); return; }
            Enter(2); return;
        }
        if (Run->Phase==5) { if (Elapsed>=1.0) Enter(0); return; }
        auto* L=Recorder(W); auto* S=Recorder(Run->Server.Get());
        if (!L || !S || !L->IsHealthy() || !S->IsHealthy()) { Finish(TEXT("Recorder unhealthy")); return; }
        if (Run->Phase==2)
        {
            if (Elapsed<0.5) return;
            if (!C->InputScript->StartScript(Run->Loops)) { Finish(TEXT("Script start failed")); return; }
            Enter(3); return;
        }
        if (Run->Phase==3)
        {
            const FJob& Job=Run->Jobs[Run->Index];
            // Inject once, after the first scripted movement has begun. Use real time;
            // the 50ms game timer means actual duration may be longer under hitches.
            if (Job.BurstMs>0 && !Run->bBurstStarted && Elapsed>=0.75)
            {
                FJob Blocked=Job; Blocked.Loss=100;
                if (!Apply(W,Blocked,Run->Direction)) { Finish(TEXT("Burst start failed")); return; }
                Run->bBurstStarted=true; Run->bBurstActive=true; Run->BurstStart=Now;
                L->RecordScriptEvent(*C,TEXT("BurstStart"),-1,-1,NAME_None);
            }
            if (Run->bBurstActive && (Now-Run->BurstStart)*1000.0>=Job.BurstMs)
            {
                if (!Apply(W,Job,Run->Direction)) { Finish(TEXT("Burst end failed")); return; }
                Run->bBurstActive=false;
                L->RecordScriptEvent(*C,TEXT("BurstEnd"),-1,-1,NAME_None);
            }
            if (Run->Desync>0 && !Run->bInjected && Elapsed>=1.0)
            {
                Run->bInjected=true;
                const FVector Before=C->GetActorLocation();
                const bool bMoved=C->SetActorLocation(Before+FVector(Run->Desync,0,0),false,nullptr,ETeleportType::TeleportPhysics);
                L->RecordScriptEvent(*C,FString::Printf(TEXT("SyntheticDesync:%d:%.2f"),bMoved?1:0,FVector::Dist(Before,C->GetActorLocation())),-1,-1,NAME_None);
            }
            if (!C->InputScript->IsRunning())
            {
                if (Job.BurstMs>0 && (!Run->bBurstStarted || Run->bBurstActive)) { Finish(TEXT("Script ended before burst completed")); return; }
                Enter(4);
            }
            return;
        }
        if (Run->Phase==4 && Elapsed>=1.0)
        { StopRecorders(TEXT("Completed")); ++Run->Index; Enter(5); return; }
    }
    void Begin(const TArray<FString>& Args,UWorld* W,bool bBatch)
    {
#if !DO_ENABLE_NET_TEST
        UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("lrb requires a build with DO_ENABLE_NET_TEST")); return;
#else
        if (Run && !Run->World.IsValid()) Finish(TEXT("Previous world ended"));
        if (Run) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("lrb already running; lrb.Stop")); return; }
        if (!IsLocalMap(W) || W->GetNetMode()!=NM_Client || !W->GetNetDriver()) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Use Lv_LocalRubberband, Play As Client")); return; }
        APlayerController* PC=W->GetFirstPlayerController();
        auto* C=PC?Cast<ALocalRubberbandCharacter>(PC->GetPawn()):nullptr;
        if (!C || !C->GetPlayerState() || !C->InputScript || C->InputScript->IsRunning() || !Cast<ULocalRubberbandMovementComponent>(C->GetCharacterMovement()))
        { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Local BP/CMC/input or PlayerState missing")); return; }
        UWorld* Server=nullptr;
        for (const FWorldContext& Context:GEngine->GetWorldContexts())
        {
            UWorld* Candidate=Context.World();
            if (!IsLocalMap(Candidate) || Candidate->GetNetMode()!=NM_DedicatedServer) continue;
            if (Server) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Multiple dedicated server worlds; abort")); return; }
            Server=Candidate;
        }
        if (!Server) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Use single-process PIE with dedicated server")); return; }
        const int32 PlayerId=C->GetPlayerState()->GetPlayerId();
        bool bFound=false;
        for (TActorIterator<ALocalRubberbandCharacter> It(Server);It;++It)
            if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId()==PlayerId) { bFound=true; break; }
        if (!bFound) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Server pawn identity not ready; retry after spawn")); return; }
        auto State=MakeUnique<FRun>(); State->World=W; State->Server=Server; State->Character=C; State->PlayerId=PlayerId;
        State->Direction=Args.Num()>1?Args[1]:TEXT("In");
        if (State->Direction!=TEXT("In") && State->Direction!=TEXT("Out") && State->Direction!=TEXT("Both")) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Direction: In, Out or Both (case-sensitive)")); return; }
        State->Loops=Args.Num()>2?FMath::Clamp(FCString::Atoi(*Args[2]),1,100):3;
        if (!bBatch && Args.Num()>3) State->Desync=FMath::Clamp(FCString::Atof(*Args[3]),0.f,600.f);
        const FString Conditions=Args.Num()>0?Args[0]:(bBatch?TEXT("N0,L100,L200,L300,J50-150,J100-300,L200-P3"):TEXT("N0"));
        TArray<FString> Names; Conditions.ParseIntoArray(Names,TEXT(","),true);
        if (Names.IsEmpty() || (!bBatch && Names.Num()!=1)) return;
        for (const FString& Name:Names) { FJob J; if (!Parse(Name,J)) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Unknown condition %s"),*Name); return; } State->Jobs.Add(J); }
        if (State->Desync>0 && (State->Jobs[0].Name!=TEXT("N0") || State->Desync<50)) { UE_LOG(LogLocalRubberbandBatch,Warning,TEXT("Synthetic test: N0, 50-600 cm only")); return; }
        State->Original=W->GetNetDriver()->PacketSimulationSettings;
        State->BatchId=FDateTime::UtcNow().ToString(TEXT("L%m%d-%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
        Run=MoveTemp(State); W->GetTimerManager().SetTimer(Run->Timer,FTimerDelegate::CreateStatic(&Step),0.05f,true);
        UE_LOG(LogLocalRubberbandBatch,Display,TEXT("lrb: %s, %d job(s), %d loops, direction %s"),*Run->BatchId,Run->Jobs.Num(),Run->Loops,*Run->Direction);
#endif
    }
    FAutoConsoleCommandWithWorldAndArgs RunCommand(TEXT("lrb.Run"),TEXT("lrb.Run [Condition=N0] [In|Out|Both=In] [Loops=3] [SyntheticDesyncCm=0]"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){Begin(A,W,false);}));
    FAutoConsoleCommandWithWorldAndArgs BatchCommand(TEXT("lrb.Batch"),TEXT("lrb.Batch [Conditions=N0,L100,L200,L300,J50-150,J100-300,L200-P3] [In|Out|Both=In] [Loops=3]"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A,UWorld* W){Begin(A,W,true);}));
    FAutoConsoleCommandWithWorld StopCommand(TEXT("lrb.Stop"),TEXT("Cancel the local run and restore the selected client's original emulation"),
        FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){if (Run && Run->World.Get()==W) Finish(TEXT("Cancelled"));}));
}
