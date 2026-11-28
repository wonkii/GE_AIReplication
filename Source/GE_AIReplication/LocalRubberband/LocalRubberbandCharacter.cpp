#include "LocalRubberbandCharacter.h"
#include "LocalRubberbandInputScriptComponent.h"
#include "LocalRubberbandMovementComponent.h"
ALocalRubberbandCharacter::ALocalRubberbandCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<ULocalRubberbandMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    InputScript = CreateDefaultSubobject<ULocalRubberbandInputScriptComponent>(TEXT("InputScript"));
    GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp = true;
}
