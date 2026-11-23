#include "RubberbandCharacter.h"
#include "RubberbandInputScriptComponent.h"
#include "RubberbandMovementComponent.h"

ARubberbandCharacter::ARubberbandCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URubberbandMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	InputScript = CreateDefaultSubobject<URubberbandInputScriptComponent>(TEXT("InputScript"));
}
