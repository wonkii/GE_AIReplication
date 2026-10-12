#pragma once

#include "CoreMinimal.h"
#include "SharpImpulseTypes.generated.h"

UENUM(BlueprintType)
enum class ESharpImpulseDirectionSpace : uint8 { World, VolumeLocal, PathTangent };

UENUM(BlueprintType)
enum class ESharpImpulseTriggerPolicy : uint8 { OncePerTrial, OncePerEntry };

UENUM(BlueprintType)
enum class ESharpImpulseFollowPolicy : uint8 { Continue, SuspendInput };

USTRUCT(BlueprintType)
struct FSharpImpulseSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	FVector ImpulseVector = FVector(0.0, 300.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	ESharpImpulseDirectionSpace DirectionSpace = ESharpImpulseDirectionSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse")
	bool bVelocityChange = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	ESharpImpulseTriggerPolicy TriggerPolicy = ESharpImpulseTriggerPolicy::OncePerTrial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (ClampMin = "0.0", Units = "s"))
	float RearmDelay = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow")
	ESharpImpulseFollowPolicy FollowPolicy = ESharpImpulseFollowPolicy::Continue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (ClampMin = "0.001", Units = "s"))
	float InputSuspendSeconds = 0.25f;

	bool IsValid(FString& OutReason) const;
};
