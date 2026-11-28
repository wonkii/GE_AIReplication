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
	// Pauses (no updates while standing still) are not jitter: skip them (see JitterMaxGap).
	if (ServerStamp > 0.f && LastServerStamp > 0.f && ServerStamp > LastServerStamp && LastArrival >= 0.0 && LastGap <= JitterMaxGap)
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

float URubberbandMovementComponent::ComputeBufferDelay() const
{
	if (!bBufferAdaptive) { return BufferDelay; }
	return FMath::Clamp(BufferBaseDelay + BufferJitterGain * Jitter, BufferBaseDelay, FMath::Max(BufferBaseDelay, MaxBufferDelay));
}

void URubberbandMovementComponent::PushBufferedState(const FVector& Location)
{
	const UWorld* World = GetWorld();
	if (!World || !CharacterOwner) { return; }
	const double Stamp = CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp();
	if (Stamp <= 0.0) { return; }
	if (StateBuffer.Num() > 0)
	{
		if (Stamp <= StateBuffer.Last().ServerTime) { return; } // duplicate or reordered state
		if (FVector::Dist(Location, StateBuffer.Last().Location) > NetworkNoSmoothUpdateDistance) { StateBuffer.Reset(); } // teleport
	}
	// Clock mapping: the fastest arrival sets the offset; release it slowly so clock drift cannot pin it.
	// The stamp is the moving client's move time, which stops while it stands still: after a pause the offset
	// jumps by the pause length. Re-sync (and drop the old states) instead of drawing far ahead of the buffer.
	const double Sample = World->GetRealTimeSeconds() - Stamp;
	if (bHasClockOffset && Sample - ClockOffset > JitterMaxGap) { StateBuffer.Reset(); bHasClockOffset = false; }
	if (!bHasClockOffset || Sample < ClockOffset) { ClockOffset = Sample; bHasClockOffset = true; }
	else { ClockOffset += (Sample - ClockOffset) * 0.002; }
	StateBuffer.Add({ Stamp, Location });
	if (StateBuffer.Num() > 64) { StateBuffer.RemoveAt(0, StateBuffer.Num() - 64); }
}

bool URubberbandMovementComponent::ComputeBufferedLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || !bHasClockOffset || StateBuffer.Num() < 2) { return false; }
	const double RenderTime = World->GetRealTimeSeconds() - ClockOffset - ComputeBufferDelay();
	int32 Index = 0;
	while (Index < StateBuffer.Num() && StateBuffer[Index].ServerTime < RenderTime) { ++Index; }
	if (Index == 0) { OutLocation = StateBuffer[0].Location; return true; }
	if (Index == StateBuffer.Num())
	{
		// Starved: continue the last segment for at most 0.1 s, then hold.
		const FBufferedState& A = StateBuffer[Index - 2];
		const FBufferedState& B = StateBuffer[Index - 1];
		const double Span = FMath::Max(B.ServerTime - A.ServerTime, 0.001);
		const double Ahead = FMath::Min(RenderTime - B.ServerTime, 0.1);
		OutLocation = B.Location + (B.Location - A.Location) * static_cast<float>(Ahead / Span);
		return true;
	}
	const FBufferedState& A = StateBuffer[Index - 1];
	const FBufferedState& B = StateBuffer[Index];
	const double Alpha = (RenderTime - A.ServerTime) / FMath::Max(B.ServerTime - A.ServerTime, 0.001);
	OutLocation = FMath::Lerp(A.Location, B.Location, static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0)));
	return true;
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
		PushBufferedState(NewLocation);
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

	// M4: replace the mesh offset with the buffered (delayed, interpolated) server path.
	if (bInterpBuffer && NetworkSmoothingMode == ENetworkSmoothingMode::Exponential && CharacterOwner
		&& CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && HasPredictionData_Client() && UpdatedComponent)
	{
		FVector Desired;
		if (ComputeBufferedLocation(Desired))
		{
			GetPredictionData_Client_Character()->MeshTranslationOffset = Desired - UpdatedComponent->GetComponentLocation();
			bNetworkSmoothingComplete = false; // keep being called every frame
			SmoothClientPosition_UpdateVisuals();
		}
		return;
	}

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
