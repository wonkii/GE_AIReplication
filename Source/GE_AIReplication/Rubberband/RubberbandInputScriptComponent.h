#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RubberbandInputScriptComponent.generated.h"

UENUM(BlueprintType)
enum class ERubberbandStepType : uint8
{
	/** Hold movement input towards a fixed world yaw. Scale 0 means "release input" (sudden stop). */
	Move,
	/** Same as Move, and press jump when the step starts. */
	JumpMove,
	/** Walk back to the script origin; ends early within ArriveRadius. Closes the loop so runs do not drift. */
	ReturnHome
};

USTRUCT(BlueprintType)
struct FRubberbandScriptStep
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ERubberbandStepType Type = ERubberbandStepType::Move;
	/** World yaw in degrees (0 = +X). Ignored by ReturnHome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float YawDegrees = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float InputScale = 1.f;
	/** Seconds. For ReturnHome this is the maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Duration = 1.f;
};

/**
 * Replays a fixed movement-input pattern on the locally controlled character ("Client B"),
 * so every network condition sees the same sharp starts, stops, turns, reversals and jumps.
 * Input goes through the normal AddMovementInput path, i.e. client prediction -> server -> other clients.
 * Console: rb.Script.Start [Loops], rb.Script.Stop (typed in the window of the client that should move).
 * One-shot: rb.Run <Condition> [Loops] [SmoothMode] [SmoothTime] [RunId] sets the labels, starts recording in every world,
 * starts this script after 0.5 s and turns recording off 1 s after the last loop.
 */
UCLASS(ClassGroup=(Rubberband), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API URubberbandInputScriptComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URubberbandInputScriptComponent();

	/** Empty = built-in default pattern (about 20 s per loop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband")
	TArray<FRubberbandScriptStep> Steps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband", meta=(ClampMin="1"))
	int32 Loops = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband", meta=(ClampMin="10"))
	float ArriveRadius = 60.f;

	UFUNCTION(BlueprintCallable, Category="Rubberband") bool StartScript(int32 LoopCount = 0);
	UFUNCTION(BlueprintCallable, Category="Rubberband") void StopScript(FName Reason = NAME_None);
	UFUNCTION(BlueprintPure, Category="Rubberband") bool IsRunning() const { return bRunning; }

	/** When set, rb.Record is turned off shortly after the script completes (used by rb.Run). */
	bool bStopRecordingOnComplete = false;

	static TArray<FRubberbandScriptStep> MakeDefaultSteps();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
	bool bRunning = false;
	int32 Loop = 0, StepIndex = 0, LoopTarget = 0;
	float StepElapsed = 0.f;
	FVector Origin = FVector::ZeroVector;
	TArray<FRubberbandScriptStep> ActiveSteps;
	void EnterStep();
	void LeaveStep();
	void Mark(const FString& Event) const;
};
