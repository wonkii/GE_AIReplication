"""Static checks for Manager rectangular arena + wall-avoidance patch.

Not a replacement for UnrealHeaderTool, compilation, physics collision or multiplayer PIE.
"""
from pathlib import Path
from math import sqrt

ROOT = Path(__file__).resolve().parents[2] / "Source/GE_AIReplication/SharpMovement"


def check(relative, *requirements):
    content = (ROOT / relative).read_text(encoding="utf-8")
    missing = [r for r in requirements if r not in content]
    assert not missing, f"{relative}: missing {missing}"
    return content


def main():
    manager = check("Experiment/SharpExperimentManager.h",
        "bEnableRandomWalkArena = false", "RandomWalkArenaSizeCm = FVector2D(20000.0f, 20000.0f)",
        "RandomWalkArenaMarginCm = 150.0f", "RandomWalkAvoidanceLookAheadCm = 450.0f",
        "bRandomWalkAvoidWorldStaticWalls = true", "RandomWalkWallProbeIntervalSeconds = 0.15f",
        "AutoSpawnEnemyCount = 64", "AutoSpawnSpacingCm = 1000.0f", "RandomWalkBaseSeed = 12345")
    check("Experiment/SharpExperimentManagerControl.cpp",
        "RandomWalkArenaSizeCm", "RandomWalkArenaMarginCm", "Enemy->RandomWalker->ConfigureArena(",
        "FSharpExperimentValidation::ValidateRegistry")
    check("Experiment/SharpExperimentManagerAutoSpawn.cpp",
        "if (bEnableRandomWalkArena)", "RandomWalkArenaSizeCm.X", "RandomWalkArenaMarginCm",
        "Auto Spawn grid cell", "RandomWalkEnemies.Add(Enemy)")
    check("Movement/SharpRandomWalkerComponent.h",
        "ConfigureArena(bool bEnabled", "IsInitialPositionInsideArena", "BeginAvoidance(",
        "MaxConsecutiveAvoidanceAttempts = 12", "ConsecutiveAvoidanceAttempts")
    walker = check("Movement/SharpRandomWalkerComponent.cpp",
        "IsDirectionAllowed", "IsArenaPositionAllowed", "DetectObstacle", "BeginAvoidance",
        "ECC_WorldStatic", "SweepSingleByObjectType", "AvoidanceRandom.Initialize",
        "Random.Initialize(ResolvedSeed)", "bAvoidanceTurn", "Avoid_", "RecordRandomWalkAvoidance",
        "RandomWalk could not find a safe route", "RandomWalk moving turn could not escape")
    assert "SetActorLocation(" not in walker
    check("Measurement/SharpMovementRecorderComponent.h", "RecordRandomWalkAvoidance")
    check("Measurement/SharpMovementRecorderComponent.cpp",
        'TEXT("avoidance_events.csv")', 'TEXT("movement_commands.csv")',
        "SegmentIndex,Kind,Attempt,SignedTurnDeg,TargetYaw,PosX,PosY,PosZ")
    check("Movement/SharpSplineFollowerComponent.cpp", "AddMovementInput")
    check("Experiment/SharpEnemyExperimentComponent.cpp", "Enemy->RandomWalker->Prepare", "Enemy->SplineFollower->Prepare")

    # 64 actors on a centred 8x8 10m grid fit into a 200m arena, even
    # after reserving a generous nominal capsule radius and 1.5m inset.
    count, spacing = 64, 1000
    width = height = 20000
    margin, nominal_radius = 150, 50
    cols = int(sqrt(count))
    rows = (count + cols - 1) // cols
    assert cols == rows == 8
    points = [((i % cols - 3.5) * spacing, (i // cols - 3.5) * spacing) for i in range(count)]
    assert len(set(points)) == count
    assert all(abs(x) < width / 2 - margin - nominal_radius and
               abs(y) < height / 2 - margin - nominal_radius for x, y in points)
    assert "TArray<TObjectPtr<ASharpSplinePath>> Paths;" in manager
    print("PASS: opt-in arena (200m x 200m), bounds + WorldStatic detection and recovery wiring")
    print("PASS: 64 auto-spawn cells fit in default arena; old Spline/Recorder entry points retained")
    print("NOTE: UE5.8 UHT, compile, collision and listen-server PIE still need in-editor testing")

if __name__ == "__main__":
    main()
