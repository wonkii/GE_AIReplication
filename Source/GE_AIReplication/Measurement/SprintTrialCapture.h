#pragma once

#include "CoreMinimal.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"
#include "Subsystems/WorldSubsystem.h"
#include "SprintTrialCapture.generated.h"

class ASprintTrialCharacter;

enum class ESprintProfileEvent : uint8
{
	Prediction, PredictionPhysics, Replay, ServerMove, StateRpc, Correction
};

struct FSprintPackedMove
{
	float Timestamp = 0.f;
	int32 Flags = 0;
	bool bSprint = false;
	int64 FirstBit = 0, EndBit = 0;
};

struct FSprintPackedSample
{
	uint32 Serial = 0;
	int32 Packet = INDEX_NONE;
	uint64 Frame = 0;
	double Time = 0.;
	int32 Epoch = 0;
	int64 Bits = 0;
	int32 Ack = INDEX_NONE, AckFlags = INDEX_NONE;
	FSprintPackedMove NewMove, Pending, Old;
};

UCLASS()
class USprintTrialCapture : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static USprintTrialCapture* Find(const UWorld* World);
	bool Open(const FString& Root, const FString& Role, const FGuid& InTrialId, bool bProfile = false);
	bool IsRecording() const { return bRecording; }
	void Start() { bRecording = true; }
	bool IsProfiling(const ASprintTrialCharacter& Character) const;
	void RecordTiming(ESprintProfileEvent Event, double Start, double End, float MoveTime = 0.f, int32 Epoch = 0, int32 Moves = 0);
	void RecordPacked(FSprintPackedSample Sample, int32 Packet);
	bool Signal(const TCHAR* Name, const FString& Text);
	bool Finish();
	void Fail(const FString& Reason);
	void Record(const ASprintTrialCharacter& Character, const TCHAR* Event, float MoveTime = 0.f,
		int32 Epoch = 0, const FVector& Other = FVector::ZeroVector, double Value = 0.0, int32 Phase = -1,
		double Delta = 0., int32 Intent = -1, int32 ModeBefore = -1, int32 PeerMode = -1, int32 Forced = -1,
		double AppliedCap = -1., int32 Packet = INDEX_NONE);

	virtual void Deinitialize() override;

private:
	struct FEvent
	{
		const TCHAR* Name;
		double WorldTime;
		float MoveTime;
		int32 Epoch, Phase;
		FVector Position, Other;
		double Speed, MaxSpeed, MeshOffset, Value;
		double PlatformSeconds, Delta, AppliedCap;
		int32 Intent, Mode, ModeBefore, PeerMode, Forced, Packet;
	};
	struct FTiming
	{
		ESprintProfileEvent Event;
		uint64 Frame;
		double Start, End;
		float MoveTime;
		int32 Epoch, Moves;
	};
	void WriteEvent(const FEvent& Event);
	void FlushProfile();

	FSharpRecordingFiles Files;
	FString Directory;
	FGuid TrialId;
	TArray<FEvent> Events;
	TArray<FTiming> Timings;
	TArray<FSprintPackedSample> PackedSamples;
	int32 Dropped = 0;
	bool bProfiling = false;
	bool bRecording = false;
};
