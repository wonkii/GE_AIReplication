#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LocalRubberbandInputScriptComponent.generated.h"

UENUM(BlueprintType)
enum class ELocalRubberbandStepType : uint8
{
	/** Hold movement input towards a fixed world yaw. Scale 0 means "release input" (sudden stop). */
	Move,
	/** Same as Move, and press jump when the step starts. */
	JumpMove,
	/** Walk back to the script origin; ends early within ArriveRadius. Closes the loop so runs do not drift. */
	ReturnHome
};

USTRUCT(BlueprintType)
struct FLocalRubberbandScriptStep
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ELocalRubberbandStepType Type = ELocalRubberbandStepType::Move;
	/** World yaw in degrees (0 = +X). Ignored by ReturnHome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float YawDegrees = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float InputScale = 1.f;
	/** Seconds. For ReturnHome this is the maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Duration = 1.f;
};

/** Independent copy of the original movement script; same default steps. */
UCLASS(ClassGroup=(LocalRubberband), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API ULocalRubberbandInputScriptComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	ULocalRubberbandInputScriptComponent();

	/** Empty = built-in default pattern (about 20 s per loop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LocalRubberband")
	TArray<FLocalRubberbandScriptStep> Steps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LocalRubberband", meta=(ClampMin="1"))
	int32 Loops = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LocalRubberband", meta=(ClampMin="10"))
	float ArriveRadius = 60.f;

	UFUNCTION(BlueprintCallable, Category="LocalRubberband") bool StartScript(int32 LoopCount = 0);
	UFUNCTION(BlueprintCallable, Category="LocalRubberband") void StopScript(FName Reason = NAME_None);
	UFUNCTION(BlueprintPure, Category="LocalRubberband") bool IsRunning() const { return bRunning; }

	static TArray<FLocalRubberbandScriptStep> MakeDefaultSteps();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
	bool bRunning = false;
	int32 Loop = 0, StepIndex = 0, LoopTarget = 0;
	float StepElapsed = 0.f;
	FVector Origin = FVector::ZeroVector;
	TArray<FLocalRubberbandScriptStep> ActiveSteps;
	void EnterStep();
	void LeaveStep();
	void Mark(const FString& Event) const;
};
