"""Static source-wiring checks. These do NOT replace UnrealHeaderTool or PIE tests."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "Source/GE_AIReplication/SharpMovement"

def read(relative):
    file = ROOT / relative
    assert file.is_file(), f"Missing {relative}"
    return file.read_text(encoding="utf-8")

def has(relative, *snippets):
    text = read(relative)
    for snippet in snippets:
        assert snippet in text, f"Missing {snippet!r} in {relative}"


def main():
    has("Movement/SharpRandomWalkTypes.h", "MinimumStraightDistance = 300.0f", "MaximumStraightDistance = 800.0f", "TargetSpeed = 300.0f", "MinimumTurnAngle = 25.0f", "MaximumTurnAngle = 135.0f", "BaseSeed = 12345")
    has("Movement/SharpRandomWalkerComponent.cpp", "Enemy->HasAuthority()", "Random.Initialize(ResolvedSeed)", "Random.RandRange(0, 1)", "Random.FRandRange(PreparedSettings.MinimumTurnAngle", "Enemy->AddMovementInput(Forward)", "Phase = ESharpRandomWalkPhase::Turning", "Enemy->SetActorRotation")
    has("Enemy/SharpEnemyCharacter.cpp", "CreateDefaultSubobject<USharpSplineFollowerComponent>", "CreateDefaultSubobject<USharpRandomWalkerComponent>", "DOREPLIFETIME(ASharpEnemyCharacter, ExperimentMovementSource)")
    has("Experiment/SharpEnemyExperimentComponent.cpp", "Enemy->RandomWalker->Prepare", "Enemy->RandomWalker->CanStart", "Enemy->RandomWalker->Start(", "Enemy->RandomWalker->Stop(", "Enemy->RandomWalker->Suspend(", "Enemy->RandomWalker->Resume(", "Enemy->RandomWalker->Fail", "OnRandomWalkStateChanged")
    has("Experiment/SharpExperimentManagerControl.cpp", "RandomWalkEnemies", "BuildGroupStartTransforms", "RandomWalkInitialTransforms")
    has("Experiment/SharpExperimentValidation.cpp", "ValidateRandomTarget", "ValidateRandomReadiness", "FindRandomStart", "Start capsules overlap")
    has("Measurement/SharpMovementRecorderComponent.cpp", 'TEXT("movement_commands.csv")')
    has("Measurement/SharpMovementRecorderManifest.cpp", 'TEXT("MovementSource")', 'TEXT("RandomWalkResolvedSeed")')
    has("Scenario/SharpImpulseVolume.cpp", "Path ? Path->GetDirection", "Enemy->GetVelocity()")
    # The Unreal editor's binary assets and original Spline code were deliberately left intact.
    assert (ROOT.parents[2] / "Content/SharpMovementExperiment/Blueprints/BP_SharpEnemy.uasset").is_file()
    assert (ROOT / "Movement/SharpSplineFollowerComponent.cpp").is_file()
    print("PASS: RandomWalk code wiring, defaults and preserved Spline entry points")
    print("NOTE: Unreal Engine 5.8 compile and PIE testing remain required")


if __name__ == "__main__":
    main()
