#pragma once

#include "CoreMinimal.h"
#include "SharpRandomWalkTypes.generated.h"

// Movement-source selection is independent of the seven network replication policies.
UENUM(BlueprintType)
enum class ESharpEnemyMovementSource : uint8
{
	Spline UMETA(DisplayName="Spline"),
	RandomWalk UMETA(DisplayName="Random Walk")
};

UENUM(BlueprintType)
enum class ESharpRandomWalkState : uint8
{
	Idle, Ready, Moving, Stopped, Suspended, Failed
};

UENUM(BlueprintType)
enum class ESharpRandomWalkPhase : uint8
{
	Straight, Braking, Turning
};

// Each segment selects one of four controllable movement patterns.
// A separate selection setting can lock a single pattern for controlled trials.
UENUM(BlueprintType)
enum class ESharpRandomWalkBehavior : uint8
{
    StopTurnWalk UMETA(DisplayName="Walk / Stop and Turn"),
    WalkTurn UMETA(DisplayName="Walk and Turn"),
    RunStopTurn UMETA(DisplayName="Run / Stop and Turn"),
    RunTurn UMETA(DisplayName="Run and Turn")
};

UENUM(BlueprintType)
enum class ESharpRandomWalkBehaviorSelection : uint8
{
    Mixed UMETA(DisplayName="Mixed (all four)"),
    StopTurnWalk UMETA(DisplayName="Walk / Stop and Turn only"),
    WalkTurn UMETA(DisplayName="Walk and Turn only"),
    RunStopTurn UMETA(DisplayName="Run / Stop and Turn only"),
    RunTurn UMETA(DisplayName="Run and Turn only")
};

USTRUCT(BlueprintType)
struct FSharpRandomWalkSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Straight", meta=(ClampMin="1.0", Units="cm"))
	float MinimumStraightDistance = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Straight", meta=(ClampMin="1.0", Units="cm"))
	float MaximumStraightDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Straight", meta=(ClampMin="1.0", Units="cm/s"))
	float TargetSpeed = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior")
    ESharpRandomWalkBehaviorSelection BehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior", meta=(ClampMin="1.0", Units="cm/s"))
    float RunSpeed = 600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior", meta=(ClampMin="1.0", Units="deg/s"))
    float WalkTurningRateDegreesPerSecond = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior", meta=(ClampMin="1.0", Units="deg/s"))
    float RunTurningRateDegreesPerSecond = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turn", meta=(ClampMin="0.1", ClampMax="180.0", Units="deg"))
	float MinimumTurnAngle = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turn", meta=(ClampMin="0.1", ClampMax="180.0", Units="deg"))
	float MaximumTurnAngle = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turn", meta=(ClampMin="1.0", Units="deg/s"))
	float TurnRateDegreesPerSecond = 540.0f;

    // Used for stop-turn patterns; moving-turn patterns steer while CMC keeps moving.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turn", meta=(ClampMin="0.0", Units="cm/s"))
	float TurnStartSpeedTolerance = 15.0f;

    // Without manager auto identity, BaseSeed + ActorId controls the repeatable stream.
    // With manager auto identity, ManagerBaseSeed + one-based registry index is used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Determinism")
	int32 BaseSeed = 12345;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Safety", meta=(ClampMin="0.0", Units="s"))
	float BlockedTimeoutSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Safety", meta=(ClampMin="0.1", Units="cm"))
	float MinimumProgressDistance = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Airborne")
	bool bAllowInputWhileFalling = false;

	bool IsValid(FString& OutReason) const
	{
		if (!FMath::IsFinite(MinimumStraightDistance) || !FMath::IsFinite(MaximumStraightDistance) ||
			MinimumStraightDistance < 1.0f || MaximumStraightDistance < MinimumStraightDistance ||
			!FMath::IsFinite(TargetSpeed) || TargetSpeed <= 0.0f ||
            !FMath::IsFinite(RunSpeed) || RunSpeed < TargetSpeed ||
            !FMath::IsFinite(WalkTurningRateDegreesPerSecond) || WalkTurningRateDegreesPerSecond <= 0.0f ||
            !FMath::IsFinite(RunTurningRateDegreesPerSecond) || RunTurningRateDegreesPerSecond <= 0.0f ||
			!FMath::IsFinite(MinimumTurnAngle) || !FMath::IsFinite(MaximumTurnAngle) ||
			MinimumTurnAngle <= 0.0f || MaximumTurnAngle < MinimumTurnAngle || MaximumTurnAngle > 180.0f ||
			!FMath::IsFinite(TurnRateDegreesPerSecond) || TurnRateDegreesPerSecond <= 0.0f ||
			!FMath::IsFinite(TurnStartSpeedTolerance) || TurnStartSpeedTolerance < 0.0f ||
			!FMath::IsFinite(BlockedTimeoutSeconds) || BlockedTimeoutSeconds < 0.0f ||
			!FMath::IsFinite(MinimumProgressDistance) || MinimumProgressDistance <= 0.0f ||
            static_cast<uint8>(BehaviorSelection) > static_cast<uint8>(ESharpRandomWalkBehaviorSelection::RunTurn))
		{
			OutReason = TEXT("RandomWalk: invalid distance, speed, turn angle/rate, braking, or stall settings");
			return false;
		}
		OutReason.Empty();
		return true;
	}
};
