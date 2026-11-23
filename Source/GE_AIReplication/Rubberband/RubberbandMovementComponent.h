#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RubberbandMovementComponent.generated.h"

/**
 * Character movement for the rubber-banding mitigation (B) experiments.
 * Simulated proxies only; server/capsule movement is never changed, only how a correction is shown (mesh offset).
 *
 * Always: estimates arrival jitter of movement updates (RFC 3550 interarrival jitter) and reports every correction.
 * When bAdaptiveSmoothing (on top of Exponential smoothing):
 *   M2 bAdaptiveTime - smoothing time T = clamp(T0 + JitterGain * max(0, J - JitterDeadZone), T0, MaxSmoothTime),
 *                      T0 = NetworkSimulatedSmoothLocationTime. No jitter -> identical to the default.
 *   M3 bRateLimit    - the mesh offset may shrink by at most MaxCatchUpSpeed cm/s, unless it exceeds RateLimitMaxOffset.
 * See docs/B_체감완화/02_적응형스무딩_설계.md
 */
UCLASS()
class GE_AIREPLICATION_API URubberbandMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;
	virtual void SmoothClientPosition(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive") bool bAdaptiveSmoothing = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive") bool bAdaptiveTime = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive") bool bRateLimit = true;
	/** Jitter (s) treated as noise; measured 0.005-0.008 s without emulated jitter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive", meta=(ClampMin="0")) float JitterDeadZone = 0.010f;
	/** Seconds of extra smoothing per second of jitter above the dead zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive", meta=(ClampMin="0")) float JitterGain = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive", meta=(ClampMin="0.01")) float MaxSmoothTime = 0.25f;
	/** cm/s: how fast the shown mesh may close the gap to the capsule (on top of the capsule's own motion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive", meta=(ClampMin="1")) float MaxCatchUpSpeed = 300.f;
	/** cm: above this offset the rate limit is lifted so the picture never strays far from the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rubberband|Adaptive", meta=(ClampMin="0")) float RateLimitMaxOffset = 50.f;

	float GetJitter() const { return Jitter; }
	float GetLastArrivalGap() const { return LastGap; }
	bool WasRateLimited() const { return bRateLimitedLastFrame; }
	/** Smoothing time this component would apply now (T0 when not adaptive). */
	float ComputeSmoothTime() const;

private:
	double LastArrival = -1.0;
	float LastServerStamp = -1.f;
	float Jitter = 0.f;
	float LastGap = 0.f;
	bool bRateLimitedLastFrame = false;
	void UpdateJitter();
};
