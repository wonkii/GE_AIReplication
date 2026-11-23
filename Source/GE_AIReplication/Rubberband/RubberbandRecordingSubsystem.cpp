#include "RubberbandRecordingSubsystem.h"
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
	TAutoConsoleVariable<int32> CVarRecord(TEXT("rb.Record"), 0, TEXT("1 = record rubber-banding samples in every game world of this process."));
	TAutoConsoleVariable<FString> CVarRunId(TEXT("rb.RunId"), TEXT("Run"), TEXT("Recording run id (folder name). Use a new id for every run."));
	TAutoConsoleVariable<FString> CVarCondition(TEXT("rb.Condition"), TEXT("Unlabeled"), TEXT("Network condition label, e.g. N0, L200, J100-300, L200-P3."));
	TAutoConsoleVariable<FString> CVarClientId(TEXT("rb.ClientId"), TEXT(""), TEXT("Optional client label (A, B). Default: -SharpClientId=, PIE instance or process id."));
	TAutoConsoleVariable<int32> CVarSmoothMode(TEXT("rb.Smooth.Mode"), -1, TEXT("Simulated proxy smoothing on clients: -1 keep, 0 Disabled, 1 Linear, 2 Exponential."));
	TAutoConsoleVariable<float> CVarSmoothTime(TEXT("rb.Smooth.LocationTime"), -1.f, TEXT("Simulated proxy NetworkSimulatedSmoothLocationTime on clients; <= 0 keeps the asset value."));

	const FString SampleHeader = TEXT("RunId,Condition,Role,ClientId,PlayerId,NetRole,HostTime,ServerTime,Frame,FrameMs");

	FString SafeName(const FString& Value) { return FPaths::MakeValidFileName(Value.IsEmpty() ? TEXT("Unlabeled") : Value); }
	FString Vec(const FVector& V) { return FString::Printf(TEXT("%.4f,%.4f,%.4f"), V.X, V.Y, V.Z); }

	FString NetRoleName(ENetRole Role)
	{
		switch (Role)
		{
		case ROLE_Authority: return TEXT("Authority");
		case ROLE_AutonomousProxy: return TEXT("Autonomous");
		case ROLE_SimulatedProxy: return TEXT("Simulated");
		default: return TEXT("None");
		}
	}

	int32 PlayerIdOf(const APawn& Pawn) { return Pawn.GetPlayerState() ? Pawn.GetPlayerState()->GetPlayerId() : -1; }

	/** Smoothing time actually used: CMC caches it in client prediction data when that data is allocated. */
	float EffectiveSmoothTime(const UCharacterMovementComponent& Movement)
	{
		if (Movement.HasPredictionData_Client())
		{
			return const_cast<UCharacterMovementComponent&>(Movement).GetPredictionData_Client_Character()->SmoothNetUpdateTime;
		}
		return Movement.NetworkSimulatedSmoothLocationTime;
	}

	FVector VisualLocation(const ACharacter& Character)
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
	FString Id = CVarClientId.GetValueOnGameThread();
	if (!Id.IsEmpty()) { return SafeName(Id); }
	if (FParse::Value(FCommandLine::Get(), TEXT("SharpClientId="), Id) && !Id.IsEmpty()) { return SafeName(Id); }
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

	const bool bWanted = CVarRecord.GetValueOnGameThread() != 0;
	if (bRecording && (!bWanted || RunId != CVarRunId.GetValueOnGameThread() || Condition != CVarCondition.GetValueOnGameThread()))
	{
		StopRecording(bWanted ? TEXT("LabelChanged") : TEXT("Stopped"));
	}
	const FString Key = CVarRunId.GetValueOnGameThread() + TEXT("/") + CVarCondition.GetValueOnGameThread();
	if (bWanted && !bRecording && Key != BlockedKey && GetWorld()->GetGameState()) { StartRecording(); }
	if (!bRecording) { return; }

	SampleCharacters(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now - FlushTime >= 1.0) { Files.Flush(); FlushTime = Now; }
	if (!Files.IsHealthy()) { StopRecording(TEXT("WriteFailed")); }
}

void URubberbandRecordingSubsystem::ApplySmoothingOverrides() const
{
	if (IsServer()) { return; }
	const int32 Mode = CVarSmoothMode.GetValueOnGameThread();
	const float Time = CVarSmoothTime.GetValueOnGameThread();
	if (Mode < 0 && Time <= 0.f) { return; }
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		UCharacterMovementComponent* Movement = It->GetCharacterMovement();
		if (!Movement || It->GetLocalRole() != ROLE_SimulatedProxy) { continue; }
		if (Mode >= 0 && Mode <= 2) { Movement->NetworkSmoothingMode = static_cast<ENetworkSmoothingMode>(Mode); }
		if (Time > 0.f)
		{
			Movement->NetworkSimulatedSmoothLocationTime = Time;
			// The property alone is not enough after prediction data exists (see AGE_AIReplicationCharacter::BeginPlay).
			if (FNetworkPredictionData_Client_Character* Data = Movement->GetPredictionData_Client_Character()) { Data->SmoothNetUpdateTime = Time; }
		}
	}
}

void URubberbandRecordingSubsystem::StartRecording()
{
	RunId = CVarRunId.GetValueOnGameThread();
	Condition = CVarCondition.GetValueOnGameThread();
	RoleTag = IsServer() ? FString(TEXT("server")) : TEXT("client_") + ClientId();
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Experiments/Rubberband"), SafeName(RunId), SafeName(Condition)));
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
	bRecording = true; FlushTime = FPlatformTime::Seconds(); Corrections.Reset();
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
		const int32 PlayerId = PlayerIdOf(Character);
		const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
		if (PlayerId < 0 || !Movement) { continue; }
		const FString Common = FString::Printf(TEXT("%s,%s,%s,%s,%d,%s,%.6f,%.6f,%llu,%.3f"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), bServer ? TEXT("Server") : TEXT("Client"),
			*Id, PlayerId, *NetRoleName(Character.GetLocalRole()), Host, Server, static_cast<unsigned long long>(GFrameCounter), DeltaTime * 1000.f);
		if (bServer)
		{
			Files.Add(TEXT("server_samples.csv"), SampleHeader + TEXT(",PosX,PosY,PosZ,VelX,VelY,VelZ,MovementMode"),
				Common + TEXT(",") + Vec(Character.GetActorLocation()) + TEXT(",") + Vec(Movement->Velocity)
				+ FString::Printf(TEXT(",%u"), static_cast<uint8>(Movement->MovementMode)));
			continue;
		}
		const FCorrectionState* State = Corrections.Find(PlayerId);
		Files.Add(RoleTag + TEXT("_samples.csv"),
			SampleHeader + TEXT(",CapsuleX,CapsuleY,CapsuleZ,VisualX,VisualY,VisualZ,VelX,VelY,VelZ,SmoothingMode,SmoothLocationTime,CorrectionCount,LastCorrectionDist"),
			Common + TEXT(",") + Vec(Character.GetActorLocation()) + TEXT(",") + Vec(VisualLocation(Character)) + TEXT(",") + Vec(Movement->Velocity)
			+ FString::Printf(TEXT(",%d,%.4f,%d,%.4f"), static_cast<int32>(Movement->NetworkSmoothingMode), EffectiveSmoothTime(*Movement),
				State ? State->Count : 0, State ? State->LastDistance : 0.f));
	}
}

void URubberbandRecordingSubsystem::RecordCorrection(const ACharacter& Character, const FVector& OldLocation, const FVector& NewLocation,
	float Speed, float ServerStateTime)
{
	if (!bRecording || IsServer()) { return; }
	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	const float Distance = FVector::Dist(OldLocation, NewLocation);
	FCorrectionState& State = Corrections.FindOrAdd(PlayerIdOf(Character));
	++State.Count; State.LastDistance = Distance;
	// Engine rule: corrections beyond NetworkNoSmoothUpdateDistance are applied without smoothing (a visible teleport).
	const bool bNoSmooth = Movement && Distance > Movement->NetworkNoSmoothUpdateDistance;
	Files.Add(RoleTag + TEXT("_corrections.csv"),
		TEXT("RunId,Condition,ClientId,PlayerId,HostTime,ServerTime,ServerStateTime,OldX,OldY,OldZ,NewX,NewY,NewZ,Distance,Speed,SmoothingMode,SmoothLocationTime,NoSmoothDistance,NoSmooth,Count"),
		FString::Printf(TEXT("%s,%s,%s,%d,%.6f,%.6f,%.6f,%s,%s,%.4f,%.4f,%d,%.4f,%.2f,%d,%d"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), *ClientId(), PlayerIdOf(Character),
			FPlatformTime::Seconds(), ServerTime(), ServerStateTime, *Vec(OldLocation), *Vec(NewLocation), Distance, Speed,
			Movement ? static_cast<int32>(Movement->NetworkSmoothingMode) : -1, Movement ? EffectiveSmoothTime(*Movement) : -1.f,
			Movement ? Movement->NetworkNoSmoothUpdateDistance : -1.f, bNoSmooth, State.Count));
}

void URubberbandRecordingSubsystem::RecordScriptEvent(const APawn& Pawn, const FString& Event, int32 Loop, int32 StepIndex, FName StepName)
{
	if (!bRecording) { return; }
	Files.Add(RoleTag + TEXT("_script.csv"),
		TEXT("RunId,Condition,PlayerId,HostTime,ServerTime,Event,Loop,StepIndex,StepName,PosX,PosY,PosZ"),
		FString::Printf(TEXT("%s,%s,%d,%.6f,%.6f,%s,%d,%d,%s,%s"),
			*FSharpRecordingFiles::Csv(RunId), *FSharpRecordingFiles::Csv(Condition), PlayerIdOf(Pawn), FPlatformTime::Seconds(), ServerTime(),
			*FSharpRecordingFiles::Csv(Event), Loop, StepIndex, *FSharpRecordingFiles::Csv(StepName.ToString()), *Vec(Pawn.GetActorLocation())));
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
		TEXT("  \"WrittenUtc\": \"%s\",\n  \"Outcome\": \"%s\",\n  \"SmoothModeOverride\": %d,\n  \"SmoothLocationTimeOverride\": %.4f,\n  \"PacketSimulation\": %s\n}\n"),
		*RunId.ReplaceCharWithEscapedChar(), *Condition.ReplaceCharWithEscapedChar(), *RoleTag, World ? static_cast<int32>(World->GetNetMode()) : -1,
		FPlatformProcess::GetCurrentProcessId(), *FDateTime::UtcNow().ToIso8601(), *Outcome,
		CVarSmoothMode.GetValueOnGameThread(), CVarSmoothTime.GetValueOnGameThread(), *Emulation);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Files.GetDirectory(), RoleTag + TEXT("_meta.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
