#include "RubberbandCharacter.h"
#include "RubberbandInputScriptComponent.h"
#include "RubberbandMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ARubberbandCharacter::ARubberbandCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URubberbandMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	InputScript = CreateDefaultSubobject<URubberbandInputScriptComponent>(TEXT("InputScript"));
	// Replicate the server move timestamp even while the server uses Exponential smoothing.
	// Linear smoothing on clients needs it (otherwise it completes instantly = no smoothing),
	// and the recorder/adaptive smoothing use it as the received state's server time.
	GetCharacterMovement()->bNetworkAlwaysReplicateTransformUpdateTimestamp = true;
}
