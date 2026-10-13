#pragma once
#include "CoreMinimal.h"
#include "SharpRecordingSettings.generated.h"

USTRUCT(BlueprintType)
struct FSharpRecordingSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.05")) float FlushIntervalSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="64")) int32 MaximumBufferedRows = 4096;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0")) float PostTrialObservationSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01")) float EventObservationSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0")) float ObservableVelocityChange = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 MaximumPendingEvents = 64;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.05")) float ConnectionSampleSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDrawDebug = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bVisualLogger = false;
	bool IsValid() const;
};
