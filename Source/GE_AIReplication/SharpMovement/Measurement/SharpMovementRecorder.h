#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpMovementRecordTypes.h"
#include "../Experiment/SharpExperimentTypes.h"
#include "SharpMovementRecorder.generated.h"

class ASharpEnemyCharacter;
class ASharpExperimentManager;
class FJsonObject;

/** Local server/client measurements; drawing and recording never change movement authority. */
UCLASS(ClassGroup = (SharpMovement), meta = (BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpMovementRecorderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpMovementRecorderComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Observation")
	FSharpObservationSettings Observation;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Recording|Runtime")
	bool bRecording = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Recording|Runtime")
	FString OutputDirectory;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Recording|Runtime")
	FString LastError;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Recording|Runtime")
	int64 LastEventId = 0;

	bool StartRecording(ASharpExperimentManager* Manager, FString& OutReason);
	void StopRecording(const FString& Reason);
	int64 BeginImpulse(FName VolumeId, const FVector& WorldImpulse, const FSharpImpulseSettings& Settings, double OverlapTime);
	void CompleteImpulseCall(int64 EventId);
	void SynchronizeClientRecording();
	void RecordDiagnostic(const FString& Kind, uint32 Sequence, double SourceServerTime, double Value1, double Value2, const FString& Detail);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	UPROPERTY(Transient)
	TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpRecordingSettings ActiveSettings;
	FSharpObservationSettings ActiveObservation;
	FSharpEnemyExperimentContext Context;
	FName RecordedActorId;
	TArray<FSharpImpulseRecord> PendingEvents;
	TArray<FString> SampleRows;
	TArray<FString> EventRows;
	TArray<FString> DiagnosticRows;
	TArray<FString> ConnectionRows;
	struct FConnectionSample { uint32 InBytes, OutBytes, InPackets, OutPackets, Period; double RealTime, ServerTime; };
	TMap<TWeakObjectPtr<class UNetConnection>, FConnectionSample> ConnectionSamples;
	struct FPendingDiagnostic { FString Row; double SourceTime; double ReceiveTime; };
	TArray<FPendingDiagnostic> PendingDiagnostics;
	bool bClientRecording = false;
	bool bClientFinished = false;
	bool StartClientRecording();
	bool CreateAuxiliaryFiles();
	void RecordClientSample(float DeltaSeconds);
	void RecordConnections();
	TSharedPtr<FJsonObject> Manifest;
	int64 SampleId = 0;
	int64 RecordOrdinal = 0;
	float SinceFlush = 0.0f;
	bool bOwnsVisualLoggerLease = false;
	bool bStopping = false;
	UFUNCTION()
	void ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	void RecordSample(float DeltaSeconds, const FVector& OldVelocity);
	void FinalizeEvent(const FSharpImpulseRecord& Event);
	void ExpireEvents();
	bool Flush();
	bool SaveManifest();
	void SetFileError(const FString& Reason);
	void AcquireVisualLogger();
	void ReleaseVisualLogger();
};
