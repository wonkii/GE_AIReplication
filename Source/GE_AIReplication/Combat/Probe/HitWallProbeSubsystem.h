#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HitWallProbeSubsystem.generated.h"

/**
 * Attaches a UHitWallProbeComponent to every Character that owns a UHitReactionComponent,
 * in every game/PIE world (server and clients), so no Blueprint has to change.
 * The probe stays idle until `hitwall.Probe 1` or `-HitWallProbe` is set.
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

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleActorSpawned(AActor* Actor);
	void AttachProbe(AActor* Actor);

	FDelegateHandle SpawnHandle;
	FString SessionStamp;
};
