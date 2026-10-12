#pragma once
#include "CoreMinimal.h"
#include "SharpMoveSnapshot.generated.h"

UENUM(BlueprintType)
enum class ESharpPredictionType : uint8 { None, ConstantVelocity, Airborne };

USTRUCT(BlueprintType)
struct AIREPLICATIONNET_API FSharpMoveSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY() uint32 SequenceId = 0;
	UPROPERTY(BlueprintReadOnly) FString RunId;
	UPROPERTY(BlueprintReadOnly) int32 TrialId = 0;
	UPROPERTY(BlueprintReadOnly) FName ActorId;
	UPROPERTY(BlueprintReadOnly) double ServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) FVector StartPosition = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector PostVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Gravity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) uint8 MovementMode = 0;
	UPROPERTY(BlueprintReadOnly) uint8 CustomMovementMode = 0;
	UPROPERTY(BlueprintReadOnly) ESharpPredictionType PredictionType = ESharpPredictionType::None;

	static bool IsNewer(uint32 Candidate, uint32 Previous) { return static_cast<int32>(Candidate - Previous) > 0; }
	bool IsFinite() const
	{
		return FMath::IsFinite(ServerTime) && !StartPosition.ContainsNaN() && !PostVelocity.ContainsNaN() && !Gravity.ContainsNaN();
	}
};
