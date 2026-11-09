#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HitWallReactionLibrary.h"
#include "ReactionWarpPolicy.generated.h"

UENUM(BlueprintType)
enum class EReactionWarpMode : uint8 { Legacy, Off, Uniform, MiddleOnly, MultiPhase };

USTRUCT(BlueprintType)
struct FReactionPhaseWarpPolicy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PhaseId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AssetStartSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AssetEndSeconds = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SemanticLabel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bAllowWarp = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MinScale = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PreservationWeight = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float TransitionSeconds = 0.04f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bSemanticReviewed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWarpPolicyReviewed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ReviewNotes;
};

/** Separate from generated analysis: re-analysis never overwrites a human policy. */
UCLASS(BlueprintType)
class GE_AIREPLICATION_API UReactionWarpPolicy : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SchemaVersion = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PolicyVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CandidateId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceHash;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString AnalysisHash;
    // Digest of baked positions, times and phase boundaries, not just their declared hashes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString AnalysisBinding;
    // Explicit approval digest. Editing any policy field invalidates this digest.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString ApprovedPolicyHash;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float LateralLimitCm = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SmoothnessWeight = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FReactionPhaseWarpPolicy> Phases;
};
