#include "RubberbandMovementComponent.h"
#include "RubberbandRecordingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

void URubberbandMovementComponent::UpdateJitter()
{
	const UWorld* World = GetWorld();
	if (!World || !CharacterOwner) { return; }
	const double Arrival = World->GetRealTimeSeconds();
	const float ServerStamp = CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp();
	LastGap = LastArrival >= 0.0 ? static_cast<float>(Arrival - LastArrival) : 0.f;
	// RFC 3550 6.4.1: D = (R_i - R_j) - (S_i - S_j), J += (|D| - J) / 16. Clock offsets cancel.
	// Stamps of 0 (not replicated) or not newer (duplicate/reordered state) carry no timing information.
	if (ServerStamp > 0.f && LastServerStamp > 0.f && ServerStamp > LastServerStamp && LastArrival >= 0.0)
	{
		const double D = (Arrival - LastArrival) - static_cast<double>(ServerStamp - LastServerStamp);
		Jitter += (static_cast<float>(FMath::Abs(D)) - Jitter) / 16.f;
	}
	if (ServerStamp > LastServerStamp) { LastServerStamp = ServerStamp; }
	LastArrival = Arrival;
}

float URubberbandMovementComponent::ComputeSmoothTime() const
{
	const float T0 = NetworkSimulatedSmoothLocationTime;
	if (!bAdaptiveSmoothing || !bAdaptiveTime) { return T0; }
	return FMath::Clamp(T0 + JitterGain * FMath::Max(0.f, Jitter - JitterDeadZone), T0, FMath::Max(T0, MaxSmoothTime));
}

FString URubberbandMovementComponent::TuningLabel(float Gain, float MaxTime)
{
	const URubberbandMovementComponent* Defaults = GetDefault<URubberbandMovementComponent>();
	FString Label;
	if (!FMath::IsNearlyEqual(MaxTime, Defaults->MaxSmoothTime, 0.001f)) { Label += FString::Printf(TEXT("-m%.2f"), MaxTime); }
	if (!FMath::IsNearlyEqual(Gain, Defaults->JitterGain, 0.01f)) { Label += FString::Printf(TEXT("-g%.1f"), Gain); }
	return Label;
}

void URubberbandMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation,
	const FVector& NewLocation, const FQuat& NewRotation)
{
	const bool bSimulated = CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy;
	if (bSimulated)
	{
		UpdateJitter();
		if (bAdaptiveSmoothing && NetworkSmoothingMode == ENetworkSmoothingMode::Exponential)
		{
			// M2: Exponential smoothing reads the cached time every frame; set it per correction.
			if (FNetworkPredictionData_Client_Character* Data = GetPredictionData_Client_Character()) { Data->SmoothNetUpdateTime = ComputeSmoothTime(); }
		}
		if (URubberbandRecordingSubsystem* Recorder = GetWorld() ? GetWorld()->GetSubsystem<URubberbandRecordingSubsystem>() : nullptr)
		{
			// Values before Super: the correction the client is about to absorb and the smoothing it will use.
			Recorder->RecordCorrection(*CharacterOwner, OldLocation, NewLocation, Velocity.Size(),
				CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp(), Jitter, LastGap);
		}
	}
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
}

void URubberbandMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	bRateLimitedLastFrame = false;
	FNetworkPredictionData_Client_Character* Data = bAdaptiveSmoothing && bRateLimit
		&& NetworkSmoothingMode == ENetworkSmoothingMode::Exponential
		&& CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && HasPredictionData_Client()
		? GetPredictionData_Client_Character() : nullptr;
	const FVector Before = Data ? Data->MeshTranslationOffset : FVector::ZeroVector;

	Super::SmoothClientPosition(DeltaSeconds);

	if (!Data || DeltaSeconds <= 0.f || Before.Size() > RateLimitMaxOffset) { return; }
	// M3: limit how fast the shown mesh closes the gap, so a large late correction is spread instead of snapped.
	const FVector Step = Data->MeshTranslationOffset - Before;
	const float MaxStep = MaxCatchUpSpeed * DeltaSeconds;
	if (Step.Size() <= MaxStep) { return; }
	Data->MeshTranslationOffset = Before + Step.GetSafeNormal() * MaxStep;
	bRateLimitedLastFrame = true;
	// Super may have marked smoothing complete when its own offset reached zero; keep smoothing until ours does.
	bNetworkSmoothingComplete = false;
	SmoothClientPosition_UpdateVisuals();
}
