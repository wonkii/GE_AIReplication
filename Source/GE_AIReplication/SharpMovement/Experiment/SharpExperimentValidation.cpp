#include "SharpExperimentValidation.h"
#include "SharpExperimentManager.h"
#include "SharpExperimentSettings.h"
#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Enemy/SharpEnemyAIController.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Movement/SharpRandomWalkerComponent.h"
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
        !Enemy->SplineFollower || !Enemy->Experiment || !Enemy->UpdatePolicy || !Enemy->GetIsReplicated())
    {
        OutReason = FString::Printf(TEXT("Path %s needs a replicated SharpEnemy with ActorId > 0"), *Path->GetName()); return false;
    }
    if (Enemy->ExperimentMovementSource != ESharpEnemyMovementSource::Spline)
    {
        OutReason = TEXT("A Spline-assigned Enemy must select ExperimentMovementSource=Spline"); return false;
    }
    if (!Enemy->IsReplicatingMovement() && !Manager.IsServerOnlyMovementDemo())
    {
        OutReason = FString::Printf(TEXT("Path %s requires Replicate Movement"), *Path->GetName()); return false;
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

bool FSharpExperimentValidation::ValidateRandomTarget(const ASharpExperimentManager& Manager,
    const ASharpEnemyCharacter* Enemy, FString& OutReason)
{
    OutReason.Empty();
    if (!Manager.Settings || !Manager.Settings->IsValidConfiguration(OutReason))
    {
        if (OutReason.IsEmpty()) { OutReason = TEXT("Assign valid experiment Settings"); } return false;
    }
    for (TActorIterator<ASharpExperimentManager> It(Manager.GetWorld()); It; ++It)
    {
        if (*It != &Manager) { OutReason = TEXT("Only one ExperimentManager is supported per world"); return false; }
    }
    if (!IsValid(Enemy) || Enemy->GetWorld() != Manager.GetWorld() ||
        !Manager.IsRegisteredRandom(Enemy) || Enemy->ExperimentActorId <= 0 ||
        Enemy->ExperimentMovementSource != ESharpEnemyMovementSource::RandomWalk ||
        !Enemy->RandomWalker || !Enemy->Experiment || !Enemy->UpdatePolicy || !Enemy->GetIsReplicated())
    {
        OutReason = TEXT("Each RandomWalkEnemies entry needs a replicated SharpEnemy, ActorId > 0, and RandomWalk movement source");
        return false;
    }
    if (!Enemy->IsReplicatingMovement() && !Manager.IsServerOnlyMovementDemo())
    {
        OutReason = TEXT("RandomWalk Enemy requires Replicate Movement"); return false;
    }
    for (TActorIterator<ASharpEnemyCharacter> It(Manager.GetWorld()); It; ++It)
    {
        if (*It != Enemy && It->ExperimentActorId == Enemy->ExperimentActorId)
        {
            OutReason = FString::Printf(TEXT("Duplicate Enemy ActorId %d"), Enemy->ExperimentActorId); return false;
        }
    }
    return Enemy->RandomWalker->Settings.IsValid(OutReason);
}

bool FSharpExperimentValidation::ValidateRegistry(const ASharpExperimentManager& Manager, FString& OutReason)
{
    if (Manager.Paths.IsEmpty() && Manager.RandomWalkEnemies.IsEmpty())
    {
        OutReason = TEXT("Assign at least one Spline to Paths or Enemy to RandomWalkEnemies"); return false;
    }
    TSet<const ASharpEnemyCharacter*> Bound;
    for (const ASharpSplinePath* Path : Manager.Paths)
    {
        if (!ValidateTarget(Manager, Path, OutReason)) { return false; }
        if (Bound.Contains(Path->AssignedEnemy.Get())) { OutReason = TEXT("Enemy appears in multiple movement registrations"); return false; }
        Bound.Add(Path->AssignedEnemy.Get());
    }
    for (const ASharpEnemyCharacter* Enemy : Manager.RandomWalkEnemies)
    {
        if (!ValidateRandomTarget(Manager, Enemy, OutReason)) { return false; }
        if (Bound.Contains(Enemy)) { OutReason = TEXT("Enemy appears twice in Paths/RandomWalkEnemies"); return false; }
        Bound.Add(Enemy);
        for (TActorIterator<ASharpSplinePath> It(Manager.GetWorld()); It; ++It)
        {
            if (It->AssignedEnemy == Enemy)
            {
                OutReason = TEXT("RandomWalk Enemy is also assigned to a Spline; clear its Spline AssignedEnemy"); return false;
            }
        }
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

bool FSharpExperimentValidation::ValidateRandomReadiness(const ASharpEnemyCharacter* Enemy, FString& OutReason)
{
    if (!IsValid(Enemy) || !Enemy->HasActorBegunPlay() || !Enemy->HasAuthority() ||
        !Enemy->Experiment || !Enemy->Experiment->IsInitialized() ||
        !Cast<ASharpEnemyAIController>(Enemy->GetController()))
    {
        OutReason = FString::Printf(TEXT("Waiting for RandomWalk Enemy BeginPlay and SharpEnemyAIController possession: %s"), *GetNameSafe(Enemy));
        return false;
    }
    OutReason.Empty(); return true;
}

bool FSharpExperimentValidation::ValidateReadiness(const ASharpExperimentManager& Manager, FString& OutReason)
{
    for (const ASharpSplinePath* Path : Manager.Paths)
    {
        if (!ValidateTargetReadiness(Path, OutReason)) { return false; }
    }
    for (const ASharpEnemyCharacter* Enemy : Manager.RandomWalkEnemies)
    {
        if (!ValidateRandomReadiness(Enemy, OutReason)) { return false; }
    }
    OutReason.Empty(); return true;
}

bool FSharpExperimentValidation::BuildStartTransforms(const ASharpExperimentManager& Manager,
    const TArray<ASharpSplinePath*>& Targets, TArray<FTransform>& OutTransforms, FString& OutReason)
{
    TArray<ASharpEnemyCharacter*> Enemies;
    Enemies.Reserve(Targets.Num());
    for (ASharpSplinePath* Path : Targets) { Enemies.Add(IsValid(Path) ? Path->AssignedEnemy.Get() : nullptr); }
    return BuildGroupStartTransforms(Manager, Enemies, Targets, OutTransforms, OutReason);
}

bool FSharpExperimentValidation::BuildGroupStartTransforms(const ASharpExperimentManager& Manager,
    const TArray<ASharpEnemyCharacter*>& Enemies, const TArray<ASharpSplinePath*>& Paths,
    TArray<FTransform>& OutTransforms, FString& OutReason)
{
    OutTransforms.Reset();
    if (Enemies.Num() != Paths.Num() || Enemies.IsEmpty())
    {
        OutReason = TEXT("Movement registration/target array mismatch"); return false;
    }
    TArray<AActor*> IgnoreActors;
    for (ASharpEnemyCharacter* Enemy : Enemies) { IgnoreActors.Add(Enemy); }
    for (int32 I = 0; I < Enemies.Num(); ++I)
    {
        ASharpEnemyCharacter* Enemy = Enemies[I];
        ASharpSplinePath* Path = Paths[I];
        if (!IsValid(Enemy)) { OutReason = TEXT("Invalid Enemy registration"); return false; }
        FTransform Transform;
        if (Path)
        {
            if (!FSharpEnemyPlacement::FindStart(*Enemy, *Path, IgnoreActors, Transform, OutReason))
            {
                OutReason = Path->GetName() + TEXT(": ") + OutReason; return false;
            }
        }
        else if (!FSharpEnemyPlacement::FindRandomStart(*Enemy,
            Manager.GetRandomWalkStartTransform(Enemy), IgnoreActors, Transform, OutReason))
        {
            OutReason = Enemy->GetName() + TEXT(": ") + OutReason; return false;
        }
        OutTransforms.Add(Transform);
        TSet<FName> VolumeIds;
        for (TActorIterator<ASharpImpulseVolume> It(Manager.GetWorld()); It; ++It)
        {
            if (!It->AppliesTo(&Manager)) { continue; }
            if (VolumeIds.Contains(It->VolumeId)) { OutReason = TEXT("Duplicate Impulse VolumeId"); return false; }
            VolumeIds.Add(It->VolumeId);
            if (!It->ValidateStart(*Enemy, Transform, OutReason)) { return false; }
        }
    }
    // Same start-overlap checks as the original Spline-only group preflight.
    for (int32 I = 0; I < Enemies.Num(); ++I)
    {
        for (int32 J = I + 1; J < Enemies.Num(); ++J)
        {
            const UCapsuleComponent* A = Enemies[I]->GetCapsuleComponent();
            const UCapsuleComponent* B = Enemies[J]->GetCapsuleComponent();
            if (A->GetCollisionResponseToChannel(B->GetCollisionObjectType()) != ECR_Block ||
                B->GetCollisionResponseToChannel(A->GetCollisionObjectType()) != ECR_Block) { continue; }
            const float AR = A->GetScaledCapsuleRadius(), BR = B->GetScaledCapsuleRadius();
            const float AH = A->GetScaledCapsuleHalfHeight() - AR, BH = B->GetScaledCapsuleHalfHeight() - BR;
            const FVector PA = OutTransforms[I].GetLocation(), PB = OutTransforms[J].GetLocation();
            const double VerticalGap = FMath::Max(0.0, FMath::Abs(PA.Z - PB.Z) - AH - BH);
            if (FVector::DistSquared2D(PA, PB) + VerticalGap * VerticalGap < FMath::Square(AR + BR))
            {
                OutReason = FString::Printf(TEXT("Start capsules overlap: %s and %s"), *Enemies[I]->GetName(), *Enemies[J]->GetName());
                return false;
            }
        }
    }
    OutReason.Empty(); return true;
}
