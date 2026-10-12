#include "SharpMovementDetectorComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

void USharpMovementDetectorComponent::BeginPlay()
{
	Super::BeginPlay(); Enemy = Cast<ASharpEnemyCharacter>(GetOwner());
	if (Enemy && Enemy->HasAuthority()) { Enemy->OnCharacterMovementUpdated.AddDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
}

void USharpMovementDetectorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Stop();
	if (Enemy) { Enemy->OnCharacterMovementUpdated.RemoveDynamic(this, &USharpMovementDetectorComponent::ObserveMovement); }
	Super::EndPlay(Reason);
}

void USharpMovementDetectorComponent::Start(const FSharpDetectorConfig& InConfig)
{
	Stop(); Config = InConfig; DetectionCount = 0;
	bActive = Enemy && Enemy->HasAuthority() && Config.IsValid();
	if (bActive)
	{
		FSharpMovementSample Initial;
		Initial.ServerTime = GetWorld()->GetTimeSeconds(); Initial.Position = Enemy->GetActorLocation();
		Initial.Velocity = Enemy->GetCharacterMovement()->Velocity;
		Initial.MovementMode = Enemy->GetCharacterMovement()->MovementMode; Initial.CustomMode = Enemy->GetCharacterMovement()->CustomMovementMode;
		Classifier.Observe(Initial, Config);
	}
}

void USharpMovementDetectorComponent::Stop()
{
	if (bActive && bRisk && Enemy)
	{
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("RiskEnd"), DetectionCount, GetWorld()->GetTimeSeconds(), 0.0, 0.0, TEXT("DetectorStopped"));
	}
	bActive = bRisk = false; Classifier.Reset();
}

void USharpMovementDetectorComponent::ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!bActive || !Enemy || !Enemy->HasAuthority()) { return; }
	FSharpMovementSample Sample;
	Sample.ServerTime = GetWorld()->GetTimeSeconds(); Sample.Position = Enemy->GetActorLocation();
	Sample.Velocity = Enemy->GetCharacterMovement()->Velocity;
	Sample.MovementMode = Enemy->GetCharacterMovement()->MovementMode; Sample.CustomMode = Enemy->GetCharacterMovement()->CustomMovementMode;
	const double Begin = FPlatformTime::Seconds();
	const FSharpClassification Result = Classifier.Observe(Sample, Config);
	const double Microseconds = (FPlatformTime::Seconds() - Begin) * 1.e6;
	bRisk = Result.bRisk;
	if (Result.bSharpEvent) { ++DetectionCount; }
	Enemy->MovementRecorder->RecordDiagnostic(TEXT("DetectorSample"), DetectionCount, Sample.ServerTime, Result.DeltaVelocity, Microseconds,
		FString::Printf(TEXT("Angle=%.6f Acceleration=%.6f Risk=%d Reasons=%d"), Result.DirectionDegrees, Result.Acceleration, bRisk, Result.Reasons));
	if (Result.bSharpEvent) { Enemy->MovementRecorder->RecordDiagnostic(TEXT("DetectorEvent"), DetectionCount, Sample.ServerTime, Result.DeltaVelocity, Result.DirectionDegrees, FString::FromInt(Result.Reasons)); }
	if (Result.bRiskStarted) { Enemy->MovementRecorder->RecordDiagnostic(TEXT("RiskStart"), DetectionCount, Sample.ServerTime, 0.0, 0.0, FString::FromInt(Result.Reasons)); }
	if (Result.bRiskEnded) { Enemy->MovementRecorder->RecordDiagnostic(TEXT("RiskEnd"), DetectionCount, Sample.ServerTime, 0.0, 0.0, TEXT("HysteresisReleaseOrSampleReset")); }
	OnClassified.Broadcast(Sample, Result);
}
