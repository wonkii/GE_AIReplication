#include "SharpConnectionRecorderComponent.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"

USharpConnectionRecorderComponent::USharpConnectionRecorderComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void USharpConnectionRecorderComponent::BeginPlay()
{
	Super::BeginPlay(); Manager = Cast<ASharpExperimentManager>(GetOwner());
}

void USharpConnectionRecorderComponent::Sample()
{
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver) { return; }
	TArray<UNetConnection*> Connections;
	if (Driver->ServerConnection) { Connections.Add(Driver->ServerConnection); }
	for (UNetConnection* Connection : Driver->ClientConnections) { if (Connection) { Connections.Add(Connection); } }
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double ServerTime = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double WallTime = static_cast<double>(FDateTime::UtcNow().GetTicks()) / ETimespan::TicksPerSecond;
	for (UNetConnection* Connection : Connections)
	{
		Files.Add(TEXT("connections.csv"), TEXT("RunId,TrialId,Role,ConnectionId,ServerTime,WallTime,InBytes,OutBytes,InPackets,OutPackets,Scope"),
			FString::Printf(TEXT("%s,%d,%s,%s,%.9f,%.6f,%llu,%llu,%llu,%llu,EngineConnectionCountersNotActorOrNIC"),
				*FSharpRecordingFiles::Csv(RunId), TrialId, *FSharpRecordingFiles::ProcessRole(*GetWorld()), *FSharpRecordingFiles::Csv(Connection->GetName()),
				ServerTime, WallTime, static_cast<unsigned long long>(static_cast<uint32>(Connection->InTotalBytes)), static_cast<unsigned long long>(static_cast<uint32>(Connection->OutTotalBytes)),
				static_cast<unsigned long long>(static_cast<uint32>(Connection->InTotalPackets)), static_cast<unsigned long long>(static_cast<uint32>(Connection->OutTotalPackets))));
	}
	Files.Flush(); LastSample = GetWorld()->GetTimeSeconds();
}

void USharpConnectionRecorderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!Manager || Manager->Identity.RunId.IsEmpty()) { return; }
	const bool bActive = Manager->Summary.Running + Manager->Summary.Suspended > 0;
	if (bRecording && (RunId != Manager->Identity.RunId || TrialId != Manager->Identity.TrialId)) { Sample(); Files.Close(); bRecording = false; }
	if (bActive && !bRecording)
	{
		RunId = Manager->Identity.RunId; TrialId = Manager->Identity.TrialId;
		if (!Manager->RecordingConfiguration.bEnabled) { return; }
		SampleSeconds = Manager->RecordingConfiguration.ConnectionSampleSeconds; TailSeconds = Manager->RecordingConfiguration.PostTrialObservationSeconds;
		bRecording = Files.Open(FPaths::Combine(FSharpRecordingFiles::RunDirectory(RunId, TrialId), TEXT("_connections"),
			FSharpRecordingFiles::ProcessRole(*GetWorld()), FString::Printf(TEXT("Part_%d"), ++Part)), 256);
		LastSample = -1; FinishAt = -1;
	}
	if (!bRecording) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (bActive) { FinishAt = -1; }
	else if (FinishAt < 0) { FinishAt = Now + TailSeconds; }
	if (Now - LastSample >= SampleSeconds) { Sample(); }
	if (FinishAt >= 0 && Now >= FinishAt) { Sample(); Files.Close(); bRecording = false; }
}

void USharpConnectionRecorderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bRecording) { Sample(); Files.Close(); } Super::EndPlay(Reason);
}
