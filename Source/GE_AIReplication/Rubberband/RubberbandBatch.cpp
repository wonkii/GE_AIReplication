// rb.Batch: run every network condition x smoothing mode in one go (single-process PIE).
// Typed in the window of the character that should move (Client B). The other client worlds (Client A)
// get the incoming lag/loss of each condition through their NetDriver, so no editor setting changes are needed.

#include "RubberbandInputScriptComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRubberbandBatch, Log, All);

namespace
{
	struct FBatchJob { FString Condition; int32 LagMin = 0, LagMax = 0, Loss = 0, Mode = 2; };

	enum class EPhase : uint8 { Apply, Arm, Running, Tail };

	struct FBatchState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<URubberbandInputScriptComponent> Script;
		TArray<FBatchJob> Jobs;
		int32 Index = 0, Loops = 3;
		FString BatchId, SmoothTime = TEXT("0.06");
		EPhase Phase = EPhase::Apply;
		bool bScriptStarted = false;
		double PhaseStart = 0;
		FTimerHandle Timer;
	};
	TUniquePtr<FBatchState> Batch;

	const TCHAR* ModeName(int32 Mode)
	{
		static const TCHAR* Names[] = { TEXT("Dis"), TEXT("Lin"), TEXT("Exp") };
		return Mode >= 0 && Mode <= 2 ? Names[Mode] : TEXT("Keep");
	}

	void SetVariable(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) { Variable->Set(*Value, ECVF_SetByConsole); }
	}

	/** N0 | L<ms> | J<min>-<max>, optional -P<loss%> suffix (e.g. L200-P3, J100-300-P3). */
	bool ParseCondition(const FString& Name, FBatchJob& Job)
	{
		Job.Condition = Name;
		FString Body = Name, LossText;
		if (Name.Split(TEXT("-P"), &Body, &LossText, ESearchCase::IgnoreCase, ESearchDir::FromEnd)) { Job.Loss = FCString::Atoi(*LossText); }
		if (Body.StartsWith(TEXT("N"))) { return true; }
		if (Body.StartsWith(TEXT("L"))) { Job.LagMin = Job.LagMax = FCString::Atoi(*Body.Mid(1)); return Job.LagMax > 0; }
		FString Min, Max;
		if (Body.StartsWith(TEXT("J")) && Body.Mid(1).Split(TEXT("-"), &Min, &Max))
		{
			Job.LagMin = FCString::Atoi(*Min); Job.LagMax = FCString::Atoi(*Max); return Job.LagMax >= Job.LagMin;
		}
		return false;
	}

	/** Incoming lag/loss on every other client world (the viewers); the moving client and the server stay clean. */
	int32 ApplyEmulation(const UWorld* MovingWorld, const FBatchJob& Job)
	{
		int32 Applied = 0;
#if DO_ENABLE_NET_TEST
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Viewer = Context.World();
			if (!Viewer || Viewer == MovingWorld || Viewer->GetNetMode() != NM_Client || !Viewer->GetNetDriver()) { continue; }
			FPacketSimulationSettings Settings;
			Settings.PktIncomingLagMin = Job.LagMin;
			Settings.PktIncomingLagMax = Job.LagMax;
			Settings.PktIncomingLoss = Job.Loss;
			Viewer->GetNetDriver()->SetPacketSimulationSettings(Settings);
			++Applied;
		}
#endif
		return Applied;
	}

	void Finish(const FString& Reason)
	{
		if (!Batch) { return; }
		if (UWorld* World = Batch->World.Get())
		{
			World->GetTimerManager().ClearTimer(Batch->Timer);
			ApplyEmulation(World, FBatchJob());
		}
		if (URubberbandInputScriptComponent* Script = Batch->Script.Get()) { Script->StopScript(TEXT("BatchEnd")); }
		SetVariable(TEXT("rb.Record"), TEXT("0"));
		UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch %s: %s (%d/%d runs). Analyse: python Scripts/Analysis/rubberband_metrics.py Saved/Experiments/Rubberband --batch %s"),
			*Batch->BatchId, *Reason, Batch->Index, Batch->Jobs.Num(), *Batch->BatchId);
		Batch.Reset();
	}

	void Step()
	{
		UWorld* World = Batch ? Batch->World.Get() : nullptr;
		URubberbandInputScriptComponent* Script = Batch ? Batch->Script.Get() : nullptr;
		if (!World || !Script) { Finish(TEXT("Aborted (world or character gone)")); return; }
		const double Now = World->GetRealTimeSeconds();
		const double Elapsed = Now - Batch->PhaseStart;
		auto Enter = [Now](EPhase Phase) { Batch->Phase = Phase; Batch->PhaseStart = Now; };

		switch (Batch->Phase)
		{
		case EPhase::Apply:
		{
			if (Batch->Index >= Batch->Jobs.Num()) { Finish(TEXT("Completed")); return; }
			const FBatchJob& Job = Batch->Jobs[Batch->Index];
			const int32 Viewers = ApplyEmulation(World, Job);
			SetVariable(TEXT("rb.Smooth.Mode"), FString::FromInt(Job.Mode));
			SetVariable(TEXT("rb.Smooth.LocationTime"), Batch->SmoothTime);
			UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch [%d/%d] %s %s: incoming lag %d-%d ms loss %d%% on %d viewer(s)"),
				Batch->Index + 1, Batch->Jobs.Num(), *Job.Condition, ModeName(Job.Mode), Job.LagMin, Job.LagMax, Job.Loss, Viewers);
			if (Viewers == 0) { Finish(TEXT("No other client window (use Play As Client, 2 players, single process)")); return; }
			Enter(EPhase::Arm);
			return;
		}
		case EPhase::Arm:
			// Let the new emulation and smoothing settle before recording.
			if (Elapsed < 1.5) { return; }
			{
				const FBatchJob& Job = Batch->Jobs[Batch->Index];
				SetVariable(TEXT("rb.Condition"), Job.Condition);
				SetVariable(TEXT("rb.RunId"), Batch->BatchId + TEXT("_") + ModeName(Job.Mode));
				SetVariable(TEXT("rb.Record"), TEXT("1"));
			}
			Enter(EPhase::Running);
			Batch->bScriptStarted = false;
			Batch->PhaseStart = Now + 0.5; // script starts after every world opened its files
			return;
		case EPhase::Running:
			if (Now < Batch->PhaseStart) { return; }
			if (!Batch->bScriptStarted)
			{
				Batch->bScriptStarted = true;
				Script->bStopRecordingOnComplete = true; // turns rb.Record off 1 s after the last loop
				if (!Script->StartScript(Batch->Loops)) { Finish(TEXT("Script could not start")); return; }
				return;
			}
			if (!Script->IsRunning()) { Enter(EPhase::Tail); }
			return;
		case EPhase::Tail:
			if (Elapsed < 2.0) { return; }
			++Batch->Index;
			Enter(EPhase::Apply);
			return;
		}
	}

	TArray<FString> SplitList(const FString& Text)
	{
		TArray<FString> Items; Text.ParseIntoArray(Items, TEXT(","), true); return Items;
	}

	FAutoConsoleCommandWithWorldAndArgs BatchCommand(TEXT("rb.Batch"),
		TEXT("Run all conditions x smoothing modes in one go (single-process PIE, typed in the moving client's window).\n")
		TEXT("rb.Batch [Loops=3] [Conditions=N0,L100,L200,L300,J50-150,J100-300,L200-P3] [Modes=0,1,2] [SmoothTime=0.06]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Batch) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Batch already running (rb.Batch.Stop to cancel)")); return; }
			APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			URubberbandInputScriptComponent* Script = Controller && Controller->GetPawn()
				? Controller->GetPawn()->FindComponentByClass<URubberbandInputScriptComponent>() : nullptr;
			if (!Script) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Batch: type it in the window of the character that should move")); return; }

			TUniquePtr<FBatchState> State = MakeUnique<FBatchState>();
			State->Loops = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 3;
			const TArray<FString> Conditions = SplitList(Args.Num() > 1 ? Args[1] : FString(TEXT("N0,L100,L200,L300,J50-150,J100-300,L200-P3")));
			const TArray<FString> Modes = SplitList(Args.Num() > 2 ? Args[2] : FString(TEXT("0,1,2")));
			if (Args.Num() > 3) { State->SmoothTime = Args[3]; }
			for (const FString& Name : Conditions)
			{
				FBatchJob Parsed;
				if (!ParseCondition(Name, Parsed)) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Batch: unknown condition '%s' (N0, L200, J100-300, L200-P3)"), *Name); return; }
				for (const FString& Mode : Modes) { FBatchJob Job = Parsed; Job.Mode = FCString::Atoi(*Mode); State->Jobs.Add(Job); }
			}
			State->World = World; State->Script = Script;
			State->BatchId = FDateTime::Now().ToString(TEXT("B%m%d-%H%M"));
			State->PhaseStart = World->GetRealTimeSeconds();
			Batch = MoveTemp(State);
			SetVariable(TEXT("rb.Record"), TEXT("0"));
			World->GetTimerManager().SetTimer(Batch->Timer, FTimerDelegate::CreateStatic(&Step), 0.1f, true);
			UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch %s: %d runs x %d loops (about %.0f min)"),
				*Batch->BatchId, Batch->Jobs.Num(), Batch->Loops, Batch->Jobs.Num() * (Batch->Loops * 17.0 + 5.0) / 60.0);
		}));

	FAutoConsoleCommandWithWorld StopBatchCommand(TEXT("rb.Batch.Stop"), TEXT("Cancel rb.Batch and clear the emulation it set."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld*) { Finish(TEXT("Cancelled")); }));
}
