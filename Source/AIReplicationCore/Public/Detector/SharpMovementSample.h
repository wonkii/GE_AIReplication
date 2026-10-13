#pragma once
#include "CoreMinimal.h"

struct FSharpMovementSample
{
	double ServerTime = 0.0;
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	uint8 MovementMode = 0;
	uint8 CustomMode = 0;
};

struct FSharpClassification
{
	double DeltaVelocity = 0.0;
	double Acceleration = 0.0;
	double DirectionDegrees = 0.0;
	uint8 Reasons = 0; // 1 velocity, 2 direction, 4 acceleration, 8 mode
	bool bRisk = false;
	bool bRiskStarted = false;
	bool bRiskEnded = false;
	bool bSharpEvent = false;
};
