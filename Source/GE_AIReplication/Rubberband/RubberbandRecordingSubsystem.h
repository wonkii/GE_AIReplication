#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../SharpMovement/Measurement/SharpRecordingFiles.h"
#include "RubberbandRecordingSubsystem.generated.h"

class ACharacter;
class APawn;

/**
 * Per-world recorder for the rubber-banding mitigation (B) experiments. One instance per server/client world,
 * so it works the same in separate processes and in single-process PIE.
 *
 * Console variables (set the same values in every process, e.g. -ExecCmds="rb.RunId R1, rb.Condition L200, rb.Record 1"):
 *   rb.Record 0|1, rb.RunId, rb.Condition, rb.ClientId (optional),
 *   rb.Smooth.Mode -1|0|1|2|3|4 (keep|Disabled|Linear|Exponential|Adaptive|Buffer), rb.Smooth.LocationTime (<=0 keeps; T0 for Adaptive),
 *   rb.Ada.UseTime 0|1, rb.Ada.UseRate 0|1, rb.Ada.Gain/DeadZone/MaxTime/CatchUpSpeed/MaxOffset (<0 keeps the component value),
 *   rb.Buf.Adaptive 0|1, rb.Buf.Delay/Gain/Base (mode 4 interpolation buffer).
 * Separate-process automation (Scripts/RunRubberbandProcess.ps1):
 *   rb.AutoScript <loops>  moving client: start the input script 3 s after recording starts with 2+ players
 *   rb.AutoQuit 1          moving client exits when its script is done; server/viewer exit when a peer leaves
 * Output: Saved/Experiments/Rubberband/<RunId>/<Condition>/
 *   server_samples.csv, client_<Id>_samples.csv, client_<Id>_corrections.csv, <role>_script.csv, <role>_meta.json
 * Time columns: HostTime = FPlatformTime::Seconds() (shared by processes on one PC), ServerTime = (estimated) server world time.
 */
UCLASS()
class GE_AIREPLICATION_API URubberbandRecordingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	void RecordCorrection(const ACharacter& Character, const FVector& OldLocation, const FVector& NewLocation, float Speed, float ServerStateTime,
		float Jitter, float ArrivalGap);
	void RecordScriptEvent(const APawn& Pawn, const FString& Event, int32 Loop, int32 StepIndex, FName StepName);
	bool IsRecording() const { return bRecording; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FCorrectionState { int32 Count = 0; float LastDistance = 0.f; };
	FSharpRecordingFiles Files;
	TMap<int32, FCorrectionState> Corrections; // by PlayerId
	FString RunId, Condition, RoleTag;
	/** RunId/Condition that failed to open or write; not retried until the labels change. */
	FString BlockedKey;
	double FlushTime = 0;
	bool bRecording = false;
	// Separate-process automation (rb.AutoScript, rb.AutoQuit)
	double RecordingStartTime = 0;
	double PeerLeftTime = -1;
	int32 PeakPlayers = 0;
	bool bAutoStarted = false;
	bool bQuitRequested = false;
	void TickAutomation();

	void StartRecording();
	void StopRecording(const FString& Reason);
	void ApplySmoothingOverrides() const;
	void SampleCharacters(float DeltaTime);
	void WriteMeta(const FString& Outcome) const;
	bool IsServer() const;
	FString ClientId() const;
	double ServerTime() const;
};
