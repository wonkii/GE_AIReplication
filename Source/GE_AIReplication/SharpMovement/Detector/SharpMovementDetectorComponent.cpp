#include "SharpMovementDetectorComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Net/SharpDetectorNetworkBridge.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

void USharpMovementDetectorComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy && Enemy->HasAuthority()) { Enemy->OnCharacterMovementUpdated.AddUniqueDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
}

FSharpMovementSample USharpMovementDetectorComponent::Sample() const
{
	FSharpMovementSample Result;
	Result.ServerTime = GetWorld()->GetTimeSeconds(); Result.Position = Enemy->GetActorLocation(); Result.Velocity = Enemy->GetVelocity();
	Result.MovementMode = static_cast<uint8>(Enemy->GetCharacterMovement()->MovementMode);
	Result.CustomMode = Enemy->GetCharacterMovement()->CustomMovementMode;
	return Result;
}

void USharpMovementDetectorComponent::Start(const USharpExperimentSettings& Settings)
{
	if (!Enemy || !Enemy->HasAuthority()) { return; }
	Config = Settings.Detection.MakeConfig(Settings.VelocityChangeThreshold, Settings.DirectionChangeThresholdDegrees);
	bAirborneModel = Settings.Prediction.bEnableAirborneModel;
	Classifier.Reset(); Classifier.Observe(Sample(), Config); bObserving = true;
}

void USharpMovementDetectorComponent::Stop()
{
	bObserving = false; Classifier.Reset();
}

void USharpMovementDetectorComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!bObserving || !Enemy || !Enemy->HasAuthority() || !Enemy->Experiment->IsObservingMovement()) { return; }
	const FSharpMovementSample Observed = Sample();
	const double Begin = FPlatformTime::Seconds();
	const FSharpClassification Result = Classifier.Observe(Observed, Config);
	const double Cost = (FPlatformTime::Seconds() - Begin) * 1000000.0;
	FString Route = TEXT("NoEvent");
	if (Result.bSharpEvent)
	{
		// Lifetime sequences remain monotonic across resets; exhaustion fails explicitly.
		if (Sequence == MAX_uint32) { Enemy->Experiment->Fail(TEXT("Detector sequence exhausted; start a new world")); return; }
		FSharpMoveSnapshot Event;
		Event.SequenceId = ++Sequence; Event.Generation = Enemy->Experiment->Status.Generation; Event.Segment = Enemy->Experiment->Status.Segment;
		Event.ServerTime = Observed.ServerTime; Event.StartPosition = Observed.Position; Event.PostVelocity = Observed.Velocity;
		Event.MovementMode = Observed.MovementMode; Event.CustomMovementMode = Observed.CustomMode;
		Event.Gravity = FVector(0, 0, Enemy->GetCharacterMovement()->GetGravityZ());
		// EventOnly and EventPredictor send identical movement payloads. Only client presentation differs.
		if (Observed.MovementMode == MOVE_Walking || Observed.MovementMode == MOVE_NavWalking) { Event.PredictionType = ESharpPredictionType::ConstantVelocity; }
		else if (Observed.MovementMode == MOVE_Falling && bAirborneModel) { Event.PredictionType = ESharpPredictionType::Airborne; }
		const ESharpDetectorNetworkDispatchResult Dispatch = FSharpDetectorNetworkBridge::DispatchDetectedEvent(Enemy, Event);
		Route = Dispatch == ESharpDetectorNetworkDispatchResult::Requested ? TEXT("Requested") :
			Dispatch == ESharpDetectorNetworkDispatchResult::BaselineSkipped ? TEXT("BaselineSkipped") : TEXT("Rejected");
	}
	Enemy->Recorder->RecordDetection(Sequence, Result, Cost, Route);
}

void USharpMovementDetectorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	if (Enemy) { Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
	Super::EndPlay(Reason);
}
