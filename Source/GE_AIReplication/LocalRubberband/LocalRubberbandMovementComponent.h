#pragma once
#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LocalRubberbandMovementComponent.generated.h"
struct FMovementBaseInterfaceData;
/** Baseline probe only: never changes the native correction/replay path. */
UCLASS()
class GE_AIREPLICATION_API ULocalRubberbandMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    void ResetObservation() { FirstEvent = LastEvent = 0; }
protected:
    virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
        float TimeStamp, FVector NewLocation, FVector NewVelocity,
        FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName,
        bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;
    virtual bool ClientUpdatePositionAfterServerUpdate() override;
private:
    int32 FirstEvent = 0, LastEvent = 0;
    FVector BeforeFirst = FVector::ZeroVector;
};
