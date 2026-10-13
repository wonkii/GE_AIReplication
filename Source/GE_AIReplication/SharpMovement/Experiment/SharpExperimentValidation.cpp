#include "SharpExperimentValidation.h"
#include "SharpExperimentManager.h"
#include "SharpExperimentSettings.h"
#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Enemy/SharpEnemyAIController.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Scenario/SharpEnemyPlacement.h"
#include "../Scenario/SharpSplinePath.h"
#include "../Scenario/SharpImpulseVolume.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

bool FSharpExperimentValidation::ValidateTarget(const ASharpExperimentManager& Manager,
    const ASharpSplinePath* Path, FString& OutReason)
{
    OutReason.Empty();
    if (!Manager.Settings || !Manager.Settings->IsValidConfiguration(OutReason))
    {
        if (OutReason.IsEmpty()) { OutReason = TEXT("Assign a valid Settings asset"); } return false;
    }
    for (TActorIterator<ASharpExperimentManager> It(Manager.GetWorld()); It; ++It)
    {
        if (*It != &Manager) { OutReason = TEXT("Only one ExperimentManager is supported per world"); return false; }
    }
    if (!IsValid(Path) || Path->GetWorld() != Manager.GetWorld() || !Manager.IsRegistered(Path) || !Path->ValidatePath(OutReason))
    {
        if (OutReason.IsEmpty()) { OutReason = TEXT("Assign a valid registered Spline from this world"); } return false;
    }
    const ASharpEnemyCharacter* Enemy = Path->AssignedEnemy;
    if (!IsValid(Enemy) || Enemy->GetWorld() != Manager.GetWorld() || Enemy->ExperimentActorId <= 0 ||
        !Enemy->SplineFollower || !Enemy->Experiment || !Enemy->UpdatePolicy || !Enemy->GetIsReplicated() || !Enemy->IsReplicatingMovement())
    {
        OutReason = FString::Printf(TEXT("Path %s needs a replicated SharpEnemy with ActorId > 0"), *Path->GetName()); return false;
    }
    int32 Registrations = 0;
    for (const ASharpSplinePath* Registered : Manager.Paths) { if (Registered == Path) { ++Registrations; } }
    if (Registrations != 1) { OutReason = TEXT("A Spline is registered more than once"); return false; }
    for (TActorIterator<ASharpSplinePath> It(Manager.GetWorld()); It; ++It)
    {
        if (*It != Path && It->PathId == Path->PathId)
        {
            OutReason = FString::Printf(TEXT("Duplicate PathId '%s': %s and %s. Assign a unique PathId to each Spline."),
                *Path->PathId.ToString(), *Path->GetName(), *It->GetName()); return false;
        }
        if (*It != Path && It->AssignedEnemy == Enemy)
        {
            OutReason = FString::Printf(TEXT("Duplicate Enemy assignment (ActorId=%d): %s and %s. Assign a different Enemy to each Spline."),
                Enemy->ExperimentActorId, *Path->GetName(), *It->GetName()); return false;
        }
    }
    for (TActorIterator<ASharpEnemyCharacter> It(Manager.GetWorld()); It; ++It)
    {
        if (*It != Enemy && It->ExperimentActorId == Enemy->ExperimentActorId)
        {
            OutReason = FString::Printf(TEXT("Duplicate Enemy ActorId %d"), Enemy->ExperimentActorId); return false;
        }
    }
    return Enemy->SplineFollower->Settings.IsValid(OutReason);
}

bool FSharpExperimentValidation::ValidateRegistry(const ASharpExperimentManager& Manager, FString& OutReason)
{
    if (Manager.Paths.IsEmpty()) { OutReason = TEXT("Assign at least one Spline in Manager.Paths"); return false; }
    for (const ASharpSplinePath* Path : Manager.Paths)
    {
        if (!ValidateTarget(Manager, Path, OutReason)) { return false; }
    }
    OutReason.Empty(); return true;
}

bool FSharpExperimentValidation::ValidateTargetReadiness(const ASharpSplinePath* Path, FString& OutReason)
{
    if (!IsValid(Path) || !IsValid(Path->AssignedEnemy) || !Path->AssignedEnemy->HasActorBegunPlay() ||
        !Path->AssignedEnemy->HasAuthority() || !Path->AssignedEnemy->Experiment || !Path->AssignedEnemy->Experiment->IsInitialized() ||
        !Cast<ASharpEnemyAIController>(Path->AssignedEnemy->GetController()))
    {
        OutReason = FString::Printf(TEXT("Waiting for BeginPlay and SharpEnemyAIController possession: %s"), *GetNameSafe(Path)); return false;
    }
    OutReason.Empty(); return true;
}

bool FSharpExperimentValidation::ValidateReadiness(const ASharpExperimentManager& Manager, FString& OutReason)
{
    for (const ASharpSplinePath* Path : Manager.Paths)
    {
        if (!ValidateTargetReadiness(Path, OutReason)) { return false; }
    }
    OutReason.Empty(); return true;
}

bool FSharpExperimentValidation::BuildStartTransforms(const ASharpExperimentManager& Manager,
	const TArray<ASharpSplinePath*>& Targets, TArray<FTransform>& OutTransforms, FString& OutReason)
{
	OutTransforms.Reset();
	TArray<AActor*> IgnoreActors;
	for (ASharpSplinePath* Path : Targets) { IgnoreActors.Add(Path->AssignedEnemy); }
	for (ASharpSplinePath* Path : Targets)
	{
		FTransform Transform;
		if (!FSharpEnemyPlacement::FindStart(*Path->AssignedEnemy, *Path, IgnoreActors, Transform, OutReason))
		{
			OutReason = Path->GetName() + TEXT(": ") + OutReason; return false;
		}
		OutTransforms.Add(Transform);
		TSet<FName> VolumeIds;
		for (TActorIterator<ASharpImpulseVolume> It(Manager.GetWorld()); It; ++It)
		{
			if (!It->AppliesTo(&Manager)) { continue; }
			if (VolumeIds.Contains(It->VolumeId)) { OutReason = TEXT("Duplicate Impulse VolumeId"); return false; }
			VolumeIds.Add(It->VolumeId);
			if (!It->ValidateStart(*Path->AssignedEnemy, Transform, OutReason)) { return false; }
		}
	}
	// Group preflight ignores current positions of moving participants, but must
	// reject starts whose capsules would overlap after the group is repositioned.
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		for (int32 J = I + 1; J < Targets.Num(); ++J)
		{
			const UCapsuleComponent* A = Targets[I]->AssignedEnemy->GetCapsuleComponent();
			const UCapsuleComponent* B = Targets[J]->AssignedEnemy->GetCapsuleComponent();
			if (A->GetCollisionResponseToChannel(B->GetCollisionObjectType()) != ECR_Block ||
				B->GetCollisionResponseToChannel(A->GetCollisionObjectType()) != ECR_Block) { continue; }
			const float AR = A->GetScaledCapsuleRadius(), BR = B->GetScaledCapsuleRadius();
			const float AH = A->GetScaledCapsuleHalfHeight() - AR, BH = B->GetScaledCapsuleHalfHeight() - BR;
			const FVector PA = OutTransforms[I].GetLocation(), PB = OutTransforms[J].GetLocation();
			const double VerticalGap = FMath::Max(0.0, FMath::Abs(PA.Z - PB.Z) - AH - BH);
			if (FVector::DistSquared2D(PA, PB) + VerticalGap * VerticalGap < FMath::Square(AR + BR))
			{
				OutReason = FString::Printf(TEXT("Start capsules overlap: %s and %s"), *Targets[I]->GetName(), *Targets[J]->GetName()); return false;
			}
		}
	}
	OutReason.Empty(); return true;
}
