"""Static assertions for the single mixed movement course (not an Unreal build)."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
src=root/'Source'/'GE_AIReplication'/'SharpMovement'
def read(path):return (src/path).read_text('utf-8')
manager=read('Experiment/SharpExperimentManager.cpp')
header=read('Experiment/SharpExperimentManager.h')
repeat=read('Experiment/SharpExperimentManagerRepeat.cpp')
control=read('Experiment/SharpExperimentManagerAutomation.cpp')
course=read('Experiment/SharpExperimentManagerIntegratedCourse.cpp')
obstacle=read('Scenario/SharpIntegratedCourseObstacle.cpp')
recorder=read('Measurement/SharpMovementRecorderManifest.cpp')
checks={
    'One shared combined course is enabled by default': 'bEnableIntegratedMovementCourse = true' in header,
    'Walking/running/stopping/turning remain mixed': 'AutoSpawnBehaviorSelection = ESharpRandomWalkBehaviorSelection::Mixed' in manager and 'MaximumTurnAngle = 180.0f' in manager,
    'Auto-spawned enemies have maximum 180-degree turns': 'Settings.MaximumTurnAngle = 180.0f' in read('Experiment/SharpExperimentManagerAutoSpawn.cpp'),
    'WorldStatic wall probes and arena enabled': 'bEnableRandomWalkArena = true' in manager and 'bRandomWalkAvoidWorldStaticWalls = true' in manager,
    'Course obstacles are spawned server-side once': 'SpawnIntegratedCourseObstacles(AssignReason)' in manager and 'bIntegratedObstaclesSpawned = true' in course,
    'Course obstacles are physically blocking': 'SetCollisionObjectType(ECC_WorldStatic)' in obstacle and 'SetCollisionResponseToAllChannels(ECR_Block)' in obstacle,
    'Course obstacles are replicated to clients': 'bReplicates = true' in obstacle and 'bAlwaysRelevant = true' in obstacle,
    'No new chase movement logic': 'Chase' not in course,
    'Knockback is initiated only at group start': 'ApplyIntegratedKnockbackPulse' in repeat and 'IntegratedKnockbackPulseIndex = 0' in repeat,
    'Knockback is recorded in existing impulse logs': 'BeginImpulse(FName(TEXT("IntegratedKnockback"))' in course and 'CompleteImpulse(EventId)' in course,
    'Knockback is applied on authoritative movement component': 'GetCharacterMovement()->AddImpulse(Impulse, true)' in course and 'if (!HasAuthority()' in course,
    'Knockback timer is cleared for abort/stop/next trial': 'StopIntegratedKnockback();' in control and 'StopIntegratedKnockback();' in repeat and 'StopIntegratedKnockback();' in manager,
    'Existing seven-mode batch remains unchanged': all(f'ESharpExperimentMode::{k}' in manager for k in ('Default','Adaptive','AlwaysHigh','SingleShot','Burst','EventOnly','EventPredictor')),
    'Course parameters are recorded in manifest': 'SetObjectField(TEXT("IntegratedCourse"), Course)' in recorder,
}
for name,value in checks.items():print(('PASS' if value else 'FAIL')+': '+name)
assert all(checks.values())
files=[
    'Experiment/SharpExperimentManager.h', 'Experiment/SharpExperimentManager.cpp',
    'Experiment/SharpExperimentManagerAutoSpawn.cpp', 'Experiment/SharpExperimentManagerRepeat.cpp',
    'Experiment/SharpExperimentManagerAutomation.cpp', 'Experiment/SharpExperimentManagerIntegratedCourse.cpp',
    'Scenario/SharpIntegratedCourseObstacle.cpp', 'Scenario/SharpIntegratedCourseObstacle.h',
    'Measurement/SharpMovementRecorderManifest.cpp'
]
for file in files:
    text=read(file)
    text=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', text, flags=re.S)
    for a,b in [('{','}'),('(',')'),('[',']')]:
        depth=0
        for c in text:
            if c==a:depth+=1
            if c==b:depth-=1
            assert depth>=0,f'{file}: unexpected {b}'
        assert depth==0,f'{file}: missing {b}'
print(f'{len(checks)} course checks + brace/scope checks passed; Unreal compiler and PIE still required.')
