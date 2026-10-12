#include "SharpMovementRecorder.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Scenario/SharpImpulseVolume.h"
#include "../Scenario/SharpSplinePath.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "VisualLogger/VisualLogger.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpRecording, Log, All);

namespace
{
	FString Csv(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return TEXT("\"") + Escaped + TEXT("\"");
	}
	FString VectorCsv(const FVector& V) { return FString::Printf(TEXT("%.9f,%.9f,%.9f"), V.X, V.Y, V.Z); }
	FString OptionalTime(double Time) { return Time < 0.0 ? FString() : FString::Printf(TEXT("%.9f"), Time); }
	FString SafeSegment(const FString& Value)
	{
		FString Result = FPaths::MakeValidFileName(Value);
		Result.ReplaceInline(TEXT("."), TEXT("_"));
		return Result.IsEmpty() ? TEXT("Unnamed") : Result;
	}
	template<typename T> TSharedPtr<FJsonObject> StructJson(const T& Value)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(T::StaticStruct(), &Value, Object.ToSharedRef(), 0, 0);
		return Object;
	}
#if ENABLE_VISUAL_LOG
	int32 VisualLoggerLeases = 0;
	bool PreviousRecording = false;
	bool PreviousFileRecording = false;
	bool PreviousServerRecording = false;
#endif
}

USharpMovementRecorderComponent::USharpMovementRecorderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void USharpMovementRecorderComponent::BeginPlay()
{
	Super::BeginPlay();
	Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy)
	{
		AddTickPrerequisiteComponent(Enemy->GetCharacterMovement());
		AddTickPrerequisiteComponent(Enemy->VisualPredictor);
		if (Enemy->HasAuthority()) { Enemy->OnCharacterMovementUpdated.AddDynamic(this, &USharpMovementRecorderComponent::ObserveMovement); }
	}
}

void USharpMovementRecorderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRecording(TEXT("EndPlay"));
	if (Enemy)
	{
		Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementRecorderComponent::ObserveMovement);
		RemoveTickPrerequisiteComponent(Enemy->GetCharacterMovement());
	}
	Super::EndPlay(EndPlayReason);
}

bool USharpMovementRecorderComponent::StartRecording(ASharpExperimentManager* Manager, FString& OutReason)
{
	if (!Enemy || !Enemy->HasAuthority() || !IsValid(Manager) || !IsValid(Manager->Settings) || bRecording)
	{
		OutReason = TEXT("Recording needs an idle recorder on the server and valid Manager settings");
		return false;
	}
	ActiveSettings = Manager->Settings->Recording;
	ActiveObservation = Observation;
	if (!ActiveSettings.IsValid(OutReason) || !ActiveObservation.IsValid(OutReason)) { return false; }
	Context = Enemy->ExperimentContext;
	RecordedActorId = Enemy->ActorId;
	LastError.Empty();
	SampleRows.Reset(); EventRows.Reset(); DiagnosticRows.Reset(); ConnectionRows.Reset(); ConnectionSamples.Reset(); PendingEvents.Reset(); SampleId = 0; RecordOrdinal = 0; SinceFlush = 0.0f;
	bClientRecording = false;
	OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), ActiveSettings.OutputSubdirectory,
		SafeSegment(Context.Identity.RunId), FString::Printf(TEXT("Trial_%d"), Context.Identity.TrialId),
		FString::Printf(TEXT("%u_%s_%s"), FPlatformProcess::GetCurrentProcessId(), *SafeSegment(GetWorld()->GetName()),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		SetFileError(TEXT("Cannot create recorder output directory")); OutReason = LastError; return false;
	}
	const FString SampleHeader = TEXT("RunId,TrialId,ActorId,PathId,PathRevision,Frame,RecordOrdinal,SampleId,ServerWorldTime,DeltaSeconds,PositionX,PositionY,PositionZ,OldVelocityX,OldVelocityY,OldVelocityZ,VelocityX,VelocityY,VelocityZ,MovementMode,CustomMode,FollowState,MeshX,MeshY,MeshZ\n");
	const FString EventHeader = TEXT("RunId,TrialId,ActorId,EventId,VolumeId,CallFrame,CallOrdinal,PositionX,PositionY,PositionZ,PreVelocityX,PreVelocityY,PreVelocityZ,ImpulseX,ImpulseY,ImpulseZ,VelocityChange,FollowPolicy,PreMovementMode,PreCustomMode,OverlapTime,CallTime,CallCompleteTime,FirstPostCallSampleTime,FirstSampleId,ActualVelocityChangeTime,ObservedSampleId,ObservedVelocityX,ObservedVelocityY,ObservedVelocityZ,ObservedMovementMode,ObservedCustomMode,DeltaVelocityMagnitude,Status\n");
	if (!FFileHelper::SaveStringToFile(SampleHeader, *FPaths::Combine(OutputDirectory, TEXT("server_samples.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!FFileHelper::SaveStringToFile(EventHeader, *FPaths::Combine(OutputDirectory, TEXT("events.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		SetFileError(TEXT("Cannot create CSV files")); OutReason = LastError; return false;
	}
	Manifest = MakeShared<FJsonObject>();
	Manifest->SetNumberField(TEXT("SchemaVersion"), 2);
	Manifest->SetStringField(TEXT("Role"), TEXT("Server"));
	TSharedRef<FJsonObject> SettingsJson = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(Manager->Settings->GetClass(), Manager->Settings, SettingsJson, 0, 0);
	Manifest->SetObjectField(TEXT("ExperimentSettings"), SettingsJson);
	Manifest->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
	Manifest->SetStringField(TEXT("BuildConfiguration"), LexToString(FApp::GetBuildConfiguration()));
	Manifest->SetStringField(TEXT("World"), GetWorld()->GetName());
	Manifest->SetStringField(TEXT("Map"), GetWorld()->GetMapName());
	Manifest->SetStringField(TEXT("MapIdentity"), UWorld::RemovePIEPrefix(GetWorld()->GetMapName()));
	Manifest->SetNumberField(TEXT("ProcessId"), FPlatformProcess::GetCurrentProcessId());
	Manifest->SetNumberField(TEXT("NetMode"), GetWorld()->GetNetMode());
	Manifest->SetObjectField(TEXT("ExperimentContext"), StructJson(Context));
	Manifest->SetStringField(TEXT("ActorId"), RecordedActorId.ToString());
	Manifest->SetStringField(TEXT("BaselineProfile"), UEnum::GetValueAsString(Manager->BaselineProfile));
	Manifest->SetObjectField(TEXT("RecordingSettings"), StructJson(ActiveSettings));
	Manifest->SetObjectField(TEXT("ObservationSettings"), StructJson(ActiveObservation));
	Manifest->SetObjectField(TEXT("FollowSettings"), StructJson(Enemy->SplineFollower->GetPreparedSettings()));
	Manifest->SetStringField(TEXT("PathTransform"), Manager->Path->GetActorTransform().ToString());
	Manifest->SetNumberField(TEXT("PathLength"), Manager->Path->GetLength());
	Manifest->SetStringField(TEXT("PathGeometry"), Manager->Path->GetGeometrySignature());
	if (const IConsoleVariable* FPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { Manifest->SetStringField(TEXT("FrameRateLimit"), FPS->GetString()); }
	int32 EnemyCount = 0;
	for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It) { ++EnemyCount; }
	Manifest->SetNumberField(TEXT("EnemyCount"), EnemyCount);
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = GetWorld()->GetNetDriver()) { Manifest->SetObjectField(TEXT("NetworkEmulation"), StructJson(Driver->PacketSimulationSettings)); }
#endif
	const UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement();
	TSharedPtr<FJsonObject> Movement = MakeShared<FJsonObject>();
	Movement->SetNumberField(TEXT("MaxWalkSpeed"), CMC->MaxWalkSpeed);
	Movement->SetNumberField(TEXT("MaxAcceleration"), CMC->MaxAcceleration);
	Movement->SetNumberField(TEXT("GroundFriction"), CMC->GroundFriction);
	Movement->SetNumberField(TEXT("BrakingDecelerationWalking"), CMC->BrakingDecelerationWalking);
	Movement->SetNumberField(TEXT("BrakingFriction"), CMC->BrakingFriction);
	Movement->SetNumberField(TEXT("BrakingFrictionFactor"), CMC->BrakingFrictionFactor);
	Movement->SetBoolField(TEXT("UseSeparateBrakingFriction"), CMC->bUseSeparateBrakingFriction);
	Movement->SetNumberField(TEXT("AirControl"), CMC->AirControl);
	Movement->SetNumberField(TEXT("GravityScale"), CMC->GravityScale);
	Movement->SetNumberField(TEXT("Mass"), CMC->Mass);
	Movement->SetStringField(TEXT("RotationRate"), CMC->RotationRate.ToString());
	Movement->SetBoolField(TEXT("OrientRotationToMovement"), CMC->bOrientRotationToMovement);
	Movement->SetNumberField(TEXT("NetworkSmoothingMode"), static_cast<int32>(CMC->NetworkSmoothingMode));
	Movement->SetNumberField(TEXT("NetUpdateFrequency"), Enemy->GetNetUpdateFrequency());
	Movement->SetNumberField(TEXT("MinNetUpdateFrequency"), Enemy->GetMinNetUpdateFrequency());
	Movement->SetBoolField(TEXT("AlwaysRelevant"), Enemy->bAlwaysRelevant);
	Movement->SetNumberField(TEXT("NetDormancy"), static_cast<int32>(Enemy->NetDormancy));
	Manifest->SetObjectField(TEXT("MovementSettings"), Movement);
	TArray<TSharedPtr<FJsonValue>> Volumes;
	for (const ASharpImpulseVolume* Volume : Manager->ImpulseVolumes)
	{
		if (!IsValid(Volume)) { continue; }
		TSharedPtr<FJsonObject> Data = StructJson(Volume->GetPreparedSettings());
		Data->SetStringField(TEXT("VolumeId"), Volume->VolumeId.ToString());
		Data->SetStringField(TEXT("Transform"), Volume->GetActorTransform().ToString());
		Data->SetStringField(TEXT("BoxExtent"), Volume->TriggerBox->GetUnscaledBoxExtent().ToString());
		Volumes.Add(MakeShared<FJsonValueObject>(Data));
	}
	Manifest->SetArrayField(TEXT("Volumes"), Volumes);
	TSharedPtr<FJsonObject> ConsoleSettings = MakeShared<FJsonObject>();
	const TCHAR* Names[] = {TEXT("t.MaxFPS"), TEXT("net.UseAdaptiveNetUpdateFrequency"), TEXT("net.PktLag"), TEXT("net.PktLagMin"),
		TEXT("net.PktLagMax"), TEXT("net.PktLoss"), TEXT("net.PktIncomingLagMin"), TEXT("net.PktIncomingLagMax"), TEXT("net.PktIncomingLoss")};
	for (const TCHAR* Name : Names)
	{
		if (const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) { ConsoleSettings->SetStringField(Name, Var->GetString()); }
	}
	Manifest->SetObjectField(TEXT("AvailableConsoleSettings"), ConsoleSettings);
	Manifest->SetStringField(TEXT("Status"), TEXT("Recording"));
	Manifest->SetNumberField(TEXT("StartTime"), GetWorld()->GetTimeSeconds());
	if (!CreateAuxiliaryFiles()) { OutReason = LastError; return false; }
	if (!SaveManifest()) { OutReason = LastError; return false; }
	bRecording = true;
	RecordConnections();
	AcquireVisualLogger();
	RecordSample(0.0f, CMC->Velocity);
	UE_LOG(LogSharpRecording, Display, TEXT("RunId=%s TrialId=%d ActorId=%s Output=%s"), *Context.Identity.RunId, Context.Identity.TrialId, *RecordedActorId.ToString(), *OutputDirectory);
	OutReason = LastError;
	return bRecording && LastError.IsEmpty();
}

bool USharpMovementRecorderComponent::CreateAuxiliaryFiles()
{
	const FString DiagnosticHeader = TEXT("RunId,TrialId,ActorId,Kind,Sequence,SourceServerTime,ReceiveServerTime,LocalWorldTime,Frame,Value1,Value2,Detail\n");
	const FString ConnectionHeader = TEXT("RunId,TrialId,ActorId,Connection,PeriodStartServerTime,ServerWorldTime,PeriodSeconds,ObservedPeriod,InBytesPerSecond,OutBytesPerSecond,InPacketsPerSecond,OutPacketsPerSecond,Scope\n");
	if (!FFileHelper::SaveStringToFile(DiagnosticHeader, *FPaths::Combine(OutputDirectory, TEXT("diagnostics.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
		!FFileHelper::SaveStringToFile(ConnectionHeader, *FPaths::Combine(OutputDirectory, TEXT("connections.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		SetFileError(TEXT("Cannot create auxiliary CSV files")); return false;
	}
	return true;
}

bool USharpMovementRecorderComponent::StartClientRecording()
{
	ActiveSettings = Enemy->ClientState.Recording; ActiveObservation = Observation;
	FString Reason;
	if (!ActiveSettings.IsValid(Reason)) { SetFileError(Reason); bClientFinished = true; return false; }
	Context = Enemy->ClientState.Context; RecordedActorId = Enemy->ActorId;
	SampleRows.Reset(); EventRows.Reset(); DiagnosticRows.Reset(); ConnectionRows.Reset(); ConnectionSamples.Reset();
	SampleId = 0; RecordOrdinal = 0; SinceFlush = 0.0f; LastError.Empty();
	OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), ActiveSettings.OutputSubdirectory,
		SafeSegment(Context.Identity.RunId), FString::Printf(TEXT("Trial_%d"), Context.Identity.TrialId),
		FString::Printf(TEXT("Client_%u_%s_%s"), FPlatformProcess::GetCurrentProcessId(), *SafeSegment(GetWorld()->GetName()), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	const FString Header = TEXT("RunId,TrialId,ActorId,Frame,ServerWorldTime,LocalWorldTime,DeltaSeconds,PositionX,PositionY,PositionZ,MeshX,MeshY,MeshZ,VelocityX,VelocityY,VelocityZ,MovementMode,CustomMode,MovementGeneration,ReplicatedServerTime,Predicting,VisualOffset\n");
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true) ||
		!FFileHelper::SaveStringToFile(Header, *FPaths::Combine(OutputDirectory, TEXT("client_samples.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) || !CreateAuxiliaryFiles())
	{
		SetFileError(TEXT("Cannot create client output files")); bClientFinished = true; return false;
	}
	Manifest = MakeShared<FJsonObject>();
	Manifest->SetNumberField(TEXT("SchemaVersion"), 2); Manifest->SetStringField(TEXT("Role"), TEXT("Client"));
	Manifest->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
	Manifest->SetObjectField(TEXT("ExperimentContext"), StructJson(Context));
	Manifest->SetObjectField(TEXT("RecordingSettings"), StructJson(ActiveSettings));
	Manifest->SetObjectField(TEXT("PredictionSettings"), StructJson(Enemy->ClientState.Prediction));
	Manifest->SetStringField(TEXT("ActorId"), RecordedActorId.ToString()); Manifest->SetStringField(TEXT("World"), GetWorld()->GetName());
	Manifest->SetNumberField(TEXT("ProcessId"), FPlatformProcess::GetCurrentProcessId());
	Manifest->SetStringField(TEXT("Clock"), TEXT("GameState estimated server world time; not exact one-way delay"));
	Manifest->SetStringField(TEXT("Status"), TEXT("Recording"));
	Manifest->SetNumberField(TEXT("ServerStartTime"), Enemy->ClientState.ServerStartTime);
	if (const IConsoleVariable* FPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { Manifest->SetStringField(TEXT("FrameRateLimit"), FPS->GetString()); }
#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = GetWorld()->GetNetDriver()) { Manifest->SetObjectField(TEXT("NetworkEmulation"), StructJson(Driver->PacketSimulationSettings)); }
#endif
	if (!SaveManifest()) { bClientFinished = true; return false; }
	bClientRecording = true; bRecording = true; bClientFinished = false;
	const FString Prefix = FString::Printf(TEXT("%s,%d,%s,"), *Csv(Context.Identity.RunId), Context.Identity.TrialId, *Csv(RecordedActorId.ToString()));
	for (const FPendingDiagnostic& Item : PendingDiagnostics)
	{
		if (Item.ReceiveTime >= Enemy->ClientState.ServerStartTime && (Item.SourceTime <= 0.0 || Item.SourceTime >= Enemy->ClientState.ServerStartTime)) { DiagnosticRows.Add(Prefix + Item.Row); }
	}
	PendingDiagnostics.Reset(); RecordConnections(); return true;
}

void USharpMovementRecorderComponent::SynchronizeClientRecording()
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy || Enemy->HasAuthority() || !GetWorld()->GetGameState()) { return; }
	const FSharpExperimentClientState& State = Enemy->ClientState;
	const bool bSameTrial = Context.Identity.RunId == State.Context.Identity.RunId && Context.Identity.TrialId == State.Context.Identity.TrialId;
	if (!bSameTrial)
	{
		if (bRecording) { StopRecording(TEXT("TrialChanged")); }
		bClientFinished = false;
	}
	const double Now = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	const bool bActive = State.State == ESharpTrialState::Running || State.State == ESharpTrialState::Suspended;
	const bool bTail = State.ServerEndTime > 0.0 && Now < State.ServerEndTime + State.Recording.PostTrialObservationSeconds;
	if (!bRecording && !bClientFinished && !State.Context.Identity.RunId.IsEmpty() && !Enemy->ActorId.IsNone() && State.bHasStarted && (bActive || bTail)) { StartClientRecording(); }
	if (bRecording && !bActive && !bTail) { StopRecording(TEXT("ClientTrialEnded")); bClientFinished = true; }
}

void USharpMovementRecorderComponent::RecordDiagnostic(const FString& Kind, uint32 Sequence, double SourceServerTime, double Value1, double Value2, const FString& Detail)
{
	if (!Enemy) { Enemy = Cast<ASharpEnemyCharacter>(GetOwner()); }
	if (!Enemy || !GetWorld()) { return; }
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double ReceiveTime = Enemy->HasAuthority() ? GetWorld()->GetTimeSeconds() : (GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds());
	const FString Row = FString::Printf(TEXT("%s,%u,%.9f,%.9f,%.9f,%llu,%.9f,%.9f,%s\n"), *Csv(Kind), Sequence, SourceServerTime, ReceiveTime,
		GetWorld()->GetTimeSeconds(), static_cast<unsigned long long>(GFrameCounter), Value1, Value2, *Csv(Detail));
	if (!bRecording)
	{
		if (!Enemy->HasAuthority())
		{
			const int32 Limit = FMath::Max(1, Enemy->ClientState.Recording.MaximumPendingClientDiagnostics);
			if (PendingDiagnostics.Num() >= Limit) { PendingDiagnostics.RemoveAt(0, PendingDiagnostics.Num() - Limit + 1); }
			PendingDiagnostics.Add({Row, SourceServerTime, ReceiveTime});
		}
		return;
	}
	DiagnosticRows.Add(FString::Printf(TEXT("%s,%d,%s,"), *Csv(Context.Identity.RunId), Context.Identity.TrialId, *Csv(RecordedActorId.ToString())) + Row);
	if (DiagnosticRows.Num() + SampleRows.Num() + EventRows.Num() + ConnectionRows.Num() >= ActiveSettings.MaximumBufferedRows) { Flush(); }
}

void USharpMovementRecorderComponent::RecordClientSample(float DeltaSeconds)
{
	++SampleId;
	const UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement();
	const double Now = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	const FVector Mesh = Enemy->GetMesh()->GetComponentLocation();
	const FVector Baseline = Enemy->GetCapsuleComponent()->GetComponentTransform().TransformPosition(Enemy->GetBaseTranslationOffset());
	SampleRows.Add(FString::Printf(TEXT("%s,%d,%s,%llu,%.9f,%.9f,%.9f,%s,%s,%s,%d,%d,%llu,%.9f,%d,%.9f\n"),
		*Csv(Context.Identity.RunId), Context.Identity.TrialId, *Csv(RecordedActorId.ToString()), static_cast<unsigned long long>(GFrameCounter), Now, GetWorld()->GetTimeSeconds(), DeltaSeconds,
		*VectorCsv(Enemy->GetActorLocation()), *VectorCsv(Mesh), *VectorCsv(CMC->Velocity), CMC->MovementMode, CMC->CustomMovementMode,
		static_cast<unsigned long long>(Enemy->MovementGeneration), Enemy->GetReplicatedServerLastTransformUpdateTimeStamp(), Enemy->VisualPredictor->bPredicting ? 1 : 0, (Mesh - Baseline).Size()));
}

void USharpMovementRecorderComponent::RecordConnections()
{
	UNetDriver* Driver = GetWorld()->GetNetDriver(); if (!Driver) { return; }
	TArray<UNetConnection*> Connections;
	if (Enemy->HasAuthority()) { for (UNetConnection* Connection : Driver->ClientConnections) { Connections.Add(Connection); } }
	else if (Driver->ServerConnection) { Connections.Add(Driver->ServerConnection); }
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double Now = Enemy->HasAuthority() ? GetWorld()->GetTimeSeconds() : (GS ? GS->GetServerWorldTimeSeconds() : 0.0);
	for (UNetConnection* Connection : Connections)
	{
		if (!Connection) { continue; }
		const TWeakObjectPtr<UNetConnection> Key(Connection);
		const double RealNow = FPlatformTime::Seconds();
		if (!ConnectionSamples.Contains(Key))
		{
			ConnectionSamples.Add(Key, {static_cast<uint32>(Connection->InTotalBytes), static_cast<uint32>(Connection->OutTotalBytes),
				static_cast<uint32>(Connection->InTotalPackets), static_cast<uint32>(Connection->OutTotalPackets), 0, RealNow, Now}); continue;
		}
		FConnectionSample& Last = ConnectionSamples[Key];
		const double Duration = RealNow - Last.RealTime;
		if (Duration <= 0.0 || (!bStopping && Duration < ActiveSettings.ConnectionSampleSeconds)) { continue; }
		const double InBps = static_cast<uint32>(Connection->InTotalBytes - Last.InBytes) / Duration;
		const double OutBps = static_cast<uint32>(Connection->OutTotalBytes - Last.OutBytes) / Duration;
		const double InPps = static_cast<uint32>(Connection->InTotalPackets - Last.InPackets) / Duration;
		const double OutPps = static_cast<uint32>(Connection->OutTotalPackets - Last.OutPackets) / Duration;
		ConnectionRows.Add(FString::Printf(TEXT("%s,%d,%s,%s,%.9f,%.9f,%.9f,%u,%.9f,%.9f,%.9f,%.9f,%s\n"), *Csv(Context.Identity.RunId), Context.Identity.TrialId,
			*Csv(RecordedActorId.ToString()), *Csv(Connection->GetName()), Last.ServerTime, Now, Duration, ++Last.Period,
			InBps, OutBps, InPps, OutPps, *Csv(TEXT("EngineConnectionCountersNotActorOrNIC"))));
		Last.InBytes = Connection->InTotalBytes; Last.OutBytes = Connection->OutTotalBytes;
		Last.InPackets = Connection->InTotalPackets; Last.OutPackets = Connection->OutTotalPackets; Last.RealTime = RealNow; Last.ServerTime = Now;
	}
}

void USharpMovementRecorderComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (bRecording && Enemy && Enemy->HasAuthority()) { RecordSample(DeltaSeconds, OldVelocity); }
}

void USharpMovementRecorderComponent::RecordSample(float DeltaSeconds, const FVector& OldVelocity)
{
	const UCharacterMovementComponent* CMC = Enemy->GetCharacterMovement();
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Position = Enemy->GetActorLocation();
	++SampleId; ++RecordOrdinal;
	SampleRows.Add(FString::Printf(TEXT("%s,%d,%s,%s,%d,%llu,%lld,%lld,%.9f,%.9f,%s,%s,%s,%d,%d,%s,%s\n"),
		*Csv(Context.Identity.RunId), Context.Identity.TrialId, *Csv(RecordedActorId.ToString()), *Csv(Context.PathId.ToString()), Context.PathRevision,
		static_cast<unsigned long long>(GFrameCounter), static_cast<long long>(RecordOrdinal), static_cast<long long>(SampleId), Now, DeltaSeconds,
		*VectorCsv(Position), *VectorCsv(OldVelocity), *VectorCsv(CMC->Velocity), static_cast<int32>(CMC->MovementMode), CMC->CustomMovementMode,
		*Csv(UEnum::GetValueAsString(Enemy->SplineFollower->State)), *VectorCsv(Enemy->GetMesh()->GetComponentLocation())));
	for (int32 Index = PendingEvents.Num() - 1; Index >= 0; --Index)
	{
		FSharpImpulseRecord& Event = PendingEvents[Index];
		if (Event.FirstSampleId == 0) { Event.FirstSampleId = SampleId; Event.FirstSampleTime = Now; }
		Event.ObservedVelocity = CMC->Velocity;
		Event.ObservedMovementMode = CMC->MovementMode;
		Event.ObservedCustomMode = CMC->CustomMovementMode;
		if (Now - Event.CallTime <= ActiveSettings.EventObservationSeconds &&
			(CMC->Velocity - Event.PreVelocity).Size() > ActiveSettings.ObservableVelocityChange)
		{
			Event.VelocityChangeTime = Now; Event.ObservedSampleId = SampleId; Event.Status = TEXT("Observed");
			FinalizeEvent(Event);
			PendingEvents.RemoveAt(Index);
		}
	}
	ExpireEvents();
	if (ActiveSettings.bWriteVisualLogger)
	{
		UE_VLOG_LOCATION(Enemy, LogSharpRecording, Log, Position, ActiveObservation.EventRadius, ActiveObservation.ServerColor,
			TEXT("Run=%s Trial=%d Actor=%s World=%s Sample=%lld Velocity=%s Mode=%d"), *Context.Identity.RunId, Context.Identity.TrialId,
			*RecordedActorId.ToString(), *GetWorld()->GetName(), static_cast<long long>(SampleId), *CMC->Velocity.ToString(), static_cast<int32>(CMC->MovementMode));
		UE_VLOG_ARROW(Enemy, LogSharpRecording, Log, Position, Position + CMC->Velocity * ActiveObservation.ArrowTimeScale,
			ActiveObservation.ServerColor, TEXT("Velocity"));
	}
	if (SampleRows.Num() + EventRows.Num() >= ActiveSettings.MaximumBufferedRows) { Flush(); }
}

int64 USharpMovementRecorderComponent::BeginImpulse(FName VolumeId, const FVector& WorldImpulse,
	const FSharpImpulseSettings& Settings, double OverlapTime)
{
	if (!bRecording || !Enemy || !Enemy->HasAuthority()) { return 0; }
	if (PendingEvents.Num() >= ActiveSettings.MaximumPendingEvents)
	{
		SetFileError(TEXT("MaximumPendingEvents exceeded; trial cannot record another event")); return 0;
	}
	FSharpImpulseRecord Event;
	Event.EventId = ++LastEventId; Event.VolumeId = VolumeId;
	Event.Position = Enemy->GetActorLocation(); Event.PreVelocity = Enemy->GetCharacterMovement()->Velocity;
	Event.PreMovementMode = Enemy->GetCharacterMovement()->MovementMode; Event.PreCustomMode = Enemy->GetCharacterMovement()->CustomMovementMode;
	Event.ObservedVelocity = Event.PreVelocity; Event.ObservedMovementMode = Event.PreMovementMode; Event.ObservedCustomMode = Event.PreCustomMode;
	Event.WorldImpulse = WorldImpulse; Event.bVelocityChange = Settings.bVelocityChange; Event.FollowPolicy = Settings.FollowPolicy;
	Event.OverlapTime = OverlapTime; Event.CallTime = GetWorld()->GetTimeSeconds(); Event.CallFrame = GFrameCounter; Event.CallOrdinal = ++RecordOrdinal;
	PendingEvents.Add(Event);
	return Event.EventId;
}

void USharpMovementRecorderComponent::CompleteImpulseCall(int64 EventId)
{
	FSharpImpulseRecord* Event = PendingEvents.FindByPredicate([EventId](const FSharpImpulseRecord& Item) { return Item.EventId == EventId; });
	if (!Event) { return; }
	Event->CallCompleteTime = GetWorld()->GetTimeSeconds();
	if (ActiveSettings.bWriteVisualLogger)
	{
		UE_VLOG(Enemy, LogSharpRecording, Log, TEXT("EventId=%lld Volume=%s Actor=%s Impulse=%s PreVelocity=%s CallTime=%.9f"),
			static_cast<long long>(EventId), *Event->VolumeId.ToString(), *RecordedActorId.ToString(), *Event->WorldImpulse.ToString(), *Event->PreVelocity.ToString(), Event->CallTime);
	}
	if (ActiveObservation.bDrawImpulseEvents)
	{
		DrawDebugSphere(GetWorld(), Event->Position, ActiveObservation.EventRadius, ActiveObservation.EventSphereSegments, ActiveObservation.EventColor, false,
			ActiveObservation.EventLifetimeSeconds, 0, ActiveObservation.LineThickness);
		DrawDebugDirectionalArrow(GetWorld(), Event->Position, Event->Position + Event->WorldImpulse * ActiveObservation.ArrowTimeScale,
			ActiveObservation.ArrowHeadSize, ActiveObservation.EventColor, false, ActiveObservation.EventLifetimeSeconds, 0, ActiveObservation.LineThickness);
	}
}

void USharpMovementRecorderComponent::FinalizeEvent(const FSharpImpulseRecord& Event)
{
	EventRows.Add(FString::Printf(TEXT("%s,%d,%s,%lld,%s,%llu,%lld,%s,%s,%s,%d,%s,%d,%d,%.9f,%.9f,%.9f,%s,%lld,%s,%lld,%s,%d,%d,%.9f,%s\n"),
		*Csv(Context.Identity.RunId), Context.Identity.TrialId, *Csv(RecordedActorId.ToString()), static_cast<long long>(Event.EventId), *Csv(Event.VolumeId.ToString()),
		static_cast<unsigned long long>(Event.CallFrame), static_cast<long long>(Event.CallOrdinal), *VectorCsv(Event.Position), *VectorCsv(Event.PreVelocity),
		*VectorCsv(Event.WorldImpulse), Event.bVelocityChange ? 1 : 0, *Csv(UEnum::GetValueAsString(Event.FollowPolicy)), Event.PreMovementMode, Event.PreCustomMode,
		Event.OverlapTime, Event.CallTime, Event.CallCompleteTime, *OptionalTime(Event.FirstSampleTime), static_cast<long long>(Event.FirstSampleId),
		*OptionalTime(Event.VelocityChangeTime), static_cast<long long>(Event.ObservedSampleId), *VectorCsv(Event.ObservedVelocity), Event.ObservedMovementMode,
		Event.ObservedCustomMode, (Event.ObservedVelocity - Event.PreVelocity).Size(), *Csv(Event.Status)));
	if (ActiveSettings.bWriteVisualLogger)
	{
		UE_VLOG(Enemy, LogSharpRecording, Log, TEXT("EventId=%lld Status=%s FirstSample=%lld ObservedSample=%lld"),
			static_cast<long long>(Event.EventId), *Event.Status, static_cast<long long>(Event.FirstSampleId), static_cast<long long>(Event.ObservedSampleId));
	}
}

void USharpMovementRecorderComponent::ExpireEvents()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = PendingEvents.Num() - 1; Index >= 0; --Index)
	{
		if (Now - PendingEvents[Index].CallTime > ActiveSettings.EventObservationSeconds)
		{
			PendingEvents[Index].Status = TEXT("NoObservableChange");
			FinalizeEvent(PendingEvents[Index]); PendingEvents.RemoveAt(Index);
		}
	}
}

void USharpMovementRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Enemy) { return; }
	if (!Enemy->HasAuthority()) { SynchronizeClientRecording(); if (bRecording) { RecordClientSample(DeltaTime); } }
	const FSharpObservationSettings& Drawing = bRecording ? ActiveObservation : Observation;
	const bool bServer = Enemy->HasAuthority();
	if ((bServer && Drawing.bDrawServerCapsule) || (!bServer && Drawing.bDrawClientCapsule))
	{
		const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
		const FColor Color = bServer ? Drawing.ServerColor : Drawing.ClientColor;
		DrawDebugCapsule(GetWorld(), Capsule->GetComponentLocation(), Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(),
			Capsule->GetComponentQuat(), Color, false, Drawing.ShapeLifetimeSeconds, 0, Drawing.LineThickness);
		if (Drawing.bDrawVelocity)
		{
			DrawDebugDirectionalArrow(GetWorld(), Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + Enemy->GetCharacterMovement()->Velocity * Drawing.ArrowTimeScale,
				Drawing.ArrowHeadSize, Color, false, Drawing.ShapeLifetimeSeconds, 0, Drawing.LineThickness);
		}
	}
	if (!bRecording) { return; }
	if (!bClientRecording) { ExpireEvents(); }
	RecordConnections();
	SinceFlush += DeltaTime;
	if (SinceFlush >= ActiveSettings.FlushIntervalSeconds || SampleRows.Num() + EventRows.Num() + DiagnosticRows.Num() + ConnectionRows.Num() >= ActiveSettings.MaximumBufferedRows) { Flush(); }
}

bool USharpMovementRecorderComponent::Flush()
{
	// Remove each successfully written buffer separately so a partial failure cannot duplicate rows.
	auto WriteRows = [this](TArray<FString>& Rows, const TCHAR* Name)
	{
		if (Rows.IsEmpty()) { return true; }
		const FString Text = FString::Join(Rows, TEXT(""));
		if (!FFileHelper::SaveStringToFile(Text, *FPaths::Combine(OutputDirectory, Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append)) { return false; }
		Rows.Reset(); return true;
	};
	if (!WriteRows(SampleRows, bClientRecording ? TEXT("client_samples.csv") : TEXT("server_samples.csv")) || !WriteRows(EventRows, TEXT("events.csv")) ||
		!WriteRows(DiagnosticRows, TEXT("diagnostics.csv")) || !WriteRows(ConnectionRows, TEXT("connections.csv")))
	{
		SetFileError(TEXT("CSV flush failed"));
		if (!bStopping) { StopRecording(TEXT("FileError")); }
		return false;
	}
	SinceFlush = 0.0f;
	return true;
}

bool USharpMovementRecorderComponent::SaveManifest()
{
	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!Manifest.IsValid() || !FJsonSerializer::Serialize(Manifest.ToSharedRef(), Writer) ||
		!FFileHelper::SaveStringToFile(Text, *FPaths::Combine(OutputDirectory, TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		SetFileError(TEXT("Manifest save failed")); return false;
	}
	return true;
}

void USharpMovementRecorderComponent::SetFileError(const FString& Reason)
{
	LastError = Reason;
	UE_LOG(LogSharpRecording, Error, TEXT("%s Output=%s"), *Reason, *OutputDirectory);
}

void USharpMovementRecorderComponent::StopRecording(const FString& Reason)
{
	if (!bRecording || bStopping) { return; }
	if (bClientRecording) { bClientFinished = true; }
	bStopping = true;
	RecordConnections();
	ExpireEvents();
	for (FSharpImpulseRecord& Event : PendingEvents) { Event.Status = TEXT("Cancelled"); FinalizeEvent(Event); }
	PendingEvents.Reset();
	Flush();
	Manifest->SetStringField(TEXT("Status"), LastError.IsEmpty() ? Reason : TEXT("Failed"));
	Manifest->SetStringField(TEXT("Error"), LastError);
	Manifest->SetNumberField(TEXT("EndTime"), GetWorld()->GetTimeSeconds());
	Manifest->SetNumberField(TEXT("SampleCount"), SampleId);
	SaveManifest();
	ReleaseVisualLogger();
	bRecording = false; bStopping = false;
}

void USharpMovementRecorderComponent::AcquireVisualLogger()
{
#if ENABLE_VISUAL_LOG
	if (!ActiveSettings.bWriteVisualLogger || !ActiveSettings.bAutoRecordVisualLoggerToFile) { return; }
	FVisualLogger& Logger = FVisualLogger::Get();
	if (VisualLoggerLeases++ == 0)
	{
		PreviousRecording = FVisualLogger::IsRecording(); PreviousFileRecording = Logger.IsRecordingToFile();
		PreviousServerRecording = Logger.IsRecordingOnServer();
		Logger.SetIsRecordingOnServer(true); Logger.SetIsRecording(true); Logger.SetIsRecordingToFile(true);
	}
	bOwnsVisualLoggerLease = true;
#endif
}

void USharpMovementRecorderComponent::ReleaseVisualLogger()
{
#if ENABLE_VISUAL_LOG
	if (!bOwnsVisualLoggerLease) { return; }
	bOwnsVisualLoggerLease = false;
	if (--VisualLoggerLeases == 0)
	{
		FVisualLogger& Logger = FVisualLogger::Get();
		// Include events emitted by StopRecording before the engine closes its file device.
		Logger.Flush();
		Logger.SetIsRecordingToFile(PreviousFileRecording); Logger.SetIsRecordingOnServer(PreviousServerRecording); Logger.SetIsRecording(PreviousRecording);
	}
#endif
}
