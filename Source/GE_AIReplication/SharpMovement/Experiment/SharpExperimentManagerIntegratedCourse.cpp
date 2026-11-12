#include "SharpExperimentManager.h"

#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "../Scenario/SharpIntegratedCourseObstacle.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpIntegratedCourse, Log, All);

bool ASharpExperimentManager::SpawnIntegratedCourseObstacles(FString& OutReason)
{
    OutReason.Empty();
    if (!bEnableIntegratedMovementCourse || bIntegratedObstaclesSpawned || RandomWalkEnemies.IsEmpty()) { return true; }
    if (!HasAuthority() || !GetWorld())
    {
        OutReason = TEXT("Integrated course obstacles must be generated on the server");
        return false;
    }
    if (IntegratedObstacleCount < 0 || IntegratedObstacleCount > 32 ||
        !FMath::IsFinite(IntegratedObstacleSizeCm) || IntegratedObstacleSizeCm < 100.0f ||
        !FMath::IsFinite(IntegratedKnockbackPulseIntervalSeconds) || IntegratedKnockbackPulseIntervalSeconds < 0.1f ||
        IntegratedKnockbackActorsPerPulse < 1 || IntegratedKnockbackActorsPerPulse > 64 ||
        !FMath::IsFinite(IntegratedKnockbackHorizontalSpeed) || IntegratedKnockbackHorizontalSpeed <= 0.0f ||
        !FMath::IsFinite(IntegratedKnockbackUpSpeed) || IntegratedKnockbackUpSpeed < 0.0f)
    {
        OutReason = TEXT("Invalid integrated movement course configuration");
        return false;
    }

    // The previous version put obstacles 5500+ cm from the Manager, often
    // beyond the map's floor. An all-failed floor trace then stopped the
    // ENTIRE seven-mode Batch before preparation. Instead try the actual
    // walkable spaces BETWEEN the auto-spawned enemy starting grid cells.
    // Each candidate lies at least spacing/sqrt(2) from its four neighbors.
    TArray<FVector2D> CandidateOffsets;
    if (bAutoSpawnRandomWalkEnemies && AutoSpawnEnemyCount >= 4 && AutoSpawnSpacingCm > 0.0f)
    {
        const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(AutoSpawnEnemyCount)));
        const int32 Rows = (AutoSpawnEnemyCount + Columns - 1) / Columns;
        for (int32 Row = 0; Row < Rows - 1; ++Row)
        {
            for (int32 Column = 0; Column < Columns - 1; ++Column)
            {
                // Same coordinate system and grid origin as AutoSpawn.cpp.
                CandidateOffsets.Emplace(
                    (static_cast<float>(Column) - 0.5f * (Columns - 2)) * AutoSpawnSpacingCm,
                    (static_cast<float>(Row) - 0.5f * (Rows - 2)) * AutoSpawnSpacingCm);
            }
        }
    }
    // The manually populated RandomWalkEnemies case also gets a deterministic
    // set of candidates, rather than depending on auto-spawn being enabled.
    if (CandidateOffsets.IsEmpty())
    {
        for (int32 RadiusIndex = 0; RadiusIndex < 5; ++RadiusIndex)
        {
            const float Radius = 450.0f + RadiusIndex * 300.0f;
            for (int32 DirectionIndex = 0; DirectionIndex < 12; ++DirectionIndex)
            {
                const float Angle = 2.0f * PI * DirectionIndex / 12.0f;
                CandidateOffsets.Emplace(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));
            }
        }
    }
    // Fixed seed: every policy sees identical geometry; spreading the first
    // eight picks across the arena makes the obstacles visible and reachable.
    FRandomStream LayoutStream(47293);
    for (int32 i = CandidateOffsets.Num() - 1; i > 0; --i)
    {
        CandidateOffsets.Swap(i, LayoutStream.RandRange(0, i));
    }

    FCollisionQueryParams Query(SCENE_QUERY_STAT(SharpIntegratedCourseFloor), false);
    Query.AddIgnoredActor(this);
    for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It) { Query.AddIgnoredActor(*It); }
    FCollisionQueryParams OverlapQuery(SCENE_QUERY_STAT(SharpIntegratedCourseObstacleOverlap), false);
    OverlapQuery.AddIgnoredActor(this);

    const FVector Origin = GetActorLocation();
    const float Yaw = GetActorRotation().Yaw;
    const float HalfObstacle = 0.5f * IntegratedObstacleSizeCm;
    const FVector BoxHalfExtent(0.75f * IntegratedObstacleSizeCm,
        0.75f * IntegratedObstacleSizeCm, FMath::Max(1.0f, HalfObstacle - 6.0f));
    const FCollisionShape Occupancy = FCollisionShape::MakeBox(BoxHalfExtent);
    const float LimitX = RandomWalkArenaSizeCm.X * 0.5f - RandomWalkArenaMarginCm;
    const float LimitY = RandomWalkArenaSizeCm.Y * 0.5f - RandomWalkArenaMarginCm;
    int32 MissingFloor = 0;
    int32 Blocked = 0;
    int32 OutsideArena = 0;

    for (const FVector2D& XY : CandidateOffsets)
    {
        if (IntegratedSpawnedObstacleCount >= IntegratedObstacleCount) { break; }
        if (bEnableRandomWalkArena &&
            (FMath::Abs(XY.X) + BoxHalfExtent.X >= LimitX ||
             FMath::Abs(XY.Y) + BoxHalfExtent.Y >= LimitY))
        {
            ++OutsideArena;
            continue;
        }
        const FVector Center = Origin + FVector(XY.X, XY.Y, 0.0f).RotateAngleAxis(Yaw, FVector::UpVector);
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit,
            Center + FVector(0.0f, 0.0f, AutoSpawnFloorTraceHalfHeightCm),
            Center - FVector(0.0f, 0.0f, AutoSpawnFloorTraceHalfHeightCm),
            ECC_Visibility, Query) || Hit.ImpactNormal.Z < 0.65f)
        {
            ++MissingFloor;
            continue;
        }
        // The 3cm gap prevents the blocking overlap preflight from interpreting
        // the legitimate floor contact as an obstacle collision.
        const FVector Position(Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
            Hit.ImpactPoint.Z + HalfObstacle + 3.0f);
        if (GetWorld()->OverlapBlockingTestByChannel(Position, FQuat::Identity,
            ECC_WorldStatic, Occupancy, OverlapQuery))
        {
            ++Blocked;
            continue;
        }
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FTransform Transform(FRotator(0, Yaw, 0), Position,
            FVector(IntegratedObstacleSizeCm / 100.0f,
                IntegratedObstacleSizeCm / 160.0f, IntegratedObstacleSizeCm / 100.0f));
        ASharpIntegratedCourseObstacle* Obstacle = GetWorld()->SpawnActor<ASharpIntegratedCourseObstacle>(
            ASharpIntegratedCourseObstacle::StaticClass(), Transform, Params);
        if (!IsValid(Obstacle))
        {
            ++Blocked;
            continue;
        }
        Obstacle->Tags.AddUnique(TEXT("SharpIntegratedCourseObstacle"));
        IntegratedObstacles.Add(Obstacle);
        ++IntegratedSpawnedObstacleCount;
        UE_LOG(LogSharpIntegratedCourse, Display,
            TEXT("[Sharp][Course] Obstacle %d/%d at %s"),
            IntegratedSpawnedObstacleCount, IntegratedObstacleCount, *Position.ToCompactString());
    }

    bIntegratedObstaclesSpawned = true;
    UE_LOG(LogSharpIntegratedCourse, Display,
        TEXT("[Sharp][Course] Enabled=1 Obstacles=%d/%d Candidates=%d MissingFloor=%d Blocked=%d OutsideArena=%d KnockbackEvery=%.2fs ActorsPerPulse=%d"),
        IntegratedSpawnedObstacleCount, IntegratedObstacleCount, CandidateOffsets.Num(),
        MissingFloor, Blocked, OutsideArena,
        IntegratedKnockbackPulseIntervalSeconds, IntegratedKnockbackActorsPerPulse);
    if (IntegratedSpawnedObstacleCount < IntegratedObstacleCount)
    {
        // A short floor, collision props or missing floor traces are NOT a
        // reason to lose all 7 trials. Keep knockbacks and movement active.
        UE_LOG(LogSharpIntegratedCourse, Warning,
            TEXT("[Sharp][Course] Only %d/%d obstacles fit; Batch will CONTINUE. Check the map floor and Manager location."),
            IntegratedSpawnedObstacleCount, IntegratedObstacleCount);
    }
    return true;
}

void ASharpExperimentManager::ApplyIntegratedKnockbackPulse()
{
    if (!HasAuthority() || !bEnableIntegratedMovementCourse || RandomWalkEnemies.IsEmpty()) { return; }
    const int32 Count = RandomWalkEnemies.Num();
    const int32 TargetCount = FMath::Min(IntegratedKnockbackActorsPerPulse, Count);
    // Restarting a Batch policy resets this index, so each policy sees the
    // exact same actor-ID/pulse schedule (although physics can still diverge).
    const int32 StartIndex = (IntegratedKnockbackPulseIndex * TargetCount) % Count;
    for (int32 LocalIndex = 0; LocalIndex < TargetCount; ++LocalIndex)
    {
        const int32 TargetIndex = (StartIndex + LocalIndex) % Count;
        ASharpEnemyCharacter* Enemy = RandomWalkEnemies[TargetIndex];
        if (!IsValid(Enemy) || !Enemy->Experiment || !Enemy->Experiment->IsExecuting() ||
            !Enemy->GetCharacterMovement() || !Enemy->Recorder) { continue; }

        // Velocity change gives a reproducible, visible hit-like displacement.
        // Alternate left/right per pulse and actor; never derive randomness
        // from the active network policy.
        const float Sign = ((IntegratedKnockbackPulseIndex + TargetIndex) % 2 == 0) ? 1.0f : -1.0f;
        const FVector Side = Enemy->GetActorRightVector().GetSafeNormal2D();
        const FVector Impulse = Side * (Sign * IntegratedKnockbackHorizontalSpeed) +
            FVector::UpVector * IntegratedKnockbackUpSpeed;
        const uint32 EventId = Enemy->Recorder->BeginImpulse(FName(TEXT("IntegratedKnockback")), Impulse, true, true);
        Enemy->GetCharacterMovement()->AddImpulse(Impulse, true);
        Enemy->Recorder->CompleteImpulse(EventId);
    }
    ++IntegratedKnockbackPulseIndex;
}

void ASharpExperimentManager::StopIntegratedKnockback()
{
    if (GetWorld()) { GetWorldTimerManager().ClearTimer(IntegratedKnockbackTimer); }
}
