#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/Reaction/ReactionWarpPlan.h"
#include "HitWallProbeSubsystem.generated.h"

class ACharacter;
class APawn;
class APlayerController;
class ACameraActor;

/**
 * 1) Attaches a UHitWallProbeComponent to every Character that owns a UHitReactionComponent,
 *    in every game/PIE world (server and clients), so no Blueprint has to change.
 * 2) On the authority world, runs automated trials (hitwall.Run): places the enemy at an exact
 *    gap in front of an existing wall, places the player at a fixed distance, toggles the
 *    enemy's warp, delivers one server hit, waits for the reaction and tail, repeats.
 * Compiled out of Shipping builds.
 */
UCLASS()
class GE_AIREPLICATION_API UHitWallProbeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	struct FRunConfig
	{
		TArray<float> GapsCm;      // capsule surface to wall face, along the retreat direction
		TArray<int32> WarpModes;   // 0 = off, 1 = on
		int32 Repeats = 10;
		float StandoffCm = 200.f;  // player centre to enemy centre
		float SettleSeconds = 0.6f;
		float TailSeconds = 0.8f;
        bool bTour = false;
        bool bAudit = false;
        bool bIncludeDisabled = false;
        bool bCamera = true;
        bool bMultiPhaseCompare = false;
        bool bAllowDraft = false;
        TArray<EReactionWarpMode> PlanModes;
	};

	/**
	 * bStations = false: one station at the enemy's current spot, obstacle searched away from the player.
	 * bStations = true : every actor tagged "HitWall_<Name>" is a station; the enemy stands there and
	 *                    retreats along the actor's forward (red X) arrow toward the obstacle.
	 */
	bool StartRun(const FRunConfig& Config, bool bStations, FString& OutMessage);
	void StopRun(const TCHAR* Reason);
    void PauseTour(bool bPause) { bTourPaused = bPause; }
	bool IsRunning() const { return Phase != EPhase::Idle; }
    const FString& GetWarpTaskDirectory() const { return WarpTaskDirectory; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleActorSpawned(AActor* Actor);
	void AttachProbe(AActor* Actor);
	void TickRun();
	void PlaceForTrial();
	void SetWarp(bool bOn) const;
    void RecordWarpTrial(const FString& Status, const FString& Reason);

	FDelegateHandle SpawnHandle;
	FString SessionStamp;

	// Automated run state (authority world only).
	enum class EPhase : uint8 { Idle, Settle, WaitStart, WaitEnd, Tail };
	struct FTrial { float Gap = 0.f; int32 Warp = 0; int32 Rep = 0; int32 Station = 0; };
	struct FStation
	{
		FString Name;
		FVector Anchor = FVector::ZeroVector;   // enemy capsule centre at the station
		FVector Back = FVector::ForwardVector;  // retreat direction, toward the obstacle
		FString Error;
        FVector Normal = FVector::ZeroVector;
        FString Obstacle;
        double WallDistance = 0.0;              // capsule travel from Anchor until it touches the obstacle
	};
	bool MeasureStation(UWorld& World, ACharacter& Target, APawn& PlayerPawn, FStation& Station, double MaxGap, FString& OutWhy) const;
	TArray<FStation> Stations;
    bool bTourPaused = false;
    FString PlacementError;
    TWeakObjectPtr<APlayerController> ViewController;
    TWeakObjectPtr<AActor> OriginalViewTarget;
    TWeakObjectPtr<ACameraActor> TourCamera;
    bool bInputLocked = false;
	EPhase Phase = EPhase::Idle;
	TArray<FTrial> Trials;
	int32 TrialIndex = -1;
	double PhaseStart = 0.0;
	int32 Rejected = 0;
	FRunConfig Run;
	TWeakObjectPtr<ACharacter> Enemy;
	TWeakObjectPtr<APawn> Player;
	bool bOriginalWarp = false;
	FTimerHandle RunTimer;
    FString WarpTaskDirectory;
    TArray<FString> WarpTrialRows;
    bool bTrialRecorded = false;
    EReactionWarpMode OriginalMode = EReactionWarpMode::Legacy;
    bool bOriginalDraft = false;
    FTransform OriginalEnemyTransform, OriginalPlayerTransform;
    FString OriginalLabel;
    int32 OriginalProbe = 0;
    float OriginalFPS = 0.f;
};
