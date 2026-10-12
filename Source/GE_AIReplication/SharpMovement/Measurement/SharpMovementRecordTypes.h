#pragma once

#include "CoreMinimal.h"
#include "../Scenario/SharpImpulseTypes.h"
#include "SharpMovementRecordTypes.generated.h"

USTRUCT(BlueprintType)
struct FSharpRecordingSettings
{
	GENERATED_BODY()

	// Relative to ProjectSavedDir; never accepts an absolute or parent-traversal path.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Files")
	FString OutputSubdirectory = TEXT("SharpMovement");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Files", meta = (ClampMin = "0.001", Units = "s"))
	float FlushIntervalSeconds = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Files", meta = (ClampMin = "0.001", Units = "s"))
	float ConnectionSampleSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Files", meta = (ClampMin = "1"))
	int32 MaximumBufferedRows = 4096;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Events", meta = (ClampMin = "0.001", Units = "s"))
	float EventObservationSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Events", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ObservableVelocityChange = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Events", meta = (ClampMin = "1"))
	int32 MaximumPendingEvents = 128;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Events", meta = (ClampMin = "1"))
	int32 MaximumPendingClientDiagnostics = 128;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trial", meta = (ClampMin = "0.0", Units = "s"))
	float PostTrialObservationSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Logger")
	bool bWriteVisualLogger = true;

	// Process-wide Visual Logger recording lease; manual recording is preserved.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual Logger")
	bool bAutoRecordVisualLoggerToFile = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Run Metadata", meta = (MultiLine = "true"))
	FString NetworkConditionNotes = TEXT("Network emulation disabled");

	bool IsValid(FString& OutReason) const;
};

USTRUCT(BlueprintType)
struct FSharpObservationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	bool bDrawServerCapsule = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	bool bDrawClientCapsule = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	bool bDrawVelocity = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	bool bDrawImpulseEvents = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	FColor ServerColor = FColor(255, 140, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	FColor ClientColor = FColor::Cyan;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing")
	FColor EventColor = FColor::Magenta;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.0", Units = "s"))
	float ShapeLifetimeSeconds = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.0", Units = "s"))
	float EventLifetimeSeconds = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.0", Units = "s"))
	float ArrowTimeScale = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.1", Units = "cm"))
	float ArrowHeadSize = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.1", Units = "cm"))
	float EventRadius = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "4", ClampMax = "64"))
	int32 EventSphereSegments = 12;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawing", meta = (ClampMin = "0.0"))
	float LineThickness = 1.0f;

	bool IsValid(FString& OutReason) const;
};

// Raw measurement metadata only. Never supplied to the M4 detector.
struct FSharpImpulseRecord
{
	int64 EventId = 0;
	FName VolumeId;
	FVector Position = FVector::ZeroVector;
	FVector PreVelocity = FVector::ZeroVector;
	FVector WorldImpulse = FVector::ZeroVector;
	FVector ObservedVelocity = FVector::ZeroVector;
	uint8 PreMovementMode = 0;
	uint8 PreCustomMode = 0;
	uint8 ObservedMovementMode = 0;
	uint8 ObservedCustomMode = 0;
	bool bVelocityChange = true;
	ESharpImpulseFollowPolicy FollowPolicy = ESharpImpulseFollowPolicy::Continue;
	double OverlapTime = 0.0;
	double CallTime = 0.0;
	double CallCompleteTime = 0.0;
	double FirstSampleTime = -1.0;
	double VelocityChangeTime = -1.0;
	uint64 CallFrame = 0;
	int64 CallOrdinal = 0;
	int64 FirstSampleId = 0;
	int64 ObservedSampleId = 0;
	FString Status = TEXT("Pending");
};
