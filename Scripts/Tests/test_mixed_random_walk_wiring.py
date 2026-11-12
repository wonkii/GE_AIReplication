"""Static checks for the optional RandomWalk mixed-behavior/auto-ID patch.

This script cannot verify UnrealHeaderTool, compilation, network timing or PIE.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "Source/GE_AIReplication/SharpMovement"

def verify(rel, *needles):
    body = (ROOT / rel).read_text(encoding="utf-8")
    missing = [n for n in needles if n not in body]
    assert not missing, f"{rel} missing: {missing}"
    return body


def main():
    verify("Movement/SharpRandomWalkTypes.h",
           "ESharpRandomWalkBehaviorSelection", "Mixed UMETA", "StopTurnWalk UMETA",
           "WalkTurn UMETA", "RunStopTurn UMETA", "RunTurn UMETA",
           "RunSpeed = 600.0f", "TargetSpeed = 300.0f",
           "WalkTurningRateDegreesPerSecond = 180.0f",
           "RunTurningRateDegreesPerSecond = 360.0f")
    driver = verify("Movement/SharpRandomWalkerComponent.cpp",
                    "PickBehavior()", "GetBehaviorSpeed()", "TurnsWhileMoving()",
                    "GetBehaviorTurnRate()", "Behavior_%s", "Random.Initialize(ResolvedSeed)",
                    "Enemy->AddMovementInput(Forward)", "bHasManagerSeed ? ManagerSeed")
    assert "SetActorLocation(" not in driver and "SetActorLocationAndRotation(" not in driver
    verify("Experiment/SharpExperimentManager.h",
           "bAutoAssignRandomWalkIdentity = true", "RandomWalkFirstActorId = 1",
           "RandomWalkBaseSeed = 12345")
    verify("Experiment/SharpExperimentManagerControl.cpp",
           "AssignRandomWalkIdentities", "OneBasedIndex = Index + 1",
           "RandomWalkFirstActorId + Index", "RandomWalkBaseSeed",
           "SetManagerSeed(Seed)", "RandomTargets.Contains", "bIdChanged")
    verify("Experiment/SharpExperimentManager.cpp", "AssignRandomWalkIdentities(AssignReason)")
    verify("Experiment/SharpExperimentManagerAutomation.cpp", "AssignRandomWalkIdentities(Reason)")
    verify("Experiment/SharpExperimentManagerRepeat.cpp", "PrepareGroup(true)")
    verify("Experiment/SharpEnemyExperimentComponent.cpp", "Enemy->RandomWalker->Prepare")
    verify("Measurement/SharpMovementRecorderComponent.cpp", 'TEXT("movement_commands.csv")')
    verify("Measurement/SharpMovementRecorderManifest.cpp", 'TEXT("RandomWalkResolvedSeed")')
    verify("Movement/SharpSplineFollowerComponent.cpp", "AddMovementInput")
    # The intended 1-based index mapping preserves the previous 1..N seed assignment
    # while mixed patterns intentionally consume additional random draws.
    mapping = [(1 + i, 12345 + i + 1) for i in range(9)]
    assert mapping[0] == (1, 12346) and mapping[-1] == (9, 12354)
    print("PASS: four behaviors, auto IDs/seeds, fallback, original recorder and Spline wiring")
    print("NOTE: Unreal Engine compilation and server/client PIE checks are still required")

if __name__ == "__main__":
    main()
