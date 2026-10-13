#include "SharpEnemyPlacement.h"
#include "SharpSplinePath.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

bool FSharpEnemyPlacement::FindStart(const ASharpEnemyCharacter& Enemy, const ASharpSplinePath& Path,
	const TArray<AActor*>& IgnoreActors, FTransform& OutTransform, FString& OutReason)
{
	const UWorld* World = Enemy.GetWorld();
	const UCapsuleComponent* Capsule = Enemy.GetCapsuleComponent();
	if (!World || Path.GetWorld() != World || !Capsule || !Enemy.SplineFollower ||
		!Path.ValidatePath(OutReason) || !Enemy.SplineFollower->Settings.IsValid(OutReason))
	{
		if (OutReason.IsEmpty()) { OutReason = TEXT("Enemy, capsule and path must exist in the same world"); }
		return false;
	}
	const bool bReverse = Enemy.SplineFollower->Settings.bReverseDirection;
	const float Distance = bReverse ? Path.GetLength() : 0.0f;
	const FVector Floor = Path.GetLocation(Distance);
	FVector Direction = Path.GetDirection(Distance) * (bReverse ? -1.0f : 1.0f);
	Direction.Z = 0;
	if (!Direction.Normalize()) { OutReason = TEXT("Start tangent has no horizontal direction"); return false; }
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SharpEnemyPlacement), false);
	Query.AddIgnoredActor(&Enemy);
	Query.AddIgnoredActor(&Path);
	Query.AddIgnoredActors(IgnoreActors);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Floor + FVector(0, 0, 50), Floor - FVector(0, 0, 50), ECC_Visibility, Query) ||
		FMath::Abs(Hit.ImpactPoint.Z - Floor.Z) > 10.0 || Hit.ImpactNormal.Z < Enemy.GetCharacterMovement()->GetWalkableFloorZ())
	{
		OutReason = TEXT("Spline start must be within 10cm of a walkable floor that blocks Visibility"); return false;
	}
	const FVector Position = Floor + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + Path.StartHeightOffset);
	const FQuat Rotation = Path.bAlignRotationToPath ? Direction.Rotation().Quaternion() : Enemy.GetActorQuat();
	if (World->OverlapBlockingTestByChannel(Position, Rotation, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
	{
		OutReason = TEXT("The start capsule is blocked; adjust the spline or start height"); return false;
	}
	OutTransform = FTransform(Rotation, Position, Enemy.GetActorScale3D());
	OutReason.Empty(); return true;
}

bool FSharpEnemyPlacement::Place(ASharpEnemyCharacter& Enemy, const FTransform& Transform, FString& OutReason)
{
	if (!Enemy.TeleportTo(Transform.GetLocation(), Transform.Rotator(), false, true))
	{
		OutReason = TEXT("Could not teleport Enemy to its validated start position"); return false;
	}
	OutReason.Empty(); return true;
}
