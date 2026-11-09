#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/AnimSequenceBase.h"
#include "HitWallReactionLibrary.generated.h"

USTRUCT(BlueprintType)
struct FHitWallTrajectorySample
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TimeSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AssetTimeSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform RootMotion;
};

USTRUCT(BlueprintType)
struct FHitWallMotionInterval
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float StartSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float EndSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AssetStartSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AssetEndSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Label;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Evidence;
};

/** Baked analysis contract for I1/I2. No authority, selection or collision policy is encoded here. */
USTRUCT(BlueprintType)
struct FHitWallReactionCandidate
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName CandidateId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UAnimSequenceBase> Animation;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Section;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SourceHash;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString AnalysisHash;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PlayRate = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DurationSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHitWallTrajectorySample> Trajectory;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHitWallMotionInterval> Phases;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHitWallMotionInterval> ProposedWarpWindows;
    // Only a human review plus runtime verification can promote the offline proposal.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bReviewed = false;
};

UCLASS(BlueprintType)
class GE_AIREPLICATION_API UHitWallReactionLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SchemaVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FHitWallReactionCandidate> Candidates;
    UFUNCTION(BlueprintPure, Category="Reaction Library")
    bool FindCandidate(FName CandidateId, FHitWallReactionCandidate& Candidate, bool bRequireReviewed = true) const;
};
