#pragma once
#include "CoreMinimal.h"
#include "SharpImpulseTypes.generated.h"

UENUM(BlueprintType)
enum class ESharpImpulseDirectionSpace : uint8 { World, VolumeLocal, PathTangent };
UENUM(BlueprintType)
enum class ESharpImpulseTriggerPolicy : uint8 { OncePerExecution, OncePerEntry };
UENUM(BlueprintType)
enum class ESharpImpulseFollowPolicy : uint8 { Continue, SuspendInput };

USTRUCT(BlueprintType)
struct FSharpImpulseSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseExperimentImpulse = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector ImpulseVector = FVector(0, 700, 250);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESharpImpulseDirectionSpace DirectionSpace = ESharpImpulseDirectionSpace::World;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bVelocityChange = true;
	// Scenario label for Detector evaluation, independent of its own classification.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIsSharpEvent = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESharpImpulseTriggerPolicy TriggerPolicy = ESharpImpulseTriggerPolicy::OncePerExecution;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0")) float RearmDelay = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ESharpImpulseFollowPolicy FollowPolicy = ESharpImpulseFollowPolicy::Continue;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.001")) float InputSuspendSeconds = 0.25f;
	bool IsValid(FString& OutReason) const;
};
