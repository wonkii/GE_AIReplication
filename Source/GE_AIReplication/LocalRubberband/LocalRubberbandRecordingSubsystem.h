#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../SharpMovement/Measurement/SharpRecordingFiles.h"
#include "LocalRubberbandRecordingSubsystem.generated.h"
class ACharacter;
class APawn;
UCLASS()
class GE_AIREPLICATION_API ULocalRubberbandRecordingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    bool Start(const FString& Directory, int32 InPlayerId, bool bInClient);
    void Stop(const FString& Reason);
    bool IsRecording() const { return bRecording; }
    bool IsHealthy() const { return bRecording && Files.IsHealthy(); }
    int32 RecordCorrection(const ACharacter& Character, float Stamp, const FVector& Target, const FVector& TargetVelocity,
        bool bRelative, uint8 Mode, int32 SavedMoves);
    void RecordReplay(const ACharacter& Character, int32 First, int32 Last, const FVector& BeforeFirst,
        const FVector& BeforeReplay, bool bReplayResult);
    void RecordScriptEvent(const APawn& Pawn, const FString& Event, int32 Loop, int32 StepIndex, FName StepName);
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    bool Matches(const APawn& Pawn) const;
    void Sample(float DeltaTime);
    FSharpRecordingFiles Files;
    FString Directory, Prefix;
    int32 PlayerId = -1, EventId = 0;
    bool bRecording = false, bClient = false;
    double LastFlush = 0;
};
