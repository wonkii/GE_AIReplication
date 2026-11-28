#include "LocalRubberbandMovementComponent.h"
#include "LocalRubberbandRecordingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
void ULocalRubberbandMovementComponent::OnClientCorrectionReceived(
    FNetworkPredictionData_Client_Character& ClientData, float TimeStamp,
    FVector NewLocation, FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData,
    FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
    if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && GetWorld())
    {
        if (auto* Recorder = GetWorld()->GetSubsystem<ULocalRubberbandRecordingSubsystem>())
        {
            const int32 Id = Recorder->RecordCorrection(*CharacterOwner, TimeStamp, NewLocation, NewVelocity,
                bBaseRelativePosition, ServerMovementMode, ClientData.SavedMoves.Num());
            if (Id > 0)
            {
                if (FirstEvent == 0) { FirstEvent = Id; BeforeFirst = CharacterOwner->GetActorLocation(); }
                LastEvent = Id;
            }
        }
    }
    Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData,
        NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}
bool ULocalRubberbandMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
    const bool bObserve = FirstEvent > 0 && CharacterOwner && GetWorld();
    const FVector BeforeReplay = bObserve ? CharacterOwner->GetActorLocation() : FVector::ZeroVector;
    const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
    if (bObserve && CharacterOwner)
    {
        if (auto* Recorder = GetWorld()->GetSubsystem<ULocalRubberbandRecordingSubsystem>())
        {
            Recorder->RecordReplay(*CharacterOwner, FirstEvent, LastEvent, BeforeFirst, BeforeReplay, bResult);
        }
        ResetObservation();
    }
    return bResult;
}
