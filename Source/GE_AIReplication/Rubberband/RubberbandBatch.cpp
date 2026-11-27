// rb.Batch: run every network condition x smoothing mode in one go (single-process PIE).
// rb.Plan: the B sweep experiments (jitter size, smoothing time, Ada-T tuning) repeated N times, as one queue.
// Typed in the window of the character that should move (Client B). The other client worlds (Client A)
// get the incoming lag/loss of each condition through their NetDriver, so no editor setting changes are needed.

#include "RubberbandInputScriptComponent.h"
#include "RubberbandMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRubberbandBatch, Log, All);

// Named namespace: unity builds merge anonymous namespaces of neighbouring .cpp files.
namespace RubberbandBatchDetail
{
	struct FBatchJob
	{
		FString Condition; int32 LagMin = 0, LagMax = 0, Loss = 0, Mode = 2;
		bool bUseTime = true, bUseRate = true;
		FString SmoothTime; // empty = batch default
		float AdaGain = -1.f, AdaMaxTime = -1.f; // M2 tuning, < 0 = class default
		float BufDelay = -1.f; bool bBufAdaptive = false; // M4 (mode 4), < 0 = class default
		FString Prefix; // rb.Plan: "r1J_" (repeat + group) keeps RunIds unique
	};

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

	float EffectiveGain(const FBatchJob& Job) { return Job.AdaGain >= 0.f ? Job.AdaGain : GetDefault<URubberbandMovementComponent>()->JitterGain; }
	float EffectiveMaxTime(const FBatchJob& Job) { return Job.AdaMaxTime > 0.f ? Job.AdaMaxTime : GetDefault<URubberbandMovementComponent>()->MaxSmoothTime; }

	float EffectiveBufDelay(const FBatchJob& Job) { return Job.BufDelay > 0.f ? Job.BufDelay : GetDefault<URubberbandMovementComponent>()->BufferDelay; }

	/** Same labels as the recorder's Variant column: Dis, Lin, Exp, Exp-0.15, Ada, Ada-T, Ada-R, Ada-T-m0.40-g12.0, Buf-0.10, Buf-A. */
	FString VariantName(const FBatchJob& Job, const FString& DefaultTime)
	{
		if (Job.Mode == 4) { return Job.bBufAdaptive ? FString(TEXT("Buf-A")) : FString::Printf(TEXT("Buf-%.2f"), EffectiveBufDelay(Job)); }
		if (Job.Mode == 3)
		{
			const FString Name = Job.bUseTime && Job.bUseRate ? TEXT("Ada") : Job.bUseTime ? TEXT("Ada-T") : Job.bUseRate ? TEXT("Ada-R") : TEXT("Ada-0");
			return Job.bUseTime ? Name + URubberbandMovementComponent::TuningLabel(EffectiveGain(Job), EffectiveMaxTime(Job)) : Name;
		}
		if (Job.Mode == 0) { return TEXT("Dis"); }
		const FString Name = Job.Mode == 1 ? TEXT("Lin") : Job.Mode == 2 ? TEXT("Exp") : TEXT("Keep");
		const float Time = FCString::Atof(*(Job.SmoothTime.IsEmpty() ? DefaultTime : Job.SmoothTime));
		return FMath::IsNearlyEqual(Time, 0.06f, 0.001f) || Time <= 0.f ? Name : FString::Printf(TEXT("%s-%.2f"), *Name, Time);
	}

	/** Mode token: 0|1|2|3|4, 3t = adaptive time only, 3r = rate limit only, optional @time (e.g. 2@0.15),
	 *  adaptive tuning after ':' (m = MaxSmoothTime s, g = JitterGain), e.g. 3t:m0.40:g12.
	 *  4@0.10 = interpolation buffer with a fixed 0.10 s delay, 4a = buffer delay following the jitter. */
	bool ParseMode(const FString& Token, FBatchJob& Job)
	{
		TArray<FString> Parts;
		Token.ParseIntoArray(Parts, TEXT(":"), true);
		if (Parts.Num() == 0) { return false; }
		FString Head = Parts[0], Time;
		if (Parts[0].Split(TEXT("@"), &Head, &Time)) { Job.SmoothTime = Time; }
		if (Head.IsEmpty() || !FChar::IsDigit(Head[0])) { return false; }
		Job.Mode = FCString::Atoi(*Head.Left(1));
		const FString Suffix = Head.Mid(1).ToLower();
		Job.bUseTime = Suffix != TEXT("r");
		Job.bUseRate = Suffix != TEXT("t");
		if (Job.Mode == 4)
		{
			if (!Suffix.IsEmpty() && Suffix != TEXT("a")) { return false; }
			Job.bBufAdaptive = Suffix == TEXT("a");
			if (!Job.SmoothTime.IsEmpty()) { Job.BufDelay = FCString::Atof(*Job.SmoothTime); Job.SmoothTime.Reset(); }
		}
		for (int32 Index = 1; Index < Parts.Num(); ++Index)
		{
			const FString Part = Parts[Index].ToLower();
			const float Value = FCString::Atof(*Part.Mid(1));
			if (Job.Mode != 3) { return false; }
			if (Part.StartsWith(TEXT("m")) && Value > 0.f) { Job.AdaMaxTime = Value; }
			else if (Part.StartsWith(TEXT("g")) && Value >= 0.f) { Job.AdaGain = Value; }
			else { return false; }
		}
		return Job.Mode >= 0 && Job.Mode <= 4;
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
		SetVariable(TEXT("rb.Ada.Gain"), FString::SanitizeFloat(GetDefault<URubberbandMovementComponent>()->JitterGain));
		SetVariable(TEXT("rb.Ada.MaxTime"), FString::SanitizeFloat(GetDefault<URubberbandMovementComponent>()->MaxSmoothTime));
		UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch %s: %s (%d/%d runs). Analyse: python Scripts/Analysis/rubberband_metrics.py Saved/Experiments/Rubberband --batch %s, then python Scripts/Analysis/rubberband_sweep_plot.py <metrics output folder>"),
			*Batch->BatchId, *Reason, Batch->Index, Batch->Jobs.Num(), *Batch->BatchId);
		Batch.Reset();
	}

	void BatchStep()
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
			SetVariable(TEXT("rb.Smooth.LocationTime"), Job.SmoothTime.IsEmpty() ? Batch->SmoothTime : Job.SmoothTime);
			SetVariable(TEXT("rb.Ada.UseTime"), Job.bUseTime ? TEXT("1") : TEXT("0"));
			SetVariable(TEXT("rb.Ada.UseRate"), Job.bUseRate ? TEXT("1") : TEXT("0"));
			// Always explicit: "-1 keeps" would leave the previous job's tuning in place.
			SetVariable(TEXT("rb.Ada.Gain"), FString::SanitizeFloat(EffectiveGain(Job)));
			SetVariable(TEXT("rb.Ada.MaxTime"), FString::SanitizeFloat(EffectiveMaxTime(Job)));
			SetVariable(TEXT("rb.Buf.Adaptive"), Job.bBufAdaptive ? TEXT("1") : TEXT("0"));
			SetVariable(TEXT("rb.Buf.Delay"), FString::SanitizeFloat(EffectiveBufDelay(Job)));
			UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch [%d/%d] %s%s %s: incoming lag %d-%d ms loss %d%% on %d viewer(s)"),
				Batch->Index + 1, Batch->Jobs.Num(), *Job.Prefix, *Job.Condition, *VariantName(Job, Batch->SmoothTime), Job.LagMin, Job.LagMax, Job.Loss, Viewers);
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
				SetVariable(TEXT("rb.RunId"), Batch->BatchId + TEXT("_") + Job.Prefix + VariantName(Job, Batch->SmoothTime));
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

	URubberbandInputScriptComponent* FindScript(UWorld* World, const TCHAR* Command)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		URubberbandInputScriptComponent* Script = Controller && Controller->GetPawn()
			? Controller->GetPawn()->FindComponentByClass<URubberbandInputScriptComponent>() : nullptr;
		if (Batch) { UE_LOG(LogRubberbandBatch, Warning, TEXT("%s: a batch is already running (rb.Batch.Stop to cancel)"), Command); return nullptr; }
		if (!Script) { UE_LOG(LogRubberbandBatch, Warning, TEXT("%s: type it in the window of the character that should move"), Command); }
		return Script;
	}

	/** Conditions x modes appended to State.Jobs. False (with a warning) on an unknown token. */
	bool AddJobs(FBatchState& State, const TArray<FString>& Conditions, const TArray<FString>& Modes, const FString& Prefix)
	{
		for (const FString& Name : Conditions)
		{
			FBatchJob Parsed;
			if (!ParseCondition(Name, Parsed)) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Batch: unknown condition '%s' (N0, L200, J100-300, L200-P3)"), *Name); return false; }
			for (const FString& Mode : Modes)
			{
				FBatchJob Job = Parsed;
				if (!ParseMode(Mode, Job)) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Batch: unknown mode '%s' (0,1,2,3,3t,3r,2@0.15,3t:m0.40:g12,4@0.10,4a)"), *Mode); return false; }
				Job.Prefix = Prefix;
				State.Jobs.Add(Job);
			}
		}
		return true;
	}

	void Begin(UWorld* World, URubberbandInputScriptComponent* Script, TUniquePtr<FBatchState> State, const TCHAR* IdFormat)
	{
		State->World = World; State->Script = Script;
		State->BatchId = FDateTime::Now().ToString(IdFormat);
		State->PhaseStart = World->GetRealTimeSeconds();
		Batch = MoveTemp(State);
		SetVariable(TEXT("rb.Record"), TEXT("0"));
		World->GetTimerManager().SetTimer(Batch->Timer, FTimerDelegate::CreateStatic(&BatchStep), 0.1f, true);
		UE_LOG(LogRubberbandBatch, Display, TEXT("rb.Batch %s: %d runs x %d loops (about %.0f min)"),
			*Batch->BatchId, Batch->Jobs.Num(), Batch->Loops, Batch->Jobs.Num() * (Batch->Loops * 17.0 + 9.0) / 60.0);
	}

	FAutoConsoleCommandWithWorldAndArgs BatchCommand(TEXT("rb.Batch"),
		TEXT("Run all conditions x smoothing modes in one go (single-process PIE, typed in the moving client's window).\n")
		TEXT("rb.Batch [Loops=3] [Conditions=N0,L100,L200,L300,J50-150,J100-300,L200-P3] [Modes=0,1,2] [SmoothTime=0.06]\n")
		TEXT("Modes: 0 Dis, 1 Lin, 2 Exp, 3 Ada (3t time only, 3r rate only), @time per mode e.g. 2@0.15, tuning e.g. 3t:m0.40:g12, 4@0.10 / 4a buffer"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			URubberbandInputScriptComponent* Script = FindScript(World, TEXT("rb.Batch"));
			if (!Script) { return; }
			TUniquePtr<FBatchState> State = MakeUnique<FBatchState>();
			State->Loops = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 3;
			const TArray<FString> Conditions = SplitList(Args.Num() > 1 ? Args[1] : FString(TEXT("N0,L100,L200,L300,J50-150,J100-300,L200-P3")));
			const TArray<FString> Modes = SplitList(Args.Num() > 2 ? Args[2] : FString(TEXT("0,1,2")));
			if (Args.Num() > 3) { State->SmoothTime = Args[3]; }
			if (!AddJobs(*State, Conditions, Modes, FString())) { return; }
			Begin(World, Script, MoveTemp(State), TEXT("B%m%d-%H%M"));
		}));

	/** rb.Plan groups. J: jitter size at a fixed 200 ms mean (where does the default start to pop?).
	 *  T: fixed smoothing time vs Ada-T (pop vs reaction trade-off). A: Ada-T tuning (default 3t is in T).
	 *  B: interpolation buffer (M4, the standard remedy) vs Exp and Ada-T g12 in the same session.
	 *  K: gain derived from the pop model (k = 7.6 keeps 99 % of corrections below the pop line), with and without a 0.30 s cap. */
	struct FPlanGroup { const TCHAR* Key; const TCHAR* Conditions; const TCHAR* Modes; };
	const FPlanGroup PlanGroups[] = {
		{ TEXT("J"), TEXT("L200,J190-210,J175-225,J150-250,J125-275,J100-300,J50-350"), TEXT("2,3t") },
		{ TEXT("T"), TEXT("N0,J50-150,J100-300"), TEXT("2,2@0.10,2@0.15,2@0.25,2@0.40,3t") },
		{ TEXT("A"), TEXT("N0,J100-300"), TEXT("3t:m0.40,3t:g12,3t:m0.40:g12") },
		{ TEXT("B"), TEXT("N0,J50-150,J100-300"), TEXT("2,3t:g12,4@0.05,4@0.10,4@0.15,4a") },
		{ TEXT("K"), TEXT("N0,J150-250,J100-300,J50-350"), TEXT("3t:g7.6,3t:g7.6:m0.30") },
	};

	FAutoConsoleCommandWithWorldAndArgs PlanCommand(TEXT("rb.Plan"),
		TEXT("B sweep experiments as one queue, repeat-major (every group once, then again), typed in the moving client's window.\n")
		TEXT("rb.Plan [Repeats=3] [Groups=J,T,A] [Loops=3]   (also B: buffer M4, K: model-derived gain)\n")
		TEXT("J: L200,J190-210..J50-350 x Exp,Ada-T | T: N0,J50-150,J100-300 x Exp 0.06/0.10/0.15/0.25/0.40, Ada-T | A: N0,J100-300 x Ada-T tuning"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			URubberbandInputScriptComponent* Script = FindScript(World, TEXT("rb.Plan"));
			if (!Script) { return; }
			const int32 Repeats = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 3;
			const TArray<FString> Groups = SplitList(Args.Num() > 1 ? Args[1] : FString(TEXT("J,T,A")));
			TUniquePtr<FBatchState> State = MakeUnique<FBatchState>();
			State->Loops = Args.Num() > 2 ? FMath::Max(1, FCString::Atoi(*Args[2])) : 3;
			for (int32 Repeat = 1; Repeat <= Repeats; ++Repeat)
			{
				for (const FString& Key : Groups)
				{
					const FPlanGroup* Group = nullptr;
					for (const FPlanGroup& Candidate : PlanGroups) { if (Key.Equals(Candidate.Key, ESearchCase::IgnoreCase)) { Group = &Candidate; } }
					if (!Group) { UE_LOG(LogRubberbandBatch, Warning, TEXT("rb.Plan: unknown group '%s' (J, T, A, B, K)"), *Key); return; }
					const FString Prefix = FString::Printf(TEXT("r%d%s_"), Repeat, Group->Key);
					if (!AddJobs(*State, SplitList(Group->Conditions), SplitList(Group->Modes), Prefix)) { return; }
				}
			}
			Begin(World, Script, MoveTemp(State), TEXT("P%m%d-%H%M"));
		}));

	FAutoConsoleCommandWithWorld StopBatchCommand(TEXT("rb.Batch.Stop"), TEXT("Cancel rb.Batch and clear the emulation it set."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld*) { Finish(TEXT("Cancelled")); }));
}
