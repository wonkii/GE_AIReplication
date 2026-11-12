#include "SharpExperimentManager.h"

#include "../Enemy/SharpEnemyAIController.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Movement/SharpRandomWalkerComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpAutoSpawn, Log, All);

bool ASharpExperimentManager::SpawnConfiguredRandomWalkEnemies(FString& OutReason)
{
    OutReason.Empty();
    if (!bAutoSpawnRandomWalkEnemies || bAutoSpawnCompleted) { return true; }
    if (!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld())
    {
        OutReason = TEXT("Auto Spawn requires a running authoritative game world");
        return false;
    }
    if (!bAutoAssignRandomWalkIdentity)
    {
        OutReason = TEXT("Enable bAutoAssignRandomWalkIdentity when using Auto Spawn");
        return false;
    }
    if (AutoSpawnEnemyCount < 1 || AutoSpawnEnemyCount > 1024 ||
        !FMath::IsFinite(AutoSpawnSpacingCm) || AutoSpawnSpacingCm < 100.0f ||
        !FMath::IsFinite(AutoSpawnFloorTraceHalfHeightCm) || AutoSpawnFloorTraceHalfHeightCm < 100.0f ||
        RandomWalkFirstActorId <= 0 ||
        static_cast<int64>(RandomWalkFirstActorId) + RandomWalkEnemies.Num() + AutoSpawnEnemyCount - 1 > MAX_int32 ||
        static_cast<uint8>(AutoSpawnBehaviorSelection) > static_cast<uint8>(ESharpRandomWalkBehaviorSelection::RunTurn))
    {
        OutReason = TEXT("Invalid Auto Spawn count, spacing, trace height, behavior or ActorId range");
        return false;
    }
    if (!AutoSpawnEnemyClass)
    {
        OutReason = TEXT("Choose BP_SharpEnemy in AutoSpawnEnemyClass on the level Manager");
        return false;
    }

    const ASharpEnemyCharacter* TemplateEnemy = AutoSpawnEnemyClass->GetDefaultObject<ASharpEnemyCharacter>();
    const UCapsuleComponent* Capsule = TemplateEnemy ? TemplateEnemy->GetCapsuleComponent() : nullptr;
    const UCharacterMovementComponent* Movement = TemplateEnemy ? TemplateEnemy->GetCharacterMovement() : nullptr;
    if (!Capsule || !Movement || !TemplateEnemy->RandomWalker ||
        !TemplateEnemy->AIControllerClass ||
        !TemplateEnemy->AIControllerClass->IsChildOf(ASharpEnemyAIController::StaticClass()))
    {
        OutReason = TEXT("AutoSpawnEnemyClass requires Capsule, CharacterMovement, RandomWalker and SharpEnemyAIController");
        return false;
    }
    FString SettingsReason;
    FSharpRandomWalkSettings CheckedSettings = TemplateEnemy->RandomWalker->Settings;
    CheckedSettings.BehaviorSelection = AutoSpawnBehaviorSelection;
    if (!CheckedSettings.IsValid(SettingsReason))
    {
        OutReason = TEXT("Auto Spawn Enemy class has invalid RandomWalker settings: ") + SettingsReason;
        return false;
    }

    TSet<const ASharpEnemyCharacter*> ExistingRandomEnemies;
    for (const ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
    {
        if (!IsValid(Enemy) || Enemy->GetWorld() != GetWorld() || ExistingRandomEnemies.Contains(Enemy))
        {
            OutReason = TEXT("RandomWalkEnemies contains an invalid or duplicate manually registered Enemy");
            return false;
        }
        ExistingRandomEnemies.Add(Enemy);
    }

    // Check all IDs before spawning a single actor, including unregistered/Spline enemies.
    const int64 FirstId = RandomWalkFirstActorId;
    const int64 PastLastId = FirstId + RandomWalkEnemies.Num() + AutoSpawnEnemyCount;
    for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It)
    {
        const ASharpEnemyCharacter* Other = *It;
        if (!ExistingRandomEnemies.Contains(Other) &&
            static_cast<int64>(Other->ExperimentActorId) >= FirstId &&
            static_cast<int64>(Other->ExperimentActorId) < PastLastId)
        {
            OutReason = FString::Printf(TEXT("Auto Spawn ActorId range conflicts with %s (Id=%d). Change RandomWalkFirstActorId."),
                *Other->GetName(), Other->ExperimentActorId);
            return false;
        }
    }

    const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
    const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    if (AutoSpawnSpacingCm <= 2.0f * CapsuleRadius + 5.0f)
    {
        OutReason = TEXT("Auto Spawn grid spacing is too small for the selected Enemy capsule");
        return false;
    }

    // Preflight every grid cell before changing the world. The Manager's XY is
    // the grid center; its yaw rotates the grid and supplies the initial facing.
    const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(AutoSpawnEnemyCount)));
    const int32 Rows = (AutoSpawnEnemyCount + Columns - 1) / Columns;
    const FVector Origin = GetActorLocation();
    const float Yaw = GetActorRotation().Yaw;
    const FQuat Facing = FRotator(0.0f, Yaw, 0.0f).Quaternion();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
    FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(SharpAutoSpawnFloor), false);
    FloorQuery.AddIgnoredActor(this);
    // Enemy meshes/capsules must not be mistaken for the walkable floor.
    for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It)
    {
        FloorQuery.AddIgnoredActor(*It);
    }
    FCollisionQueryParams BlockQuery(SCENE_QUERY_STAT(SharpAutoSpawnOverlap), false);
    BlockQuery.AddIgnoredActor(this);
    const FCollisionResponseParams CollisionResponses(Capsule->GetCollisionResponseToChannels());
    TArray<FTransform> SpawnTransforms;
    SpawnTransforms.Reserve(AutoSpawnEnemyCount);

    for (int32 Index = 0; Index < AutoSpawnEnemyCount; ++Index)
    {
        const int32 Row = Index / Columns;
        const int32 Column = Index % Columns;
        const FVector Offset(
            (static_cast<float>(Column) - 0.5f * (Columns - 1)) * AutoSpawnSpacingCm,
            (static_cast<float>(Row) - 0.5f * (Rows - 1)) * AutoSpawnSpacingCm,
            0.0f);
        const FVector Center = Origin + Offset.RotateAngleAxis(Yaw, FVector::UpVector);
        FHitResult FloorHit;
        const FVector TraceTop = Center + FVector(0, 0, AutoSpawnFloorTraceHalfHeightCm);
        const FVector TraceBottom = Center - FVector(0, 0, AutoSpawnFloorTraceHalfHeightCm);
        if (!GetWorld()->LineTraceSingleByChannel(FloorHit, TraceTop, TraceBottom, ECC_Visibility, FloorQuery) ||
            FloorHit.ImpactNormal.Z < Movement->GetWalkableFloorZ())
        {
            OutReason = FString::Printf(TEXT("Auto Spawn grid cell %d (%d,%d): no walkable Visibility-blocking floor"),
                Index + 1, Row, Column);
            return false;
        }
        // Before spawning, reject cells outside the optional arena. The inset
        // reserves both the Manager margin and the Enemy capsule radius.
        if (bEnableRandomWalkArena)
        {
            const FVector Relative = (Center - Origin).RotateAngleAxis(-Yaw, FVector::UpVector);
            const float LimitX = RandomWalkArenaSizeCm.X * 0.5f - RandomWalkArenaMarginCm - CapsuleRadius;
            const float LimitY = RandomWalkArenaSizeCm.Y * 0.5f - RandomWalkArenaMarginCm - CapsuleRadius;
            if (LimitX <= 0.0f || LimitY <= 0.0f ||
                FMath::Abs(Relative.X) > LimitX || FMath::Abs(Relative.Y) > LimitY)
            {
                OutReason = FString::Printf(TEXT("Auto Spawn grid cell %d: outside the RandomWalk arena; reduce count/spacing or increase arena size"), Index + 1);
                return false;
            }
        }
        // A small lift avoids numerical ground-contact overlap; FindRandomStart
        // still accepts it (its allowed floor offset is 30cm).
        const FVector Location = FloorHit.ImpactPoint + FVector(0, 0, CapsuleHalfHeight + 2.0f);
        if (GetWorld()->OverlapBlockingTestByChannel(Location, Facing, Capsule->GetCollisionObjectType(),
            Shape, BlockQuery, CollisionResponses))
        {
            OutReason = FString::Printf(TEXT("Auto Spawn grid cell %d (%d,%d): blocking collision at %s"),
                Index + 1, Row, Column, *Location.ToCompactString());
            return false;
        }
        SpawnTransforms.Emplace(Facing, Location, FVector::OneVector);
    }

    // Deferred spawning assigns identity, movement source and seed BEFORE BeginPlay
    // (Blueprint BeginPlay and recorder logging can therefore read them).
    TArray<ASharpEnemyCharacter*> Spawned;
    Spawned.Reserve(AutoSpawnEnemyCount);
    auto RollBack = [&Spawned]()
    {
        for (ASharpEnemyCharacter* Enemy : Spawned)
        {
            if (IsValid(Enemy)) { Enemy->Destroy(); }
        }
    };
    const int32 ExistingCount = RandomWalkEnemies.Num();
    for (int32 Index = 0; Index < SpawnTransforms.Num(); ++Index)
    {
        ASharpEnemyCharacter* Enemy = GetWorld()->SpawnActorDeferred<ASharpEnemyCharacter>(
            AutoSpawnEnemyClass.Get(), SpawnTransforms[Index], this, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!IsValid(Enemy) || !Enemy->RandomWalker)
        {
            if (IsValid(Enemy)) { Enemy->Destroy(); }
            RollBack();
            OutReason = FString::Printf(TEXT("Auto Spawn deferred creation failed at index %d"), Index + 1);
            return false;
        }
        // IDs use the same overall registry order as AssignRandomWalkIdentities.
        const int32 RegistryIndex = ExistingCount + Index;
        Enemy->ExperimentActorId = RandomWalkFirstActorId + RegistryIndex;
        Enemy->ExperimentMovementSource = ESharpEnemyMovementSource::RandomWalk;
        Enemy->RandomWalker->Settings.BehaviorSelection = AutoSpawnBehaviorSelection;
        if (bEnableIntegratedMovementCourse) { Enemy->RandomWalker->Settings.MaximumTurnAngle = 180.0f; }
        const int32 Seed = static_cast<int32>(static_cast<uint32>(RandomWalkBaseSeed) +
            static_cast<uint32>(RegistryIndex + 1));
        Enemy->RandomWalker->SetManagerSeed(Seed);
        Enemy->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
        Enemy->SetReplicates(true);
        Enemy->SetReplicateMovement(true);
        Enemy->bAlwaysRelevant = bAutoSpawnAlwaysRelevant;
        Enemy->Tags.AddUnique(TEXT("SharpAutoSpawned"));
        Enemy->FinishSpawning(SpawnTransforms[Index]);
        if (!IsValid(Enemy))
        {
            RollBack();
            OutReason = FString::Printf(TEXT("Auto Spawn FinishSpawning failed at index %d"), Index + 1);
            return false;
        }
        if (!Enemy->GetController()) { Enemy->SpawnDefaultController(); }
        Spawned.Add(Enemy);
    }
    for (ASharpEnemyCharacter* Enemy : Spawned)
    {
        RandomWalkEnemies.Add(Enemy);
        RandomWalkInitialTransforms.Add(Enemy, Enemy->GetActorTransform());
    }
    AutoSpawnedEnemyCount = Spawned.Num();
    bAutoSpawnCompleted = true;
    UE_LOG(LogSharpAutoSpawn, Display,
        TEXT("[Sharp][AutoSpawn] Created=%d TotalRandom=%d Grid=%dx%d SpacingCm=%.1f Class=%s FirstId=%d BaseSeed=%d Behavior=%s AlwaysRelevant=%d"),
        AutoSpawnedEnemyCount, RandomWalkEnemies.Num(), Columns, Rows, AutoSpawnSpacingCm,
        *GetNameSafe(AutoSpawnEnemyClass.Get()), RandomWalkFirstActorId, RandomWalkBaseSeed,
        *StaticEnum<ESharpRandomWalkBehaviorSelection>()->GetNameStringByValue(static_cast<int64>(AutoSpawnBehaviorSelection)),
        bAutoSpawnAlwaysRelevant);
    return true;
}
