#include "RubberbandMovementComponent.h"
#include "RubberbandRecordingSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

void URubberbandMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation,
	const FVector& NewLocation, const FQuat& NewRotation)
{
	URubberbandRecordingSubsystem* Recorder = GetWorld() ? GetWorld()->GetSubsystem<URubberbandRecordingSubsystem>() : nullptr;
	if (Recorder && CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Values before Super: the correction the client is about to absorb and the smoothing it will use.
		Recorder->RecordCorrection(*CharacterOwner, OldLocation, NewLocation, Velocity.Size(),
			CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp());
	}
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
}
