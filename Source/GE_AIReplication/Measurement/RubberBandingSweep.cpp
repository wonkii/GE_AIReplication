#include "RubberBandingSweep.h"

#include "Engine/NetDriver.h"

#if !UE_BUILD_SHIPPING && DO_ENABLE_NET_TEST
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GE_AIReplicationCharacter.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "ObservedPlayerMovementComponent.h"
#include "RubberBandingCaptureSubsystem.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogRubberBandingSweep, Log, All);

namespace
{
	constexpr int32 DelaysMs[] = {0, 100, 200, 300};
	constexpr int32 StressDelayMs = 250;
	constexpr int32 MinStressLossPercent = 10;
	constexpr double WarmupSeconds = 1.0;
	constexpr double TailSeconds = 1.0;

	enum class EPhase { Reset, Warmup, Replay, Tail };
	enum class EProfile { Fixed, Jitter, Loss, Stress };

	const TCHAR* PatternName(ERubberBandingReplay Pattern)
	{
		switch (Pattern)
		{
		case ERubberBandingReplay::Turn90: return TEXT("Turn90");
		case ERubberBandingReplay::Turn180: return TEXT("Turn180");
		case ERubberBandingReplay::StateTransition: return TEXT("StateTransition");
		default: return TEXT("Standard");
		}
	}

	const TCHAR* ProfileName(EProfile Profile)
	{
		switch (Profile)
		{
		case EProfile::Jitter: return TEXT("Jitter");
		case EProfile::Loss: return TEXT("Loss");
		case EProfile::Stress: return TEXT("Stress");
		default: return TEXT("Fixed");
		}
	}

	struct FTrial
	{
		int32 DelayMs = 0;
		int32 Repetition = 1;
		ERubberBandingReplay Pattern = ERubberBandingReplay::Standard;
		EOwnerTestSpeedMode Variant = EOwnerTestSpeedMode::Baseline;
		TSharedPtr<FJsonObject> Result;

		const TCHAR* VariantName() const { return Variant == EOwnerTestSpeedMode::Improved ? TEXT("Improved") : TEXT("Baseline"); }
	};

	struct FParticipant
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UNetDriver> Driver;
		TWeakObjectPtr<URubberBandingCaptureSubsystem> Capture;
		FPacketSimulationSettings OriginalSettings;
	};

	struct FSweep
	{
		TArray<FParticipant> Participants;
		TWeakObjectPtr<UObservedPlayerMovementComponent> Mover;
		TWeakObjectPtr<UObservedPlayerMovementComponent> ServerMover;
		TWeakObjectPtr<APlayerController> MoverController;
		FVector StartPosition;
		FRotator StartRotation;
		FRotator StartView;
		FString Directory;
		FString TrialDirectory;
		TArray<FTrial> Trials;
		TSharedPtr<FJsonObject> Batch;
		int32 MoverId = INDEX_NONE;
		EProfile Profile = EProfile::Fixed;
		int32 LossPercent = 0;
		float WalkSpeed = 500.f;
		float RunSpeed = 800.f;
		int32 Completed = 0;
		uint32 StartInBytes = 0;
		uint32 StartOutBytes = 0;
		bool bOwnerTest = false;
		bool bCompare = false;
		bool bOwner = false;
		bool bDiagnostic = false;
		bool bDiagnosticRequested = false;
		int32 Trial = 0;
		int32 StartedCaptures = 0;
		EPhase Phase = EPhase::Reset;
		double PhaseStart = 0;
		double StableSince = 0;
	};

	TUniquePtr<FSweep> Sweep;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldTearDownHandle;
	int32 TrialSerial = 0;
	float ConfiguredWalkSpeed = 500.f;
	float ConfiguredRunSpeed = 800.f;

	bool WriteText(const FString& Directory, const TCHAR* Name, const FString& Text, uint32 Flags = 0)
	{
		return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, Name),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), Flags);
	}

	bool WriteReadme(const FString& Directory, const FString& Text, uint32 Flags = 0)
	{
		return WriteText(Directory, TEXT("README.md"), Text, Flags);
	}

	void AppendOutcome(const FString& Directory, const FString& Text)
	{
		UE_CLOG(!WriteReadme(Directory, TEXT("\n") + Text + TEXT("\n"), FILEWRITE_Append),
			LogRubberBandingSweep, Error, TEXT("Could not write outcome in %s"), *Directory);
	}

	bool CloseTrial(FSweep& Run, const TCHAR* Outcome)
	{
		bool bSaved = true;
		for (int32 Index = 0; Index < Run.StartedCaptures; ++Index)
		{
			URubberBandingCaptureSubsystem* Capture = Run.Participants[Index].Capture.Get();
			bSaved &= Capture && Capture->Stop(Outcome);
		}
		Run.StartedCaptures = 0;
		return bSaved;
	}

	bool WriteBatch(const FSweep& Run)
	{
		FString Json;
		return FJsonSerializer::Serialize(Run.Batch.ToSharedRef(), TJsonWriterFactory<>::Create(&Json)) &&
			WriteText(Run.Directory, TEXT("batch.json"), Json);
	}

	bool PrepareBatch(FSweep& Run, const FString& Command, int32 ObserverPIE, int32 MoverPIE)
	{
		Run.Batch = MakeShared<FJsonObject>();
		Run.Batch->SetStringField(TEXT("RunId"), FPaths::GetCleanFilename(Run.Directory));
		Run.Batch->SetStringField(TEXT("Command"), Command);
		Run.Batch->SetStringField(TEXT("StartedUtc"), FDateTime::UtcNow().ToIso8601());
		Run.Batch->SetStringField(TEXT("Outcome"), TEXT("Recording"));
		Run.Batch->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
		Run.Batch->SetStringField(TEXT("BuildVersion"), FApp::GetBuildVersion());
		Run.Batch->SetNumberField(TEXT("MoverPlayerId"), Run.MoverId);
		Run.Batch->SetNumberField(TEXT("MoverPacketLossPercentPerDirection"), Run.LossPercent);
		Run.Batch->SetNumberField(TEXT("ObserverPIE"), ObserverPIE);
		Run.Batch->SetNumberField(TEXT("MoverPIE"), MoverPIE);
		Run.Batch->SetStringField(TEXT("PacketRandomness"), TEXT("Engine RNG, repeated trials; no deterministic packet replay"));
		Run.Batch->SetStringField(TEXT("DrainCheck"), TEXT("One-second wait and oldest unacknowledged owner move age <= 250 ms"));
		TArray<TSharedPtr<FJsonValue>> Original;
		for (const FParticipant& Participant : Run.Participants)
		{
			Original.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Participant.OriginalSettings)));
		}
		Run.Batch->SetArrayField(TEXT("OriginalNetworkEmulation"), Original);
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (int32 Index = 0; Index < Run.Trials.Num(); ++Index)
		{
			FTrial& Trial = Run.Trials[Index];
			Trial.Result = MakeShared<FJsonObject>();
			auto& Entry = *Trial.Result;
			Entry.SetStringField(TEXT("RunId"), FPaths::GetCleanFilename(Run.Directory));
			Entry.SetNumberField(TEXT("TrialId"), Index + 1);
			Entry.SetNumberField(TEXT("PlayerId"), Run.MoverId);
			Entry.SetNumberField(TEXT("Repetition"), Trial.Repetition);
			Entry.SetNumberField(TEXT("DelayMsPerDirection"), Trial.DelayMs);
			Entry.SetNumberField(TEXT("PacketLossPercentPerDirection"), Run.LossPercent);
			Entry.SetStringField(TEXT("Scenario"), PatternName(Trial.Pattern));
			Entry.SetStringField(TEXT("ScenarioOrigin"),
				Trial.Pattern == ERubberBandingReplay::StateTransition ? TEXT("ControlledExperiment") : TEXT("GroundMovement"));
			Entry.SetStringField(TEXT("Profile"), ProfileName(Run.Profile));
			Entry.SetStringField(TEXT("Variant"), Trial.VariantName());
			Entry.SetStringField(TEXT("BuildVersion"), FApp::GetBuildVersion());
			Entry.SetNumberField(TEXT("WalkSpeed"), Run.WalkSpeed);
			Entry.SetNumberField(TEXT("RunSpeed"), Run.RunSpeed);
			Entry.SetBoolField(TEXT("SpeedOverrideEnabled"), Trial.Pattern == ERubberBandingReplay::StateTransition);
			Entry.SetStringField(TEXT("Outcome"), TEXT("Pending"));
			Entries.Add(MakeShared<FJsonValueObject>(Trial.Result));
		}
		Run.Batch->SetArrayField(TEXT("Trials"), Entries);
		const bool bPrepared = WriteBatch(Run) &&
			WriteText(Run.Directory, TEXT("summary.csv"),
				TEXT("TrialId,Scenario,Profile,Variant,DelayMs,Repetition,Outcome,CorrectionObserved,ServerErrorCount,Corrections,CorrectionsPerMinute,CapturedSeconds,")
				TEXT("ServerSamples,ServerP95Cm,ServerMaxCm,ReconciledSamples,ReconciliationP95Cm,ReconciliationMaxCm,")
				TEXT("FrameSamples,FrameP95Ms,FrameMaxMs,ReplaySamples,ReplayP95Ms,ReplayMaxMs,UnresolvedCorrections,")
				TEXT("ServerPathCm,ServerDisplacementCm,ServerEndX,ServerEndY,ServerEndZ,")
				TEXT("WalkToRunMatchedMoves,WalkToRunSpeedMismatches,RunToWalkMatchedMoves,RunToWalkSpeedMismatches,OwnerInBytes,OwnerOutBytes\n")) &&
			WriteText(Run.Directory, TEXT("summary.md"),
				TEXT("# Owner movement trials\n\nUse Completed rows for comparisons. N/A indicates no matching samples.\n\n")
				TEXT("| Trial | Scenario | Variant | Delay per direction (ms) | Repeat | Result | Corrections | Server p95 (cm) | Reconciliation max (cm) |\n")
				TEXT("|---|---|---|---:|---:|---|---:|---:|---:|\n"));
		if (!bPrepared)
		{
			AppendOutcome(Run.Directory, TEXT("Outcome Failed preparing batch output."));
			UE_LOG(LogRubberBandingSweep, Error, TEXT("Could not prepare OwnerTest outputs: %s"), *Run.Directory);
		}
		return bPrepared;
	}

	struct FDistribution
	{
		int32 Count = 0;
		double P95 = 0;
		double Max = 0;

		explicit FDistribution(TArray<double> Samples)
		{
			Count = Samples.Num();
			if (Count == 0) { return; }
			Samples.Sort();
			P95 = Samples[FMath::CeilToInt(Count * 0.95) - 1];
			Max = Samples.Last();
		}

		FString Columns() const
		{
			return Count ? FString::Printf(TEXT("%d,%.6f,%.6f"), Count, P95, Max) : TEXT("0,,");
		}
	};

	bool SaveTrialResult(FSweep& Run, const TCHAR* Outcome)
	{
		FTrial& Trial = Run.Trials[Run.Trial];
		Trial.Result->SetStringField(TEXT("Outcome"), Outcome);
		const auto& Owner = Run.Participants[1].Capture->GetTrialMetrics();
		const auto& Server = Run.Participants[2].Capture->GetTrialMetrics();
		const FDistribution Errors(Server.ServerErrorsCm);
		const FDistribution Correction(Owner.ReconciliationCm);
		const FDistribution Frames(Owner.FrameGapsMs);
		const FDistribution Replay(Owner.ReplayMs);
		const double Seconds = (Owner.LastSample - Owner.FirstSample) * FPlatformTime::GetSecondsPerCycle64();
		const double Displacement = Server.FirstServerPosition.IsSet() ?
			FVector::Distance(Server.FirstServerPosition.GetValue(), Server.LastServerPosition) : 0.;
		const FString Rate = Seconds > 0 ? FString::Printf(TEXT("%.6f"), Owner.CorrectionCount * 60. / Seconds) : FString();
		int32 Matched[2] = {}, StateMismatches[2] = {};
		int32 OwnerIndex = 0;
		for (const auto& Checked : Server.CheckedMoves)
		{
			while (Owner.PredictedMoves.IsValidIndex(OwnerIndex))
			{
				const auto& Predicted = Owner.PredictedMoves[OwnerIndex];
				if (Predicted.Epoch > Checked.Epoch ||
					(Predicted.Epoch == Checked.Epoch && Predicted.Timestamp > Checked.Timestamp)) { break; }
				++OwnerIndex;
				const int32 Transition = Predicted.TransitionId - 1;
				if (Predicted.Epoch == Checked.Epoch && Predicted.Timestamp == Checked.Timestamp && Transition >= 0 && Transition < 2)
				{
					++Matched[Transition];
					StateMismatches[Transition] += !FMath::IsNearlyEqual(Predicted.MaxSpeed, Checked.MaxSpeed);
				}
			}
		}
		const UNetDriver* Driver = Run.Participants[1].Driver.Get();
		const UNetConnection* Connection = Driver ? Driver->ServerConnection : nullptr;
		const FString NetBytes = Connection ? FString::Printf(TEXT("%u,%u"), static_cast<uint32>(Connection->InTotalBytes) - Run.StartInBytes,
			static_cast<uint32>(Connection->OutTotalBytes) - Run.StartOutBytes) : TEXT(",");
		Trial.Result->SetBoolField(TEXT("CorrectionObserved"), Owner.CorrectionCount > 0);
		Trial.Result->SetNumberField(TEXT("Corrections"), Owner.CorrectionCount);
		Trial.Result->SetNumberField(TEXT("UnresolvedCorrections"), Owner.CorrectionCount - Correction.Count);
		if (Errors.Count) { Trial.Result->SetNumberField(TEXT("ServerErrorP95Cm"), Errors.P95); }
		if (Correction.Count) { Trial.Result->SetNumberField(TEXT("ReconciliationMaxCm"), Correction.Max); }
		const FString Csv = FString::Printf(TEXT("%d,%s,%s,%s,%d,%d,%s,%s,%d,%d,%s,%.6f,%s,%s,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%s\n"),
			Run.Trial + 1, PatternName(Trial.Pattern), ProfileName(Run.Profile), Trial.VariantName(), Trial.DelayMs,
			Trial.Repetition, Outcome, Owner.CorrectionCount ? TEXT("true") : TEXT("false"),
			Server.ServerErrorCount, Owner.CorrectionCount, *Rate, Seconds, *Errors.Columns(), *Correction.Columns(),
			*Frames.Columns(), *Replay.Columns(), Owner.CorrectionCount - Correction.Count, Server.ServerPathCm, Displacement,
			Server.LastServerPosition.X, Server.LastServerPosition.Y, Server.LastServerPosition.Z,
			Matched[0], StateMismatches[0], Matched[1], StateMismatches[1], *NetBytes);
		const FString Markdown = FString::Printf(TEXT("| %d | %s | %s | %d | %d | %s | %d | %s | %s |\n"),
			Run.Trial + 1, PatternName(Trial.Pattern), Trial.VariantName(), Trial.DelayMs, Trial.Repetition, Outcome,
			Owner.CorrectionCount, Errors.Count ? *FString::Printf(TEXT("%.3f"), Errors.P95) : TEXT("N/A"),
			Correction.Count ? *FString::Printf(TEXT("%.3f"), Correction.Max) : TEXT("N/A"));
		const bool bCsv = WriteText(Run.Directory, TEXT("summary.csv"), Csv, FILEWRITE_Append);
		const bool bMarkdown = WriteText(Run.Directory, TEXT("summary.md"), Markdown, FILEWRITE_Append);
		if (!bCsv || !bMarkdown) { Trial.Result->SetStringField(TEXT("Outcome"), TEXT("Failed")); }
		return WriteBatch(Run) && bCsv && bMarkdown;
	}

	bool WriteComparisons(const FSweep& Run)
	{
		FString Section = TEXT("\n## Baseline / Improved\n\n")
			TEXT("Only pairs with both trials completed are included. N/A applies when the baseline count is zero.\n\n")
			TEXT("| Scenario | Delay (ms) | Repeat | Baseline trial | Improved trial | Corrections B → I | Reduction | Server p95 B → I (cm) | Reconciliation max B → I (cm) |\n")
			TEXT("|---|---:|---:|---:|---:|---|---|---|---|\n");
		for (int32 Index = 1; Index < Run.Trials.Num(); ++Index)
		{
			const FTrial& A = Run.Trials[Index - 1];
			const FTrial& B = Run.Trials[Index];
			if (A.Pattern != B.Pattern || A.DelayMs != B.DelayMs || A.Repetition != B.Repetition || A.Variant == B.Variant ||
				A.Result->GetStringField(TEXT("Outcome")) != TEXT("Completed") || B.Result->GetStringField(TEXT("Outcome")) != TEXT("Completed")) { continue; }
			const bool bBaselineFirst = A.Variant == EOwnerTestSpeedMode::Baseline;
			const auto& Baseline = *(bBaselineFirst ? A.Result : B.Result);
			const auto& Improved = *(bBaselineFirst ? B.Result : A.Result);
			const double OldCount = Baseline.GetNumberField(TEXT("Corrections"));
			const double NewCount = Improved.GetNumberField(TEXT("Corrections"));
			double OldP95 = 0, NewP95 = 0;
			const bool bHasP95 = Baseline.TryGetNumberField(TEXT("ServerErrorP95Cm"), OldP95) &&
				Improved.TryGetNumberField(TEXT("ServerErrorP95Cm"), NewP95);
			const auto Reconciliation = [](const FJsonObject& Entry)
			{
				double Max = 0;
				return Entry.TryGetNumberField(TEXT("ReconciliationMaxCm"), Max) ?
					FString::Printf(TEXT("%.3f"), Max) : FString(TEXT("N/A"));
			};
			Section += FString::Printf(TEXT("| %s | %d | %d | %d | %d | %.0f → %.0f | %s | %s | %s → %s |\n"), PatternName(A.Pattern),
				A.DelayMs, A.Repetition, bBaselineFirst ? Index : Index + 1, bBaselineFirst ? Index + 1 : Index, OldCount, NewCount,
				OldCount > 0 ? *FString::Printf(TEXT("%.1f%%"), 100.0 * (OldCount - NewCount) / OldCount) : TEXT("N/A"),
				bHasP95 ? *FString::Printf(TEXT("%.3f → %.3f"), OldP95, NewP95) : TEXT("N/A"),
				*Reconciliation(Baseline), *Reconciliation(Improved));
		}
		return WriteText(Run.Directory, TEXT("summary.md"), Section, FILEWRITE_Append);
	}

	void Finish(const TCHAR* Outcome)
	{
		if (!Sweep) { return; }
		FWorldDelegates::OnWorldBeginTearDown.Remove(WorldTearDownHandle);
		WorldTearDownHandle.Reset();
		if (UObservedPlayerMovementComponent* Movement = Sweep->Mover.Get()) { Movement->CancelInputReplay(); }
		if (UObservedPlayerMovementComponent* Movement = Sweep->ServerMover.Get()) { Movement->ClearDiagnosticCorrection(); }
		const bool bTrialWasOpen = Sweep->StartedCaptures == Sweep->Participants.Num();
		CloseTrial(*Sweep, TEXT("Incomplete"));
		if (Sweep->bOwnerTest)
		{
			if (bTrialWasOpen && Sweep->Participants[1].Capture.IsValid() && Sweep->Participants[2].Capture.IsValid())
			{
				SaveTrialResult(*Sweep, FCString::Strcmp(Outcome, TEXT("Cancelled")) == 0 ? TEXT("Cancelled") : TEXT("Failed"));
			}
			if (Sweep->Trials.IsValidIndex(Sweep->Trial))
			{
				auto& Entry = *Sweep->Trials[Sweep->Trial].Result;
				const FString TrialOutcome = Entry.GetStringField(TEXT("Outcome"));
				if (TrialOutcome == TEXT("Pending") || TrialOutcome == TEXT("Recording"))
				{
					Entry.SetStringField(TEXT("Outcome"), FCString::Strcmp(Outcome, TEXT("Cancelled")) == 0 ? TEXT("Cancelled") : TEXT("Failed"));
				}
			}
			for (const auto& Entry : {Sweep->Mover, Sweep->ServerMover})
			{
				if (auto* Movement = Entry.Get()) { Movement->ConfigureOwnerTestSpeed(EOwnerTestSpeedMode::Off, 0, 0, 0); }
			}
		}
		TArray<TSharedPtr<FJsonValue>> Restored;
		bool bRestored = true;
		for (const FParticipant& Participant : Sweep->Participants)
		{
			if (UNetDriver* Driver = Participant.Driver.Get())
			{
				Driver->SetPacketSimulationSettings(Participant.OriginalSettings);
				Restored.Add(MakeShared<FJsonValueObject>(FJsonObjectConverter::UStructToJsonObject(Driver->PacketSimulationSettings)));
				bRestored &= FPacketSimulationSettings::StaticStruct()->CompareScriptStruct(
					&Driver->PacketSimulationSettings, &Participant.OriginalSettings, 0);
			}
			else { Restored.Add(MakeShared<FJsonValueNull>()); }
		}
		if (Sweep->bOwnerTest)
		{
			if (!bRestored) { Outcome = TEXT("Failed: packet emulation restoration mismatch"); }
			if (Sweep->bCompare && !WriteComparisons(*Sweep))
			{
				Outcome = TEXT("Failed: comparison summary write failed");
			}
			Sweep->Batch->SetStringField(TEXT("Outcome"), Outcome);
			Sweep->Batch->SetArrayField(TEXT("RestoredNetworkEmulation"), Restored);
			if (!WriteBatch(*Sweep)) { Outcome = TEXT("Failed: batch.json write failed"); }
		}
		AppendOutcome(Sweep->Directory, FString::Printf(TEXT("Outcome %s (%d/%d completed)."), Outcome, Sweep->Completed, Sweep->Trials.Num()));
		UE_LOG(LogRubberBandingSweep, Display, TEXT("%s (%d/%d): %s"), Outcome, Sweep->Completed, Sweep->Trials.Num(), *Sweep->Directory);
		Sweep.Reset();
	}

	bool SetDelay(const FParticipant& Participant, int32 IncomingMs, int32 OutgoingMs,
		EProfile Profile = EProfile::Fixed, int32 LossPercent = 0)
	{
		FPacketSimulationSettings Settings;
		Settings.PktIncomingLagMin = Settings.PktIncomingLagMax = IncomingMs;
		Settings.PktLagMin = Settings.PktLagMax = OutgoingMs;
		if (Profile == EProfile::Jitter)
		{
			Settings.PktIncomingLagMin = FMath::Max(0, IncomingMs - 50);
			Settings.PktIncomingLagMax = IncomingMs + 50;
			Settings.PktLagMin = FMath::Max(0, OutgoingMs - 50);
			Settings.PktLagMax = OutgoingMs + 50;
		}
		Settings.PktIncomingLoss = Settings.PktLoss = LossPercent;
		UNetDriver* Driver = Participant.Driver.Get();
		Driver->SetPacketSimulationSettings(Settings);
		const FPacketSimulationSettings& Applied = Driver->PacketSimulationSettings;
		return Applied.PktIncomingLagMin == Settings.PktIncomingLagMin && Applied.PktIncomingLagMax == Settings.PktIncomingLagMax &&
			Applied.PktLagMin == Settings.PktLagMin && Applied.PktLagMax == Settings.PktLagMax &&
			Applied.PktIncomingLoss == Settings.PktIncomingLoss && Applied.PktLoss == Settings.PktLoss &&
			Applied.PktLag == 0 && Applied.PktOrder == 0 && Applied.PktDup == 0 &&
			Applied.PktJitter == 0 && Applied.PktFrameDelay == 0;
	}

	bool IsReady(UNetDriver* Driver, UWorld* World)
	{
		return Driver && World && World->GetNetDriver() == Driver && World->HasBegunPlay() && !World->bIsTearingDown &&
			(World->GetNetMode() == NM_DedicatedServer ||
			 (Driver->ServerConnection && Driver->ServerConnection->GetConnectionState() == USOCK_Open));
	}

	bool StartTrial(FSweep& Run)
	{
		const FTrial& Trial = Run.Trials[Run.Trial];
		const int32 Ms = Trial.DelayMs;
		const int32 MoverMs = Run.bOwner ? Ms : 0;
		const int32 ObserverMs = Run.bOwner ? 0 : Ms;
		const FString Folder = Run.bOwnerTest ? FString::Printf(TEXT("%03d-%03dms-%s-%s"),
			Run.Trial + 1, Ms, PatternName(Trial.Pattern), Trial.VariantName()) : FString::Printf(TEXT("%03dms"), Ms);
		Run.TrialDirectory = FPaths::Combine(Run.Directory, Folder);
		if (Trial.Result.IsValid())
		{
			Trial.Result->SetStringField(TEXT("Directory"), Folder);
			Trial.Result->SetStringField(TEXT("Outcome"), TEXT("Recording"));
		}
		if (!IFileManager::Get().MakeDirectory(*Run.TrialDirectory, true) ||
			!WriteReadme(Run.TrialDirectory, FString::Printf(
				TEXT("# RubberBanding trial at %d ms\n\nCreated (UTC) %s by %s.\n\n")
				TEXT("Profile %s. Observer receive %d ms. Mover send and receive nominally %d ms each.\n")
				TEXT("Mover packet loss setting: %d%% per direction. Actual losses vary by trial.\n")
				TEXT("See the batch README for roles and CSV usage. Keep the raw CSV and manifests.\n"),
				Ms, *FDateTime::UtcNow().ToIso8601(), Run.bOwnerTest ? TEXT("RubberBanding.OwnerTest") : TEXT("RubberBanding.Sweep"),
				ProfileName(Run.Profile), ObserverMs, MoverMs, Run.LossPercent)))
		{
			return false;
		}
		for (const FParticipant& Participant : Run.Participants)
		{
			if (!Participant.Capture->Start(Run.TrialDirectory, Trial.Result, Run.MoverId)) { return false; }
			++Run.StartedCaptures;
		}
		if (const UNetConnection* Connection = Run.Participants[1].Driver->ServerConnection)
		{
			Run.StartInBytes = static_cast<uint32>(Connection->InTotalBytes);
			Run.StartOutBytes = static_cast<uint32>(Connection->OutTotalBytes);
		}
		return true;
	}

	bool TickSweep(float)
	{
		FSweep& Run = *Sweep;
		for (const FParticipant& Participant : Run.Participants)
		{
			if (!IsReady(Participant.Driver.Get(), Participant.World.Get()) || !Participant.Capture.IsValid())
			{
				Finish(TEXT("Aborted: a PIE world or connection ended"));
				return false;
			}
		}
		UObservedPlayerMovementComponent* Movement = Run.Mover.Get();
		UObservedPlayerMovementComponent* ServerMovement = Run.ServerMover.Get();
		APlayerController* Controller = Run.MoverController.Get();
		ACharacter* Pawn = Movement ? Movement->GetCharacterOwner() : nullptr;
		ACharacter* ServerPawn = ServerMovement ? ServerMovement->GetCharacterOwner() : nullptr;
		if (!Controller || !IsValid(Pawn) || !IsValid(ServerPawn) || Controller->GetPawn() != Pawn ||
			Pawn->GetLocalRole() != ROLE_AutonomousProxy || !ServerPawn->GetController())
		{
			Finish(TEXT("Aborted: mover pawn or possession changed"));
			return false;
		}
		for (int32 Index = 0; Index < Run.StartedCaptures; ++Index)
		{
			if (!URubberBandingCaptureSubsystem::GetActive(Run.Participants[Index].World.Get()))
			{
				Finish(TEXT("Aborted: capture failed"));
				return false;
			}
		}
		const double Now = FPlatformTime::Seconds();
		const double Elapsed = Now - Run.PhaseStart;
		const FTrial& Trial = Run.Trials[Run.Trial];
		switch (Run.Phase)
		{
		case EPhase::Reset:
			if (Elapsed < WarmupSeconds || (Run.bOwnerTest && Movement->GetPendingMoveAge() > 0.25))
			{
				if (Elapsed < 8.0) { break; }
				Finish(TEXT("Failed: pending movement did not drain"));
				return false;
			}
			if (Run.bOwnerTest)
			{
				const auto Mode = Trial.Pattern == ERubberBandingReplay::StateTransition ? Trial.Variant : EOwnerTestSpeedMode::Off;
				++TrialSerial;
				Movement->ConfigureOwnerTestSpeed(Mode, Run.WalkSpeed, Run.RunSpeed, TrialSerial);
				ServerMovement->ConfigureOwnerTestSpeed(Mode, Run.WalkSpeed, Run.RunSpeed, TrialSerial);
			}
			// Reset both copies outside recording, after the previous delayed packets drain.
			// Use collision-checked teleports and verify both copies before replay.
			if (Run.Trial > 0)
			{
				Movement->StopMovementImmediately();
				ServerMovement->StopMovementImmediately();
				Controller->SetControlRotation(Run.StartView);
				if (!ServerPawn->TeleportTo(Run.StartPosition, Run.StartRotation) ||
					!Pawn->TeleportTo(Run.StartPosition, Run.StartRotation))
				{
					Finish(TEXT("Aborted: could not return mover to its starting position"));
					return false;
				}
			}
			{
				const int32 Ms = Trial.DelayMs;
				const int32 MoverMs = Run.bOwner ? Ms : 0;
				if (!SetDelay(Run.Participants[0], Run.bOwner ? 0 : Ms, 0) ||
				!SetDelay(Run.Participants[1], MoverMs, MoverMs, Run.Profile, Run.LossPercent))
				{
					Finish(TEXT("Aborted: packet emulation settings were rejected"));
					return false;
				}
			}
			Run.Phase = EPhase::Warmup;
			Run.PhaseStart = Now;
			Run.StableSince = 0;
			break;
		case EPhase::Warmup:
			if (!Pawn->GetActorLocation().Equals(Run.StartPosition, 3.0) ||
				!ServerPawn->GetActorLocation().Equals(Run.StartPosition, 3.0) ||
				(Run.bOwnerTest && (!Movement->Velocity.IsNearlyZero(1.f) || !ServerMovement->Velocity.IsNearlyZero(1.f) ||
					!Movement->IsMovingOnGround() || !ServerMovement->IsMovingOnGround() ||
					Movement->IsOwnerTestRunning() || ServerMovement->IsOwnerTestRunning())) ||
				!FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Controller->GetControlRotation().Yaw, Run.StartView.Yaw), 1.0))
			{
				Run.StableSince = 0;
				if (Elapsed < (Run.bOwnerTest ? 8.0 : WarmupSeconds)) { break; }
				Finish(TEXT("Aborted: the mover did not settle at the original pose"));
				return false;
			}
			if (Run.StableSince == 0) { Run.StableSince = Now; }
			if (Elapsed < WarmupSeconds || (Run.bOwnerTest && Now - Run.StableSince < 0.25)) { break; }
			if (!StartTrial(Run) || !Movement->StartInputReplay(Trial.Pattern))
			{
				Finish(TEXT("Aborted: could not start trial capture or input replay"));
				return false;
			}
			UE_LOG(LogRubberBandingSweep, Display, TEXT("Replay at %d ms (%d/%d): %s"), Trial.DelayMs,
				Run.Trial + 1, Run.Trials.Num(), *Run.TrialDirectory);
			Run.Phase = EPhase::Replay;
			Run.PhaseStart = Now;
			Run.bDiagnosticRequested = false;
			break;
		case EPhase::Replay:
			if (Movement->IsInputReplayActive())
			{
				if (Run.bDiagnostic && !Run.bDiagnosticRequested && Elapsed >= 0.75)
				{
					ServerMovement->RequestDiagnosticCorrection();
					Run.bDiagnosticRequested = true;
				}
				if (Elapsed < 30.0) { break; }
				Finish(TEXT("Aborted: replay timed out"));
				return false;
			}
			if (!Movement->DidInputReplayComplete())
			{
				if (Run.bOwnerTest && Movement->HadInvalidReplayTiming())
				{
					Run.Phase = EPhase::Tail;
					Run.PhaseStart = Now;
					break;
				}
				Finish(TEXT("Aborted: input replay cancelled"));
				return false;
			}
			Run.Phase = EPhase::Tail;
			Run.PhaseStart = Now;
			break;
		case EPhase::Tail:
			if (Elapsed < TailSeconds) { break; }
			{
				const TArray<float>& Corrections = Movement->GetObservedCorrectionTimestamps();
				const TOptional<float> DiagnosticTimestamp = ServerMovement->GetDiagnosticCorrectionTimestamp();
				const bool bDiagnosticMissing = Run.bDiagnostic &&
					(!Run.bDiagnosticRequested || !DiagnosticTimestamp.IsSet() || !Corrections.Contains(DiagnosticTimestamp.GetValue()));
				ServerMovement->ClearDiagnosticCorrection();
				AppendOutcome(Run.TrialDirectory, FString::Printf(TEXT("Mover corrections received: %d.%s"), Corrections.Num(),
					bDiagnosticMissing ? TEXT(" Diagnostic correction was not confirmed.") : TEXT("")));
				const bool bInvalid = Run.bOwnerTest && Movement->HadInvalidReplayTiming();
				const bool bSaved = CloseTrial(Run, bDiagnosticMissing ? TEXT("Incomplete") : bInvalid ? TEXT("InvalidTiming") : TEXT("Stopped"));
				const TCHAR* Result = !bSaved || bDiagnosticMissing ? TEXT("Failed") : bInvalid ? TEXT("InvalidTiming") : TEXT("Completed");
				const bool bSummarized = !Run.bOwnerTest || SaveTrialResult(Run, Result);
				if (!bSaved || !bSummarized || bDiagnosticMissing)
				{
					Finish(TEXT("Aborted: trial output or diagnostic correction incomplete"));
					return false;
				}
				if (!bInvalid) { ++Run.Completed; }
			}
			if (++Run.Trial == Run.Trials.Num())
			{
				Finish(Run.Completed == Run.Trials.Num() ? TEXT("Completed") : TEXT("Finished with InvalidTiming trials"));
				return false;
			}
			for (const FParticipant& Participant : Run.Participants)
			{
				if (!SetDelay(Participant, 0, 0))
				{
					Finish(TEXT("Failed: could not clear packet emulation between trials"));
					return false;
				}
			}
			Run.Phase = EPhase::Reset;
			Run.PhaseStart = Now;
			break;
		}
		return true;
	}

	void StartRun(UWorld* World, bool bOwner, bool bDiagnostic, bool bOwnerTest, EProfile Profile,
		TArray<FTrial> Trials, const FString& Command, bool bCompare = false, int32 LossPercent = 0)
	{
		if (Sweep || !World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Client)
		{
			UE_LOG(LogRubberBandingSweep, Warning, TEXT("Start a single sweep from a connected PIE client."));
			return;
		}
		TArray<UWorld*> Clients{World};
		TArray<UWorld*> Servers;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (!Candidate || Candidate == World || Candidate->WorldType != EWorldType::PIE) { continue; }
			if (Candidate->GetNetMode() == NM_Client) { Clients.Add(Candidate); }
			if (Candidate->GetNetMode() == NM_DedicatedServer) { Servers.Add(Candidate); }
		}
		if (Clients.Num() != 2 || Servers.Num() != 1)
		{
			UE_LOG(LogRubberBandingSweep, Warning, TEXT("Use two PIE players, Play As Client and Run Under One Process."));
			return;
		}

		TUniquePtr<FSweep> Run = MakeUnique<FSweep>();
		for (UWorld* ParticipantWorld : {Clients[0], Clients[1], Servers[0]})
		{
			UNetDriver* Driver = ParticipantWorld->GetNetDriver();
			URubberBandingCaptureSubsystem* Capture = ParticipantWorld->GetSubsystem<URubberBandingCaptureSubsystem>();
			if (!IsReady(Driver, ParticipantWorld) || !Capture || URubberBandingCaptureSubsystem::GetActive(ParticipantWorld))
			{
				UE_LOG(LogRubberBandingSweep, Warning, TEXT("Wait for both clients to connect and stop any manual capture first."));
				return;
			}
			Run->Participants.Add({ParticipantWorld, Driver, Capture, Driver->PacketSimulationSettings});
		}
		APlayerController* Controller = Clients[1]->GetFirstPlayerController();
		ACharacter* Pawn = Controller && Controller->IsLocalController() ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
		APlayerState* State = Pawn ? Pawn->GetPlayerState() : nullptr;
		UObservedPlayerMovementComponent* Movement = Pawn ? Cast<UObservedPlayerMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
		UObservedPlayerMovementComponent* ServerMovement = nullptr;
		if (State)
		{
			for (FConstPlayerControllerIterator It = Servers[0]->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* ServerController = It->Get();
				const APlayerState* ServerState = ServerController ? ServerController->PlayerState.Get() : nullptr;
				if (ServerState && ServerState->GetPlayerId() == State->GetPlayerId())
				{
					const ACharacter* ServerPawn = Cast<ACharacter>(ServerController->GetPawn());
					ServerMovement = ServerPawn ? Cast<UObservedPlayerMovementComponent>(ServerPawn->GetCharacterMovement()) : nullptr;
					break;
				}
			}
		}
		if (!Movement || !ServerMovement || !Cast<AGE_AIReplicationCharacter>(Pawn) || Pawn->GetLocalRole() != ROLE_AutonomousProxy ||
			!Movement->IsMovingOnGround() || !Movement->Velocity.IsNearlyZero(1.f) || Movement->IsInputReplayActive())
		{
			UE_LOG(LogRubberBandingSweep, Warning, TEXT("The other client must possess a stationary observed character on the ground."));
			return;
		}
		Run->Mover = Movement;
		Run->ServerMover = ServerMovement;
		Run->MoverController = Controller;
		Run->StartPosition = Pawn->GetActorLocation();
		Run->StartRotation = Pawn->GetActorRotation();
		Run->StartView = Controller->GetControlRotation();
		Run->bOwner = bOwner;
		Run->bDiagnostic = bDiagnostic;
		Run->bOwnerTest = bOwnerTest;
		Run->bCompare = bCompare;
		Run->Profile = Profile;
		Run->LossPercent = LossPercent;
		Run->Trials = MoveTemp(Trials);
		Run->MoverId = State->GetPlayerId();
		Run->WalkSpeed = ConfiguredWalkSpeed;
		Run->RunSpeed = ConfiguredRunSpeed;
		const int32 ObserverPIE = GEngine->GetWorldContextFromWorld(World)->PIEInstance;
		const int32 MoverPIE = GEngine->GetWorldContextFromWorld(Clients[1])->PIEInstance;
		Run->Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tasks"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + (bOwnerTest ? TEXT("-RB-OwnerTest-") : TEXT("-RB-Sweep-")) +
			FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
		if (!IFileManager::Get().MakeDirectory(*Run->Directory, true) || !WriteReadme(Run->Directory, FString::Printf(
			TEXT("# RubberBanding experiment\n\nCreated (UTC) %s by %s to measure movement.\n\n")
			TEXT("Observer Client PIE %d (command window). Mover Client PIE %d, PlayerId %d.\n")
			TEXT("Open each delay folder's per-world manifest.json and events.csv. Filter CSV by mover PlayerId.\n")
			TEXT("Observer: RemoteMovementNotified and RemoteCapsuleApplied. Mover: OwnerCorrectionReceived and OwnerReplay.\n")
			TEXT("Server: ServerErrorCheck and, in Probe mode, ServerDiagnosticCorrection. Match owner corrections by MoveTimestamp.\n")
			TEXT("OwnerTest includes batch.json (plan and outcomes), summary.csv and summary.md (trial metrics).\n")
			TEXT("Keep raw CSV and manifests with the experiment results. Recording outcome pending.\n"),
			*FDateTime::UtcNow().ToIso8601(), *Command, ObserverPIE, MoverPIE, State->GetPlayerId())))
		{
			UE_LOG(LogRubberBandingSweep, Error, TEXT("Could not prepare sweep directory %s"), *Run->Directory);
			return;
		}
		if (bOwnerTest && !PrepareBatch(*Run, Command, ObserverPIE, MoverPIE)) { return; }
		Sweep = MoveTemp(Run);
		for (const FParticipant& Participant : Sweep->Participants)
		{
			if (!SetDelay(Participant, 0, 0))
			{
				Finish(TEXT("Aborted: packet emulation settings were rejected"));
				return;
			}
		}
		Sweep->PhaseStart = FPlatformTime::Seconds();
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickSweep));
		WorldTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddLambda([](UWorld* EndingWorld)
		{
			if (IsRubberBandingSweepActive(EndingWorld))
			{
				FTSTicker::RemoveTicker(TickHandle);
				Finish(TEXT("Aborted: PIE world ended"));
			}
		});
		UE_LOG(LogRubberBandingSweep, Display, TEXT("Started %s (%d trials), observer PIE %d, mover PIE %d: %s"),
			*Command, Sweep->Trials.Num(), ObserverPIE, MoverPIE, *Sweep->Directory);
	}

	void CancelRun()
	{
		FTSTicker::RemoveTicker(TickHandle);
		Finish(TEXT("Cancelled"));
	}

	void StartSweep(const TArray<FString>& Args, UWorld* World)
	{
		const bool bObserver = Args.Num() == 1 && Args[0].Equals(TEXT("Observer"), ESearchCase::IgnoreCase);
		const bool bOwner = Args.Num() >= 1 && Args[0].Equals(TEXT("Owner"), ESearchCase::IgnoreCase);
		const bool bDiagnostic = bOwner && Args.Num() == 2 && Args[1].Equals(TEXT("Probe"), ESearchCase::IgnoreCase);
		if (!bObserver && !(bOwner && (Args.Num() == 1 || bDiagnostic)))
		{
			UE_LOG(LogRubberBandingSweep, Warning, TEXT("Usage: RubberBanding.Sweep Observer or RubberBanding.Sweep Owner [Probe]"));
			return;
		}
		TArray<FTrial> Trials;
		for (const int32 Delay : DelaysMs) { Trials.Add(FTrial{Delay}); }
		StartRun(World, bOwner, bDiagnostic, false, EProfile::Fixed, MoveTemp(Trials),
			TEXT("RubberBanding.Sweep ") + FString::Join(Args, TEXT(" ")));
	}

	void RunOwnerTest(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() == 1 && Args[0].Equals(TEXT("Status"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogRubberBandingSweep, Display, TEXT("Configured Walk %.2f / Run %.2f cm/s"), ConfiguredWalkSpeed, ConfiguredRunSpeed);
			if (IsRubberBandingSweepActive(World) && Sweep->bOwnerTest)
			{
				const FTrial& Trial = Sweep->Trials[Sweep->Trial];
				UE_LOG(LogRubberBandingSweep, Display, TEXT("Trial %d/%d, %s %s %s, %d ms and %d%% loss per direction, repeat %d. Estimated %.0f s for remaining trials. %s"),
					Sweep->Trial + 1, Sweep->Trials.Num(), PatternName(Trial.Pattern), Trial.VariantName(), ProfileName(Sweep->Profile),
					Trial.DelayMs, Sweep->LossPercent, Trial.Repetition, (Sweep->Trials.Num() - Sweep->Trial) * 8.0, *Sweep->Directory);
			}
			else { UE_LOG(LogRubberBandingSweep, Display, TEXT("No OwnerTest is active in this PIE session.")); }
			return;
		}
		if (Args.Num() == 1 && Args[0].Equals(TEXT("Cancel"), ESearchCase::IgnoreCase))
		{
			if (IsRubberBandingSweepActive(World) && Sweep->bOwnerTest) { CancelRun(); }
			else { UE_LOG(LogRubberBandingSweep, Display, TEXT("No OwnerTest is active in this PIE session.")); }
			return;
		}
		if (!Args.IsEmpty() && Args[0].Equals(TEXT("Speeds"), ESearchCase::IgnoreCase))
		{
			float Walk = 0.f, Run = 0.f;
			if (!Sweep && World && World->WorldType == EWorldType::PIE && World->GetNetMode() == NM_Client &&
				Args.Num() == 3 && LexTryParseString(Walk, *Args[1]) && LexTryParseString(Run, *Args[2]) &&
				FMath::IsFinite(Walk) && FMath::IsFinite(Run) && Walk > 0.f && Run > Walk)
			{
				ConfiguredWalkSpeed = Walk;
				ConfiguredRunSpeed = Run;
				UE_LOG(LogRubberBandingSweep, Display, TEXT("StateTransition speeds: Walk %.2f / Run %.2f cm/s"), Walk, Run);
			}
			else
			{
				UE_LOG(LogRubberBandingSweep, Warning, TEXT("Use OwnerTest Speeds <positive Walk> <Run greater than Walk> in an idle PIE client. Current %.2f / %.2f cm/s."),
					ConfiguredWalkSpeed, ConfiguredRunSpeed);
			}
			return;
		}
		const ERubberBandingReplay AllPatterns[] = {ERubberBandingReplay::Turn90, ERubberBandingReplay::Turn180, ERubberBandingReplay::StateTransition};
		TArray<ERubberBandingReplay> Patterns;
		const bool bCompare = !Args.IsEmpty() && Args[0].Equals(TEXT("Compare"), ESearchCase::IgnoreCase);
		for (const auto Pattern : AllPatterns)
		{
			if (bCompare || (!Args.IsEmpty() && Args[0].Equals(PatternName(Pattern), ESearchCase::IgnoreCase))) { Patterns.Add(Pattern); }
		}
		EProfile Profile = EProfile::Fixed;
		bool bProfileValid = false;
		for (const EProfile Candidate : {EProfile::Fixed, EProfile::Jitter, EProfile::Loss, EProfile::Stress})
		{
			if (Args.Num() >= 2 && Args[1].Equals(ProfileName(Candidate), ESearchCase::IgnoreCase))
			{
				Profile = Candidate;
				bProfileValid = true;
				break;
			}
		}
		int32 Repeats = 5;
		const bool bStress = Profile == EProfile::Stress;
		int32 LossPercent = bStress ? MinStressLossPercent : Profile == EProfile::Loss ? 5 : 0;
		if (Patterns.IsEmpty() || !bProfileValid || Args.Num() > (bStress ? 4 : 3) ||
			(Args.Num() >= 3 && !LexTryParseString(Repeats, *Args[2])) || Repeats < 1 || Repeats > 20 ||
			(Args.Num() == 4 && (!LexTryParseString(LossPercent, *Args[3]) || LossPercent < MinStressLossPercent || LossPercent >= 100)))
		{
			UE_LOG(LogRubberBandingSweep, Warning, TEXT("Usage: RubberBanding.OwnerTest <Turn90|Turn180|StateTransition|Compare> <Fixed|Jitter|Loss|Stress> [repeats 1..20] [Stress loss percent 10..99]"));
			return;
		}
		const TConstArrayView<int32> TrialDelays = bStress ? TConstArrayView<int32>(&StressDelayMs, 1) : MakeArrayView(DelaysMs);
		TArray<FTrial> Trials;
		for (const auto Pattern : Patterns)
		{
			for (const int32 Delay : TrialDelays)
			{
				if (Profile == EProfile::Jitter && Delay == 0) { continue; }
				for (int32 Repeat = 1; Repeat <= Repeats; ++Repeat)
				{
					const FTrial Baseline{Delay, Repeat, Pattern, EOwnerTestSpeedMode::Baseline};
					const FTrial Improved{Delay, Repeat, Pattern, EOwnerTestSpeedMode::Improved};
					if (bCompare && Repeat % 2 == 0) { Trials.Add(Improved); }
					Trials.Add(Baseline);
					if (bCompare && Repeat % 2 != 0) { Trials.Add(Improved); }
				}
			}
		}
		StartRun(World, true, false, true, Profile, MoveTemp(Trials),
			TEXT("RubberBanding.OwnerTest ") + FString::Join(Args, TEXT(" ")), bCompare, LossPercent);
	}

	FAutoConsoleCommandWithWorldAndArgs OwnerTestCommand(TEXT("RubberBanding.OwnerTest"),
		TEXT("PIE owner movement experiments. Profiles: Fixed, Jitter, Loss, Stress (500 ms added RTT; 10% loss per direction by default)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunOwnerTest));

	FAutoConsoleCommandWithWorldAndArgs SweepCommand(TEXT("RubberBanding.Sweep"),
		TEXT("Run 0/100/200/300 ms in two-client single-process PIE: Observer or Owner [Probe]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartSweep));

	FAutoConsoleCommandWithWorld CancelSweepCommand(TEXT("RubberBanding.CancelSweep"), TEXT("Cancel the active PIE sweep."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (IsRubberBandingSweepActive(World))
			{
				CancelRun();
			}
		}));
}
#endif

bool IsRubberBandingSweepActive(const UWorld* World)
{
#if !UE_BUILD_SHIPPING && DO_ENABLE_NET_TEST
	return Sweep && World && Sweep->Participants.ContainsByPredicate([World](const FParticipant& Participant)
	{
		return Participant.World.Get() == World;
	});
#else
	return false;
#endif
}
