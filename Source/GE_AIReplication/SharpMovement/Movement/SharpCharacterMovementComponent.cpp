#include "SharpCharacterMovementComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Components/SkeletalMeshComponent.h"

void USharpCharacterMovementComponent::ApplyAccumulatedForces(float DeltaSeconds)
{
	Super::ApplyAccumulatedForces(DeltaSeconds);
	if (ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(CharacterOwner))
	{
		if (Enemy->Recorder) { Enemy->Recorder->ObserveForceApplication(); }
	}
}

void USharpCharacterMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation,
	const FVector& NewLocation, const FQuat& NewRotation)
{
	ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(CharacterOwner);
	const FVector OldMesh = Enemy && Enemy->GetMesh() ? Enemy->GetMesh()->GetComponentLocation() : FVector::ZeroVector;
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
	if (Enemy && Enemy->Recorder && Enemy->VisualPredictor)
	{
		Enemy->Recorder->RecordDiagnostic(TEXT("SmoothCorrection"), 0, Enemy->GetReplicatedServerLastTransformUpdateTimeStamp(),
			(NewLocation - OldLocation).Size(), 0, TEXT("SuperPreserved"));
		Enemy->VisualPredictor->NotifyCorrection(OldMesh);
	}
}

void USharpCharacterMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	const ASharpEnemyCharacter* Enemy = Cast<ASharpEnemyCharacter>(CharacterOwner);
	if (!Enemy || !Enemy->VisualPredictor || !Enemy->VisualPredictor->OwnsPresentation()) { Super::SmoothClientPosition(DeltaSeconds); }
}
