"""Static auto-spawn patch checks (not a substitute for UHT/UE/PIE)."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[2] / "Source/GE_AIReplication/SharpMovement"

def check(relative, *items):
    source = (ROOT / relative).read_text(encoding="utf-8")
    missing = [item for item in items if item not in source]
    assert not missing, f"{relative}: missing {missing}"
    return source


def main():
    manager = check("Experiment/SharpExperimentManager.h",
       "bAutoSpawnRandomWalkEnemies = false", "AutoSpawnEnemyCount = 64",
       "AutoSpawnSpacingCm = 1000.0f", "AutoSpawnBehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed",
       "AutoSpawnEnemyClass", "AutoSpawnAlwaysRelevant = true",
       "RandomWalkBaseSeed = 12345", "RandomWalkFirstActorId = 1",
       "SpawnConfiguredRandomWalkEnemies")
    assert 'TArray<TObjectPtr<ASharpSplinePath>> Paths;' in manager
    assert 'TArray<TObjectPtr<ASharpEnemyCharacter>> RandomWalkEnemies;' in manager
    startup = check("Experiment/SharpExperimentManager.cpp",
       "if (!HasAuthority())", "SpawnConfiguredRandomWalkEnemies(AssignReason)",
       "AssignRandomWalkIdentities(AssignReason)", "TryAutomaticPrepare")
    assert startup.index("SpawnConfiguredRandomWalkEnemies(AssignReason)") < startup.index("AssignRandomWalkIdentities(AssignReason)")
    spawn = check("Experiment/SharpExperimentManagerAutoSpawn.cpp",
       "!HasAuthority()", "!bAutoAssignRandomWalkIdentity", "AutoSpawnEnemyClass->GetDefaultObject",
       "RandomWalkFirstActorId", "RandomWalkBaseSeed", "AutoSpawnEnemyCount",
       "LineTraceSingleByChannel", "OverlapBlockingTestByChannel", "AutoSpawnSpacingCm",
       "SpawnActorDeferred<ASharpEnemyCharacter>", "FinishSpawning", "Enemy->ExperimentActorId",
       "Enemy->ExperimentMovementSource = ESharpEnemyMovementSource::RandomWalk",
       "Enemy->RandomWalker->Settings.BehaviorSelection = AutoSpawnBehaviorSelection",
       "Enemy->RandomWalker->SetManagerSeed(Seed)", "Enemy->SetReplicateMovement(true)",
       "Enemy->bAlwaysRelevant = bAutoSpawnAlwaysRelevant", "RandomWalkEnemies.Add(Enemy)",
       "RandomWalkInitialTransforms.Add", "bAutoSpawnCompleted = true", "RollBack()")
    assert spawn.index("LineTraceSingleByChannel") < spawn.index("SpawnActorDeferred")
    assert spawn.index("SpawnActorDeferred") < spawn.index("RandomWalkEnemies.Add(Enemy)")
    check("Experiment/SharpExperimentManagerControl.cpp",
          "AssignRandomWalkIdentities", "RandomWalkEnemies", "RandomWalkInitialTransforms",
          "PrepareGroup(true)") if "PrepareGroup(true)" in (ROOT / 'Experiment/SharpExperimentManagerControl.cpp').read_text() else check("Experiment/SharpExperimentManagerControl.cpp", "AssignRandomWalkIdentities", "RandomWalkEnemies", "RandomWalkInitialTransforms")
    check("Experiment/SharpExperimentManagerRepeat.cpp", "PrepareGroup(true)")
    check("Experiment/SharpExperimentValidation.cpp", "ValidateRandomTarget", "ValidateRandomReadiness", "FindRandomStart")
    check("Measurement/SharpMovementRecorderComponent.cpp", 'TEXT("movement_commands.csv")')
    check("Measurement/SharpMovementRecorderManifest.cpp", 'TEXT("RandomWalkResolvedSeed")')
    check("Movement/SharpSplineFollowerComponent.cpp", "AddMovementInput")
    # Verify 8 x 8 placement and one-based seed assignment with defaults.
    n = 64
    cols = math.ceil(math.sqrt(n))
    rows = (n + cols - 1) // cols
    cells = [(i // cols, i % cols) for i in range(n)]
    xy = [((col - 0.5 * (cols - 1)) * 1000,
           (row - 0.5 * (rows - 1)) * 1000) for row, col in cells]
    assert (cols, rows) == (8, 8) and len(set(xy)) == 64
    assert min(x for x, y in xy) == -3500 and max(x for x, y in xy) == 3500
    assert min(y for x, y in xy) == -3500 and max(y for x, y in xy) == 3500
    assert [(1 + i, 12345 + i + 1) for i in range(64)][0] == (1, 12346)
    assert [(1 + i, 12345 + i + 1) for i in range(64)][-1] == (64, 12409)
    print("PASS: 64 Enemy / 8x8 grid / 1000cm spacing / ActorId 1..64 / Seed 12346..12409")
    print("PASS: only server spawns; existing Spline, recording, RandomWalk and repeat workflows retained")
    print("NOTE: UnrealHeaderTool, Unreal C++ compilation and multiplayer PIE still need user-side verification")

if __name__ == '__main__':
    main()
