#include "RubberbandRecordingSubsystem.h"
#include "RubberbandMovementComponent.h"
#include "RubberbandInputScriptComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRubberbandRecorder, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarRbRecord(TEXT("rb.Record"), 0, TEXT("1 = record rubber-banding samples in every game world of this process."));
	TAutoConsoleVariable<FString> CVarRbRunId(TEXT("rb.RunId"), TEXT("Run"), TEXT("Recording run id (folder name). Use a new id for every run."));
	TAutoConsoleVariable<FString> CVarRbCondition(TEXT("rb.Condition"), TEXT("Unlabeled"), TEXT("Network condition label, e.g. N0, L200, J100-300, L200-P3."));
	TAutoConsoleVariable<FString> CVarRbClientId(TEXT("rb.ClientId"), TEXT(""), TEXT("Optional client label (A, B). Default: -SharpClientId=, PIE instance or process id."));
	TAutoConsoleVariable<int32> CVarRbSmoothMode(TEXT("rb.Smooth.Mode"), -1, TEXT("Simulated proxy smoothing on clients: -1 keep, 0 Disabled, 1 Linear, 2 Exponential, 3 Adaptive (Exponential + B), 4 Interpolation buffer (M4)."));
	TAutoConsoleVariable<int32> CVarRbAdaUseTime(TEXT("rb.Ada.UseTime"), 1, TEXT("Adaptive: 1 = jitter-adaptive smoothing time (M2)."));
	TAutoConsoleVariable<int32> CVarRbAdaUseRate(TEXT("rb.Ada.UseRate"), 1, TEXT("Adaptive: 1 = catch-up speed limit (M3)."));
	TAutoConsoleVariable<float> CVarRbAdaGain(TEXT("rb.Ada.Gain"), -1.f, TEXT("Adaptive: seconds of smoothing per second of jitter (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbAdaDeadZone(TEXT("rb.Ada.DeadZone"), -1.f, TEXT("Adaptive: jitter treated as noise, s (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbAdaMaxTime(TEXT("rb.Ada.MaxTime"), -1.f, TEXT("Adaptive: maximum smoothing time, s (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbAdaCatchUp(TEXT("rb.Ada.CatchUpSpeed"), -1.f, TEXT("Adaptive: maximum catch-up speed, cm/s (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbAdaMaxOffset(TEXT("rb.Ada.MaxOffset"), -1.f, TEXT("Adaptive: offset above which the rate limit is lifted, cm (<0 keeps)."));
	TAutoConsoleVariable<int32> CVarRbBufAdaptive(TEXT("rb.Buf.Adaptive"), 0, TEXT("Buffer (mode 4): 1 = delay follows jitter, 0 = fixed rb.Buf.Delay."));
	TAutoConsoleVariable<float> CVarRbBufDelay(TEXT("rb.Buf.Delay"), -1.f, TEXT("Buffer (mode 4): fixed delay, s (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbBufGain(TEXT("rb.Buf.Gain"), -1.f, TEXT("Buffer (mode 4): adaptive delay per second of jitter (<0 keeps)."));
	TAutoConsoleVariable<float> CVarRbBufBase(TEXT("rb.Buf.Base"), -1.f, TEXT("Buffer (mode 4): adaptive base delay, s (<0 keeps)."));
	TAutoConsoleVariable<int32> CVarRbAutoScript(TEXT("rb.AutoScript"), 0, TEXT("Moving client: >0 starts the input script with this many loops once recording runs with 2+ players."));
	TAutoConsoleVariable<int32> CVarRbAutoQuit(TEXT("rb.AutoQuit"), 0, TEXT("1 = exit when the auto script is done (moving client) or when a peer leaves (server, viewer)."));
	bool GRbWasNetworked = false; // this process has been in a networked world (a standalone world after it = disconnected)
	TAutoConsoleVariable<float> CVarRbSmoothTime(TEXT("rb.Smooth.LocationTime"), -1.f, TEXT("Simulated proxy NetworkSimulatedSmoothLocationTime on clients; <= 0 keeps the asset value."));

	const FString RbSampleHeader = TEXT("RunId,Condition,Role,ClientId,PlayerId,NetRole,HostTime,ServerTime,Frame,FrameMs");

	FString RbSafeName(const FString& Value) { return FPaths::MakeValidFileName(Value.IsEmpty() ? TEXT("Unlabeled") : Value); }
	FString RbVec(const FVector& V) { return FString::Printf(TEXT("%.4f,%.4f,%.4f"), V.X, V.Y, V.Z); }

	FString RbNetRoleName(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Authority");
		case ROLE_AutonomousProxy: return TEXT("Autonomous");
		case ROLE_SimulatedProxy: return TEXT("Simulated");
		default: return TEXT("None");
		}
	}

	int32 RbPlayerIdOf(const APawn& Pawn) { return Pawn.GetPlayerState() ? Pawn.GetPlayerState()->GetPlayerId() : -1; }

	/** Smoothing time actually used: CMC caches it in client prediction data when that data is allocated. */
	float RbEffectiveSmoothTime(const UCharacterMovementComponent& Movement)
	{
		// M4 has no smoothing time; report its buffer delay in the same column.
		if (const URubberbandMovementComponent* Rubber = Cast<URubberbandMovementComponent>(&Movement))
		{
			if (Rubber->bInterpBuffer) { return Rubber->ComputeBufferDelay(); }
		}
		if (Movement.HasPredictionData_Client())
		{
			return const_cast<UCharacterMovementComponent&>(Movement).GetPredictionData_Client_Character()->SmoothNetUpdateTime;
		}
		return Movement.NetworkSimulatedSmoothLocationTime;
	}

	/** Label of the smoothing variant in effect, shared with rb.Batch run names (Dis, Lin, Exp, Exp-0.15, Ada, Ada-T, Ada-R). */
	FString RbVariant(const UCharacterMovementComponent& Movement)
	{
		const URubberbandMovementComponent* Rubber = Cast<URubberbandMovementComponent>(&Movement);
		if (Rubber && Rubber->bInterpBuffer)
		{
			return Rubber->bBufferAdaptive ? FString(TEXT("Buf-A")) : FString::Printf(TEXT("Buf-%.2f"), Rubber->BufferDelay);
		}
		if (Rubber && Rubber->bAdaptiveSmoothing)
		{
			const FString Name = Rubber->bAdaptiveTime && Rubber->bRateLimit ? TEXT("Ada") : Rubber->bAdaptiveTime ? TEXT("Ada-T") : Rubber->bRateLimit ? TEXT("Ada-R") : TEXT("Ada-0");
			return Rubber->bAdaptiveTime ? Name + URubberbandMovementComponent::TuningLabel(Rubber->JitterGain, Rubber->MaxSmoothTime) : Name;
		}
		FString Name;
		switch (Movement.NetworkSmoothingMode)
		{
		case ENetworkSmoothingMode::Disabled: return TEXT("Dis");
		case ENetworkSmoothingMode::Linear: Name = TEXT("Lin"); break;
		case ENetworkSmoothingMode::Exponential: Name = TEXT("Exp"); break;
		default: return TEXT("Replay");
		}
		const float Time = Movement.NetworkSimulatedSmoothLocationTime;
		return FMath::IsNearlyEqual(Time, 0.06f, 0.001f) ? Name : FString::Printf(TEXT("%s-%.2f"), *Name, Time);
	}

	FVector RbVisualLocation(const ACharacter& Character)
	{
		const USkeletalMeshComponent* Mesh = Character.GetMesh();
		if (!Mesh) { return Character.GetActorLocation(); }
		const FVector BaseOffset = Character.GetCapsuleComponent()->GetComponentTransform().TransformVector(Character.GetBaseTranslationOffset());
		return Mesh->GetComponentLocation() - BaseOffset;
	}
}

bool URubberbandRecordingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URubberbandRecordingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URubberbandRecordingSubsystem, STATGROUP_Tickables);
}

bool URubberbandRecordingSubsystem::IsServer() const
{
	return GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

FString URubberbandRecordingSubsystem::ClientId() const
{
	FString Id = CVarRbClientId.GetValueOnGameThread();
	if (!Id.IsEmpty()) { return RbSafeName(Id); }
	if (FParse::Value(FCommandLine::Get(), TEXT("SharpClientId="), Id) && !Id.IsEmpty()) { return RbSafeName(Id); }
	if (const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr)
	{
		if (Context->WorldType == EWorldType::PIE) { return FString::Printf(TEXT("PIE%d"), Context->PIEInstance); }
	}
	return FString::Printf(TEXT("P%u"), FPlatformProcess::GetCurrentProcessId());
}

double URubberbandRecordingSubsystem::ServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return IsServer() || !GameState ? World->GetTimeSeconds() : GameState->GetServerWorldTimeSeconds();
}

void URubberbandRecordingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!GetWorld()) { return; }
	ApplySmoothingOverrides();
	TickAutomation();
	if (bQuitRequested) { return; }

	const bool bWanted = CVarRbRecord.GetValueOnGameThread() != 0;
	if (bRecording && (!bWanted || RunId != CVarRbRunId.GetValueOnGameThread() || Condition != CVarRbCondition.GetValueOnGameThread()))
	{
		StopRecording(bWanted ? TEXT("LabelChanged") : TEXT("Stopped"));
	}
	const FString Key = CVarRbRunId.GetValueOnGameThread() + TEXT("/") + CVarRbCondition.GetValueOnGameThread();
	if (bWanted && !bRecording && Key != BlockedKey && GetWorld()->GetGameState()) { StartRecording(); }
	if (!bRecording) { return; }

	SampleCharacters(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now - FlushTime >= 1.0) { Files.Flush(); FlushTime = Now; }
	if (!Files.IsHealthy()) { StopRecording(TEXT("WriteFailed")); }
}

void URubberbandRecordingSubsystem::TickAutomation()
{
	UWorld* World = GetWorld();
	if (!World || bQuitRequested) { return; }
	const ENetMode NetMode = World->GetNetMode();
	if (NetMode == NM_Client || NetMode == NM_DedicatedServer || NetMode == NM_ListenServer) { GRbWasNetworked = true; }
	const bool bAutoQuit = CVarRbAutoQuit.GetValueOnGameThread() != 0;
	auto Quit = [this](const TCHAR* Reason)
	{
		StopRecording(Reason);
		bQuitRequested = true;
		UE_LOG(LogRubberbandRecorder, Display, TEXT("rb.AutoQuit: %s, exiting"), Reason);
		FPlatformMisc::RequestExit(false);
	};
	// Disconnected (server went away): the client falls back to a standalone world.
	if (bAutoQuit && NetMode == NM_Standalone && GRbWasNetworked) { Quit(TEXT("Disconnected")); return; }

	const AGameStateBase* GameState = World->GetGameState();
	if (!GameState) { return; }
	const int32 Players = GameState->PlayerArray.Num();
	PeakPlayers = FMath::Max(PeakPlayers, Players);

	const int32 AutoLoops = CVarRbAutoScript.GetValueOnGameThread();
	if (AutoLoops > 0 && !bAutoStarted && bRecording && !IsServer() && Players >= 2 && FPlatformTime::Seconds() - RecordingStartTime >= 3.0)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		URubberbandInputScriptComponent* Script = Controller && Controller->GetPawn()
			? Controller->GetPawn()->FindComponentByClass<URubberbandInputScriptComponent>() : nullptr;
		if (Script && !Script->IsRunning())
		{
			Script->bStopRecordingOnComplete = true; // turns rb.Record off 1 s after the last loop
			bAutoStarted = Script->StartScript(AutoLoops);
			UE_LOG(LogRubberbandRecorder, Display, TEXT("rb.AutoScript: %s (%d loops)"), bAutoStarted ? TEXT("started") : TEXT("failed"), AutoLoops);
		}
	}
	if (!bAutoQuit) { return; }
	if (bAutoStarted && !bRecording && CVarRbRecord.GetValueOnGameThread() == 0) { Quit(TEXT("ScriptDone")); return; }
	if (PeakPlayers >= 2 && Players < 2)
	{
		const double Now = FPlatformTime::Seconds();
		if (PeerLeftTime < 0) { PeerLeftTime = Now; }
		// The server waits so the viewer also sees the peer leave (and records the tail) before the connection drops.
		if (Now - PeerLeftTime >= (IsServer() ? 3.0 : 1.0)) { Quit(TEXT("PeerLeft")); }
	}
}

void URubberbandRecordingSubsystem::ApplySmoothingOverrides() const
{
	if (IsServer()) { return; }
	const int32 Mode = CVarRbSmoothMode.GetValueOnGameThread();
	const float Time = CVarRbSmoothTime.GetValueOnGameThread();
	if (Mode < 0 && Time <= 0.f) { return; }
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		UCharacterMovementComponent* Movement = It->GetCharacterMovement();
		if (!Movement || It->GetLocalRole() != ROLE_SimulatedProxy) { continue; }
		URubberbandMovementComponent* Rubber = Cast<URubberbandMovementComponent>(Movement);
		if (Mode == 4)
		{
			Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
			if (Rubber)
			{
				Rubber->bAdaptiveSmoothing = false;
				Rubber->bInterpBuffer = true;
				Rubber->bBufferAdaptive = CVarRbBufAdaptive.GetValueOnGameThread() != 0;
				auto Keep = [](float Value, float& Target) { if (Value >= 0.f) { Target = Value; } };
				Keep(CVarRbBufDelay.GetValueOnGameThread(), Rubber->BufferDelay);
				Keep(CVarRbBufGain.GetValueOnGameThread(), Rubber->BufferJitterGain);
				Keep(CVarRbBufBase.GetValueOnGameThread(), Rubber->BufferBaseDelay);
			}
		}
		else if (Mode == 3)
		{
			Movement->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
			if (Rubber)
			{
				Rubber->bInterpBuffer = false;
				Rubber->bAdaptiveSmoothing = true;
				Rubber->bAdaptiveTime = CVarRbAdaUseTime.GetValueOnGameThread() != 0;
				Rubber->bRateLimit = CVarRbAdaUseRate.GetValueOnGameThread() != 0;
				auto Keep = [](float Value, float& Target) { if (Value >= 0.f) { Target = Value; } };
				Keep(CVarRbAdaGain.GetValueOnGameThread(), Rubber->JitterGain);
				Keep(CVarRbAdaDeadZone.GetValueOnGameThread(), Rubber->JitterDeadZone);
				Keep(CVarRbAdaMaxTime.GetValueOnGameThread(), Rubber->MaxSmoothTime);
				Keep(CVarRbAdaCatchUp.GetValueOnGameThread(), Rubber->MaxCatchUpSpeed);
				Keep(CVarRbAdaMaxOffset.GetValueOnGameThread(), Rubber->RateLimitMaxOffset);
			}
		}
		else if (Mode >= 0 && Mode <= 2)
		{
			Movement->NetworkSmoothingMode = static_cast<ENetworkSmoothingMode>(Mode);
			if (Rubber) { Rubber->bAdaptiveSmoothing = false; Rubber->bInterpBuffer = false; }
		}
		if (Time > 0.f)
		{
			Movement->NetworkSimulatedSmoothLocationTime = Time;
			// The property alone is not enough after prediction data exists (see AGE_AIReplicationCharacter::BeginPlay).
			// Adaptive sets the cached time itself on every correction (T0 = the property).
			if (!(Rubber && Rubber->bAdaptiveSmoothing))
			{
				if (FNetworkPredictionData_Client_Character* Data = Movement->GetPredictionData_Client_Character()) { Data->SmoothNetUpdateTime = Time; }
			}
		}
	}
}

void URubberbandRecordingSubsystem::StartRecording()
{
	RunId = CVarRbRunId.GetValueOnGameThread();
	Condition = CVarRbCondition.GetValueOnGameThread();
	RoleTag = IsServer() ? FString(TEXT("server")) : TEXT("client_") + ClientId();
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Experiments/Rubberband"), RbSafeName(RunId), RbSafeName(Condition)));
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (IFileManager::Get().FileExists(*FPaths::Combine(Directory, RoleTag + TEXT("_meta.json"))))
	{
		BlockedKey = RunId + TEXT("/") + Condition;
		UE_LOG(LogRubberbandRecorder, Error, TEXT("Rubberband recording %s already exists for %s. Use a new rb.RunId."), *Directory, *RoleTag);
		return;
	}
	const FString Readme = FPaths::Combine(Directory, TEXT("README.md"));
	if (!IFileManager::Get().FileExists(*Readme))
	{
		FFileHelper::SaveStringToFile(FString::Printf(
			TEXT("# Rubberband B recording\n\n")
			TEXT("- Generated by: URubberbandRecordingSubsystem (rb.Record 1)\n")
			TEXT("- Purpose: rubber-banding mitigation (B) baseline/adaptive smoothing measurement\n")
			TEXT("- Created (UTC): %s\n- RunId: %s\n- Condition: %s\n\n")
			TEXT("## Files and usage\n\n")
			TEXT("- server_samples.csv: authoritative character positions (server process)\n")
			TEXT("- client_<Id>_samples.csv: what each client shows (capsule and mesh/visual) per frame, with smoothing settings\n")
			TEXT("- client_<Id>_corrections.csv: every simulated-proxy correction received on that client\n")
			TEXT("- <role>_script.csv: scripted input step marks; <role>_meta.json: emulation and smoothing settings actually in effect\n")
			TEXT("- Analyse with Scripts/Analysis/rubberband_metrics.py <this RunId folder>\n\n")
			TEXT("## Result and retention\n\n")
			TEXT("Written when recording starts; does not indicate success. Check each *_meta.json Outcome. Keep raw CSVs with reports.\n"),
			*FDateTime::UtcNow().ToIso8601(), *RunId, *Condition), *Readme, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (!Files.Open(Directory, 4096)) { BlockedKey = RunId + TEXT("/") + Condition; return; }
	bRecording = true; FlushTime = FPlatformTime::Seconds(); RecordingStartTime = FlushTime; Corrections.Reset();
	WriteMeta(TEXT("Recording"));
	UE_LOG(LogRubberbandRecorder, Log, TEXT("Rubberband recording started: %s (%s)"), *Directory, *RoleTag);
}

void URubberbandRecordingSubsystem::StopRecording(const FString& Reason)
{
	if (!bRecording) { return; }
	if (Reason == TEXT("WriteFailed"))
	{
		BlockedKey = RunId + TEXT("/") + Condition;
		UE_LOG(LogRubberbandRecorder, Error, TEXT("Rubberband recording failed for %s. Use a new rb.RunId (files are never mixed)."), *BlockedKey);
	}
	Files.Flush();
	WriteMeta(Reason);
	Files.Close();
	bRecording = false;
	UE_LOG(LogRubberbandRecorder, Log, TEXT("Rubberband recording stopped (%s, %s)"), *RoleTag, *Reason);
}

void URubberbandRecordingSubsystem::Deinitialize()
{
	StopRecording(TEXT("WorldEnd"));
	Super::Deinitialize();
}

void URubberbandRecordingSubsystem::SampleCharacters(float DeltaTime)
{
	const double Host = FPlatformTime::Seconds(), Server = ServerTime();
	const bool bServer = IsServer();
	const FString Id = bServer ? FString(TEXT("Server")) : ClientId();
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		const ACharacter& Character = **It;
		const int32 PlayerId = RbPlayerIdOf(Character);
		const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		if (PlayerId < 0 || !Movement) { continue; }
		const FString Common = FString::Printf(TEXT("%s,%s,%s,%s,%d,%s,%.6f,%.6f,%llu,%.3f"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), bServer ? TEXT("Server") : TEXT("Client"),
			*Id, PlayerId, *RbNetRoleName(Character.GetLocalRole()), Host, Server, static_cast<unsigned long long>(GFrameCounter), DeltaTime * 1000.f);
		if (bServer)
		{
			Files.Add(TEXT("server_samples.csv"), RbSampleHeader + TEXT(",PosX,PosY,PosZ,VelX,VelY,VelZ,MovementMode"),
				Common + TEXT(",") + RbVec(Character.GetActorLocation()) + TEXT(",") + RbVec(Movement->Velocity)
				+ FString::Printf(TEXT(",%u"), static_cast<uint8>(Movement->MovementMode)));
			continue;
		}
		const FCorrectionState* State = Corrections.Find(PlayerId);
		const URubberbandMovementComponent* Rubber = Cast<URubberbandMovementComponent>(Movement);
		Files.Add(RoleTag + TEXT("_samples.csv"),
			RbSampleHeader + TEXT(",CapsuleX,CapsuleY,CapsuleZ,VisualX,VisualY,VisualZ,VelX,VelY,VelZ,SmoothingMode,SmoothLocationTime,CorrectionCount,LastCorrectionDist,Variant,Jitter,RateLimited"),
			Common + TEXT(",") + RbVec(Character.GetActorLocation()) + TEXT(",") + RbVec(RbVisualLocation(Character)) + TEXT(",") + RbVec(Movement->Velocity)
			+ FString::Printf(TEXT(",%d,%.4f,%d,%.4f,%s,%.5f,%d"), static_cast<int32>(Movement->NetworkSmoothingMode), RbEffectiveSmoothTime(*Movement),
				State ? State->Count : 0, State ? State->LastDistance : 0.f, *RbVariant(*Movement),
				Rubber ? Rubber->GetJitter() : -1.f, Rubber && Rubber->WasRateLimited() ? 1 : 0));
	}
}

void URubberbandRecordingSubsystem::RecordCorrection(const ACharacter& Character, const FVector& OldLocation, const FVector& NewLocation,
	float Speed, float ServerStateTime, float Jitter, float ArrivalGap)
{
	if (!bRecording || IsServer()) { return; }
	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	const float Distance = FVector::Dist(OldLocation, NewLocation);
	FCorrectionState& State = Corrections.FindOrAdd(RbPlayerIdOf(Character));
	++State.Count; State.LastDistance = Distance;
	// Engine rule: corrections beyond NetworkNoSmoothUpdateDistance are applied without smoothing (a visible teleport).
	const bool bNoSmooth = Movement && Distance > Movement->NetworkNoSmoothUpdateDistance;
	Files.Add(RoleTag + TEXT("_corrections.csv"),
		TEXT("RunId,Condition,ClientId,PlayerId,HostTime,ServerTime,ServerStateTime,OldX,OldY,OldZ,NewX,NewY,NewZ,Distance,Speed,SmoothingMode,SmoothLocationTime,NoSmoothDistance,NoSmooth,Count,Variant,Jitter,ArrivalGap"),
		FString::Printf(TEXT("%s,%s,%s,%d,%.6f,%.6f,%.6f,%s,%s,%.4f,%.4f,%d,%.4f,%.2f,%d,%d,%s,%.5f,%.5f"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), *ClientId(), RbPlayerIdOf(Character),
			FPlatformTime::Seconds(), ServerTime(), ServerStateTime, *RbVec(OldLocation), *RbVec(NewLocation), Distance, Speed,
			Movement ? static_cast<int32>(Movement->NetworkSmoothingMode) : -1, Movement ? RbEffectiveSmoothTime(*Movement) : -1.f,
			Movement ? Movement->NetworkNoSmoothUpdateDistance : -1.f, bNoSmooth, State.Count,
			Movement ? *RbVariant(*Movement) : TEXT("?"), Jitter, ArrivalGap));
}

void URubberbandRecordingSubsystem::RecordScriptEvent(const APawn& Pawn, const FString& Event, int32 Loop, int32 StepIndex, FName StepName)
{
	if (!bRecording) { return; }
	Files.Add(RoleTag + TEXT("_script.csv"),
		TEXT("RunId,Condition,PlayerId,HostTime,ServerTime,Event,Loop,StepIndex,StepName,PosX,PosY,PosZ"),
		FString::Printf(TEXT("%s,%s,%d,%.6f,%.6f,%s,%d,%d,%s,%s"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), RbPlayerIdOf(Pawn), FPlatformTime::Seconds(), ServerTime(),
			*FSharpRecordingFiles::Csv(Event), Loop, StepIndex, *FSharpRecordingFiles::Csv(StepName.ToString()), *RbVec(Pawn.GetActorLocation())));
}

void URubberbandRecordingSubsystem::WriteMeta(const FString& Outcome) const
{
	const UWorld* World = GetWorld();
	FString Emulation = TEXT("{}");
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr)
	{
		const FPacketSimulationSettings& S = Driver->PacketSimulationSettings;
		Emulation = FString::Printf(TEXT("{\"PktLag\":%d,\"PktLagVariance\":%d,\"PktLagMin\":%d,\"PktLagMax\":%d,\"PktLoss\":%d,")
			TEXT("\"PktIncomingLagMin\":%d,\"PktIncomingLagMax\":%d,\"PktIncomingLoss\":%d}"),
			S.PktLag, S.PktLagVariance, S.PktLagMin, S.PktLagMax, S.PktLoss, S.PktIncomingLagMin, S.PktIncomingLagMax, S.PktIncomingLoss);
	}
#endif
	const FString Json = FString::Printf(
		TEXT("{\n  \"RunId\": \"%s\",\n  \"Condition\": \"%s\",\n  \"Role\": \"%s\",\n  \"NetMode\": %d,\n  \"ProcessId\": %u,\n")
		TEXT("  \"WrittenUtc\": \"%s\",\n  \"Outcome\": \"%s\",\n  \"SmoothModeOverride\": %d,\n  \"SmoothLocationTimeOverride\": %.4f,\n")
		TEXT("  \"Adaptive\": {\"UseTime\":%d,\"UseRate\":%d,\"Gain\":%.3f,\"DeadZone\":%.4f,\"MaxTime\":%.3f,\"CatchUpSpeed\":%.1f,\"MaxOffset\":%.1f},\n")
		TEXT("  \"PacketSimulation\": %s\n}\n"),
		*RunId.ReplaceCharWithEscapedChar(), *Condition.ReplaceCharWithEscapedChar(), *RoleTag, World ? static_cast<int32>(World->GetNetMode()) : -1,
		FPlatformProcess::GetCurrentProcessId(), *FDateTime::UtcNow().ToIso8601(), *Outcome,
		CVarRbSmoothMode.GetValueOnGameThread(), CVarRbSmoothTime.GetValueOnGameThread(),
		CVarRbAdaUseTime.GetValueOnGameThread(), CVarRbAdaUseRate.GetValueOnGameThread(), CVarRbAdaGain.GetValueOnGameThread(),
		CVarRbAdaDeadZone.GetValueOnGameThread(), CVarRbAdaMaxTime.GetValueOnGameThread(), CVarRbAdaCatchUp.GetValueOnGameThread(),
		CVarRbAdaMaxOffset.GetValueOnGameThread(), *Emulation);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Files.GetDirectory(), RoleTag + TEXT("_meta.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
