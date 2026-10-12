#include "SharpCharacterMovementComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Measurement/SharpMovementRecorder.h"
#include "Components/SkeletalMeshComponent.h"

void USharpCharacterMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(CharacterOwner);
	const FVector OldMesh = Enemy && Enemy->GetMesh() ? Enemy->GetMesh()->GetComponentLocation() : FVector::ZeroVector;
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
	if (Enemy)
	{
		Enemy->MovementRecorder->RecordDiagnostic(TEXT("SmoothCorrection"), Enemy->MovementGeneration,
			Enemy->GetReplicatedServerLastTransformUpdateTimeStamp(), (NewLocation - OldLocation).Size(), 0.0, TEXT("SuperPreserved"));
		Enemy->VisualPredictor->NotifyCorrection(OldMesh);
	}
}

void USharpCharacterMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	const ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(CharacterOwner);
	if (!Enemy || !Enemy->VisualPredictor || !Enemy->VisualPredictor->OwnsPresentation()) { Super::SmoothClientPosition(DeltaSeconds); }
}
