#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Components/ActorComponent.h"
#include "SharpRandomWalkTypes.h"
#include "SharpRandomWalkerComponent.generated.h"

class ASharpEnemyCharacter;
class UCharacterMovementComponent;
class USharpRandomWalkerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSharpRandomWalkStateChanged,
	USharpRandomWalkerComponent*, Walker, ESharpRandomWalkState, State, const FString&, Reason);

/** Server-only deterministic movement-pattern driver.
 *  Walk/run straight, then stop-turn or moving-turn, and repeat.
 *  Uses normal CMC input rather than teleporting; the network policy never changes here.
 */
UCLASS(ClassGroup=(SharpMovement), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpRandomWalkerComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpRandomWalkerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sharp Random Walk")
	FSharpRandomWalkSettings Settings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	ESharpRandomWalkState State = ESharpRandomWalkState::Idle;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	ESharpRandomWalkPhase Phase = ESharpRandomWalkPhase::Straight;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
    ESharpRandomWalkBehavior ActiveBehavior = ESharpRandomWalkBehavior::StopTurnWalk;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	float SegmentDistance = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	float SegmentProgress = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	float TargetYaw = 0.0f;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	int32 SegmentIndex = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sharp Random Walk|Runtime")
	FString LastFailure;

	UPROPERTY(BlueprintAssignable, Category="Sharp Random Walk")
	FSharpRandomWalkStateChanged OnStateChanged;

	bool Prepare(int32 ActorId, FString& OutReason);
	bool CanStart(FString& OutReason) const;
	bool Start(FString& OutReason);
	bool Resume(FString& OutReason);
	void Suspend();
	void Stop();
	void Fail(const FString& Reason);
	void Reset();
	const FSharpRandomWalkSettings& GetPreparedSettings() const { return PreparedSettings; }
	int32 GetResolvedSeed() const { return ResolvedSeed; }
    // Assigned by ExperimentManager before Prepare; fallback remains BaseSeed + ActorId.
    void SetManagerSeed(int32 InSeed) { bHasManagerSeed = true; ManagerSeed = InSeed; }
    void ClearManagerSeed() { bHasManagerSeed = false; }

	// Runtime-only settings injected by the authoritative ExperimentManager before Prepare.
	// Never replicated: client-side characters only consume standard network movement.
	void ConfigureArena(bool bEnabled, const FVector& Center, float YawDegrees,
		const FVector2D& FullSizeCm, float MarginCm, float LookAheadCm,
		bool bProbeWorldStatic, float ProbeIntervalSeconds);
	bool IsInitialPositionInsideArena(const FVector& Position, float CapsuleRadiusCm) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	UPROPERTY(Transient) TObjectPtr<UCharacterMovementComponent> Movement;
	FSharpRandomWalkSettings PreparedSettings;
	FRandomStream Random;
	FVector SegmentStart = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float StallSeconds = 0.0f;
	float ProgressCheckpoint = 0.0f;
	float OriginalMaxWalkSpeed = 0.0f;
	bool bOriginalOrientToMovement = false;
	bool bOriginalUseControllerRotationYaw = false;
	bool bSpeedOverridden = false;
	int32 ResolvedSeed = 0;
    int32 ManagerSeed = 0;
    bool bHasManagerSeed = false;

	bool bArenaEnabled = false;
	FVector ArenaCenter = FVector::ZeroVector;
	float ArenaYaw = 0.0f;
	FVector2D ArenaSize = FVector2D::ZeroVector;
	float ArenaMargin = 0.0f;
	float AvoidLookAheadCm = 450.0f;
	float WallProbeIntervalSeconds = 0.15f;
	bool bProbeStaticWalls = true;
	float WallProbeElapsed = 0.0f;
	bool bAvoidanceTurn = false;
	int32 ConsecutiveAvoidanceAttempts = 0;
	static constexpr int32 MaxConsecutiveAvoidanceAttempts = 12;

	bool IsArenaPositionAllowed(const FVector& Position, float CapsuleRadiusCm) const;
	bool IsDirectionAllowed(const FVector& Direction, float DistanceCm, bool bTestWall, bool& bOutBoundary) const;
	bool DetectObstacle(const FVector& Direction, float DeltaTime, FString& OutKind);
	bool BeginAvoidance(const FString& Kind);
	void RecordAvoidance(const FString& Kind, float SignedAngle);

    float GetBehaviorSpeed() const;
    float GetBehaviorTurnRate() const;
    bool TurnsWhileMoving() const;
    ESharpRandomWalkBehavior PickBehavior();
	bool ValidateRuntime(FString& OutReason) const;
	void BeginStraight();
	void BeginTurn();
	void SetState(ESharpRandomWalkState NewState, const FString& Reason = FString());
	void RestoreMovementSettings();
};
