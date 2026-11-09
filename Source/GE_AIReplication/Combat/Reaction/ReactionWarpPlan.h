#pragma once

#include "CoreMinimal.h"
#include "ReactionWarpPolicy.h"
#include "ReactionWarpPlan.generated.h"

class UAnimMontage;

UENUM()
enum class EReactionWarpResult : uint8 { Unmodified, Planned, Fallback };

USTRUCT()
struct FReactionWarpScaleKey
{
    GENERATED_BODY()
    UPROPERTY() float Time = 0.f;
    UPROPERTY() float Scale = 1.f;
};

USTRUCT()
struct FReactionWarpPhaseTravel
{
    GENERATED_BODY()
    UPROPERTY() int32 PhaseId = 0;
    UPROPERTY() float OriginalCm = 0.f;
    UPROPERTY() float PlannedCm = 0.f;
};

/** Small server-authored payload; no poses, samples or environment are replicated. */
USTRUCT()
struct FReactionWarpPlan
{
    GENERATED_BODY()
    UPROPERTY() int32 SchemaVersion = 2;
    UPROPERTY() int32 ReactionId = 0;
    UPROPERTY() FName CandidateId;
    UPROPERTY() FString SourceHash;
    UPROPERTY() FString AnalysisHash;
    UPROPERTY() FString PolicyHash;
    UPROPERTY() FString PlanHash;
    UPROPERTY() TObjectPtr<UAnimMontage> Montage = nullptr;
    UPROPERTY() TSoftObjectPtr<UHitWallReactionLibrary> Library;
    UPROPERTY() TSoftObjectPtr<UReactionWarpPolicy> Policy;
    UPROPERTY() float PlayRate = 1.f;
    UPROPERTY() double ServerStartTime = 0.;
    UPROPERTY() EReactionWarpMode Mode = EReactionWarpMode::Off;
    UPROPERTY() EReactionWarpResult Result = EReactionWarpResult::Unmodified;
    UPROPERTY() FString Reason;
    UPROPERTY() bool bDraft = false;
    UPROPERTY() bool bPathChecked = false;
    UPROPERTY() float BudgetCm = 0.f;
    UPROPERTY() float OriginalCm = 0.f;
    UPROPERTY() float MinimumCm = 0.f;
    UPROPERTY() float MaximumCm = 0.f;
    UPROPERTY() float MaxLateralCm = 0.f;
    UPROPERTY() float MaxLateralTime = 0.f;
    UPROPERTY() TArray<FReactionWarpScaleKey> Keys;
    UPROPERTY() TArray<FReactionWarpPhaseTravel> PhaseTravel;
};

/** Pure, deterministic planner shared by tools, sweeps and the runtime modifier. */
namespace ReactionWarp
{
    constexpr int32 MaxPhases = 16;
    constexpr int32 MaxKeys = 64;
    GE_AIREPLICATION_API FString AnalysisBinding(const FHitWallReactionCandidate& Candidate);
    GE_AIREPLICATION_API FString PolicyHash(const UReactionWarpPolicy& Policy);
    GE_AIREPLICATION_API FString PlanHash(const FReactionWarpPlan& Plan);
    GE_AIREPLICATION_API FString Validate(const FHitWallReactionCandidate& Candidate, const UReactionWarpPolicy& Policy, bool bAllowDraft);
    GE_AIREPLICATION_API float ScaleAt(const TArray<FReactionWarpScaleKey>& Keys, float Time);
    GE_AIREPLICATION_API FVector Integrate(const TArray<FHitWallTrajectorySample>& Track,
        const TArray<FReactionWarpScaleKey>& Keys, float Start, float End);
    GE_AIREPLICATION_API FReactionWarpPlan Build(const FHitWallReactionCandidate& Candidate,
        const UReactionWarpPolicy& Policy, float BudgetCm, EReactionWarpMode Mode,
        bool bAllowDraft = false, int32 MaxIterations = 4096);
    GE_AIREPLICATION_API bool SelfTest(FString& Report);
}
