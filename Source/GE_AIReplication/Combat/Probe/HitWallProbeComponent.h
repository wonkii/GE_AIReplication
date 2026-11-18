#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"
#include "Combat/Reaction/HitWallReactionLibrary.h"
#include "HitWallProbeComponent.generated.h"

class ACharacter;
class UHitReactionComponent;

/**
 * Measurement only. Records the owner's capsule, mesh, pelvis, floor and wall contact
 * every frame while a UHitReactionComponent reaction plays, on every peer (server and
 * clients), plus a short tail after the reaction ends. Never moves or corrects anything.
 *
 * Enable at runtime with the console variable `hitwall.Probe 1` or the command line
 * flag `-HitWallProbe`. Optional `hitwall.Label <text>` tags the next reactions.
 * Output: Saved/HitWallProbe/<session>_<role>/<actor>/hit_frames.csv, hit_events.csv
 * Analyse with Scripts/Analysis/hitwall_probe.py.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class GE_AIREPLICATION_API UHitWallProbeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UHitWallProbeComponent();

	/** Same session stamp is shared by every probe in one world, set by the subsystem. */
	void SetSessionStamp(const FString& InStamp) { SessionStamp = InStamp; }

	static bool IsProbeEnabled();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION() void HandleReactionChanged(bool bActive, int32 ReactionId);
	UFUNCTION() void HandleOwnerHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	bool EnsureFiles();
	void BeginReactionRecord(int32 ReactionId);
	void EndReactionRecord(const TCHAR* Reason);
	void WriteFrame(float DeltaTime);
	FString RoleName() const;

	UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
	UPROPERTY(Transient) TObjectPtr<UHitReactionComponent> HitReaction;

	FSharpRecordingFiles Files;
	FString SessionStamp;
	bool bFilesTried = false;

	// Current reaction record.
	bool bRecording = false;
	bool bInTail = false;
	double TailUntil = 0.0;
	int32 CurrentReactionId = 0;
	int32 FrameCount = 0;
	FString CurrentLabel;
	FString CurrentDirection;
	float CurrentFacingYaw = 0.f;
	FVector StartLocation = FVector::ZeroVector;
	double StartUtcMs = 0.0;
	double StartWorldTime = 0.0;

	// Wall contacts reported by movement sweeps since the last written frame.
	int32 PendingWallHits = 0;
	FVector PendingWallNormal = FVector::ZeroVector;
	FString PendingWallActor;
	int32 PendingPawnHits = 0;
	FString PendingPawnActor;
	int32 TotalPawnHitFrames = 0;
	TWeakObjectPtr<class UAnimMontage> ReactionMontage;
	int32 TotalWallHitFrames = 0;
	int32 TotalOverlapFrames = 0;
	float MaxCapsuleRise = 0.f;
    FVector PreviousLeftFoot = FVector::ZeroVector, PreviousRightFoot = FVector::ZeroVector;
    bool bPreviousLeftContact = false, bPreviousRightContact = false;
    double LeftContactTravel = 0., RightContactTravel = 0.;
    TArray<FHitWallTrajectorySample> CurrentWarpTrack;
    float WarpStartAssetTime = 0.f;
};
