#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Experiment/SharpExperimentTypes.h"
#include "SharpRecordingFiles.h"
#include "SharpMovementRecorderComponent.generated.h"

class ASharpEnemyCharacter;
class USharpExperimentSettings;
struct FSharpClassification;
class FJsonObject;

UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpMovementRecorderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpMovementRecorderComponent();
	void HandleSessionChanged();
	void Finish(const FString& Reason);
	void RecordDiagnostic(const FString& Kind, uint32 Sequence, double SourceTime, double Value, double CostMicroseconds, const FString& Reason);
	void RecordDetection(uint32 Sequence, const FSharpClassification& Result, double CostMicroseconds, const FString& Route);
	uint32 BeginImpulse(FName VolumeId, const FVector& WorldImpulse, bool bVelocityChange, bool bIsSharpEvent);
	void CompleteImpulse(uint32 EventId);
	void ObserveForceApplication();
	void ObserveReceipt(double SourceTime);
	bool IsRecording() const { return bRecording; }
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	FSharpEnemyTrialStatus Context;
	FSharpRecordingFiles Files;
	TSharedPtr<FJsonObject> Manifest;
	int32 RecordedActorId = 0;
	bool bVisualLoggerLease = false;
	bool bRecording = false;
	int32 ClosedGeneration = 0, ClosedSegment = 0;
	uint32 NextEventId = 0;
	uint64 ReceiptCount = 0;
	double LastReceiptSourceTime = -1, FlushTime = 0, TailDeadline = -1;
	struct FPendingImpulse
	{
		uint32 Id = 0; FName VolumeId; FVector Impulse, BeforeVelocity;
		double CallTime = 0, CompleteTime = -1, AppliedTime = -1, WallTime = 0, FirstSampleTime = -1;
		uint64 Frame = 0; bool bVelocityChange = false, bIsSharpEvent = true;
	};
	TArray<FPendingImpulse> PendingImpulses;
	FString CommonRow(double ServerTime) const;
	void StartRecording();
	void WriteManifest(const FString& Outcome);
	void WriteImpulse(const FPendingImpulse& Event, double ObservedTime, const FString& Outcome);
	UFUNCTION() void ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
};
