#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"
#include "RubberBandingCaptureSubsystem.generated.h"

class ACharacter;
class FJsonObject;

struct FRubberBandingTrialMetrics
{
	struct FMove
	{
		int32 Epoch;
		float Timestamp;
		double MaxSpeed;
		int32 TransitionId;
	};
	TArray<double> ServerErrorsCm;
	TArray<double> ReconciliationCm;
	TArray<double> ReplayMs;
	TArray<double> FrameGapsMs;
	int32 ServerErrorCount = 0;
	int32 CorrectionCount = 0;
	TArray<FMove> PredictedMoves;
	TArray<FMove> CheckedMoves;
	TOptional<FVector> FirstServerPosition;
	FVector LastServerPosition = FVector::ZeroVector;
	double ServerPathCm = 0;
	uint64 FirstSample = 0;
	uint64 LastSample = 0;
};

UCLASS()
class URubberBandingCaptureSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static URubberBandingCaptureSubsystem* GetActive(const UWorld* World);
	bool Start(const FString& Directory, TSharedPtr<FJsonObject> Trial = {}, int32 MoverId = INDEX_NONE);
	bool Stop(const TCHAR* Outcome);
	const FRubberBandingTrialMetrics& GetTrialMetrics() const { return Metrics; }
	bool IsTrialMover(const ACharacter& Character) const;
	void Record(const ACharacter& Character, const TCHAR* Event, const FVector& Before, const FVector& After,
		TOptional<float> MoveTimestamp = {}, TOptional<float> DeltaSeconds = {}, TOptional<int32> Detail = {},
		TOptional<int32> CorrelationId = {}, TOptional<int32> MoveEpoch = {});

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Deinitialize() override;

private:
	bool WriteManifest(const TCHAR* Outcome) const;
	FSharpRecordingFiles Files;
	TSharedPtr<FJsonObject> InitialNetworkEmulation;
	TSharedPtr<FJsonObject> TrialMetadata;
	int32 MoverPlayerId = INDEX_NONE;
	FRubberBandingTrialMetrics Metrics;
	FString StartedUtc;
	int64 EventId = 0;
};
