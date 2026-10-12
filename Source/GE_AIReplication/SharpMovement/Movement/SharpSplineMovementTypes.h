#pragma once

#include "CoreMinimal.h"
#include "SharpSplineMovementTypes.generated.h"

UENUM(BlueprintType)
enum class ESharpSplineFollowState : uint8
{
	Idle,
	Ready,
	Following,
	Suspended,
	Completed,
	Failed
};

USTRUCT(BlueprintType)
struct FSharpSplineFollowSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0.1", Units = "cm/s"))
	float TargetSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tracking", meta = (ClampMin = "0.1", Units = "cm"))
	float LookAheadDistance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tracking")
	bool bReverseDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arrival", meta = (ClampMin = "0.1", Units = "cm"))
	float AcceptanceRadius = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arrival", meta = (ClampMin = "0.0", Units = "cm/s"))
	float StopSpeedTolerance = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arrival", meta = (ClampMin = "0.1", Units = "cm"))
	float EndSlowdownDistance = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arrival", meta = (ClampMin = "0.1", Units = "cm/s"))
	float MinimumApproachSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety", meta = (ClampMin = "0.0", Units = "s"))
	float BlockedTimeoutSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety", meta = (ClampMin = "0.1", Units = "cm"))
	float MinimumProgressDistance = 1.0f;

	// Limits projection jumps, not the character's physical displacement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety", meta = (ClampMin = "0.0", Units = "cm"))
	float ProjectionSlackDistance = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safety", meta = (ClampMin = "1.0"))
	float ProjectionSpeedMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airborne")
	bool bAllowInputWhileFalling = false;

	bool IsValid(FString& OutReason) const;
};
