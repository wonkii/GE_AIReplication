#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SharpMoveSnapshot.h"
#include "SharpSnapshotChannelComponent.generated.h"

class ASharpEnemyCharacter;

// EventOnly and EventPredictor move the simulated-proxy CAPSULE from this
// channel. The owning Actor still replicates identity, status and other state;
// ordinary ACharacter FRepMovement is disabled only during event trials.
UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpSnapshotChannelComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USharpSnapshotChannelComponent();
    bool Publish(const FSharpMoveSnapshot& InSnapshot);
    void Invalidate();
    void HandleSessionChanged();
    bool IsPredicting() const { return bClientEventMode && bHasAnchor && bPredictorMode && bPredictionWithinHorizon; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FSharpMoveSnapshot Snapshot;
    // Both detector events and recovery anchors use one global monotonic stream.
    uint32 LastPublishedSequence = 0;
    uint32 LastObservedSequence = 0;
    int32 LastObservedGeneration = 0, LastObservedSegment = 0;
    double LastPublishedServerTime = -1.0;

    // Client-side movement replacement (non-replicated).
    bool bClientEventMode = false;
    bool bPredictorMode = false;
    bool bHasAnchor = false;
    bool bPredictionWithinHorizon = false;
    bool bSavedCMCTickEnabled = true;
    ENetworkSmoothingMode SavedSmoothing = ENetworkSmoothingMode::Exponential;
    int32 ActiveGeneration = 0, ActiveSegment = 0;
    FSharpMoveSnapshot Anchor;
    FVector CorrectionOffset = FVector::ZeroVector;
    float CorrectionElapsed = 0.0f;

    void UpdateClientMovementOwnership();
    void ApplyClientState(float DeltaSeconds);
    void ReleaseClientMovementOwnership();
    UFUNCTION() void OnRep_Snapshot();
};
