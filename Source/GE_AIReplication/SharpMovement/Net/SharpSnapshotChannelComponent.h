#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpMoveSnapshot.h"
#include "SharpSnapshotChannelComponent.generated.h"

UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpSnapshotChannelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpSnapshotChannelComponent();
	bool Publish(const FSharpMoveSnapshot& InSnapshot);
	void Invalidate();
	void HandleSessionChanged();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FSharpMoveSnapshot Snapshot;
	// Lifetime watermarks survive reset; the channel does not allocate event IDs.
	uint32 LastPublishedSequence = 0;
	uint32 LastObservedSequence = 0;
	int32 LastObservedGeneration = 0, LastObservedSegment = 0;
	UFUNCTION() void OnRep_Snapshot();
};
