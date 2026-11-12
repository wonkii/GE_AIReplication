"""Source-level invariants for the experimental event transport.
This does NOT compile Unreal code or replace PIE client/server validation.
Run: python Tools/check_event_transport_patch.py
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1] / 'Source' / 'GE_AIReplication' / 'SharpMovement'
NET = Path(__file__).resolve().parents[1] / 'Source' / 'AIReplicationNet' / 'Public'


def read(part):
    return (ROOT / part).read_text(encoding='utf-8')


def balanced(code):
    # Ignore comments / text literals: this is not a C++ parser.
    code = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', code, flags=re.S)
    for op, cl in (('{', '}'), ('(', ')'), ('[', ']')):
        n = 0
        for c in code:
            if c == op:
                n += 1
            if c == cl:
                n -= 1
            assert n >= 0, f'Unbalanced {op}{cl}'
        assert n == 0, f'Unclosed {op}{cl}'

checks = {
    'Server disables only movement replication for event policies':
        'OwnerActor->SetReplicateMovement(false)' in read('Net/SharpUpdatePolicy.cpp') and
        'OwnerActor->SetReplicateMovement(bSavedReplicateMovement)' in read('Net/SharpUpdatePolicy.cpp'),
    'Baseline modes remain present':
        all(f'ESharpExperimentMode::{mode}' in read('Net/SharpUpdatePolicy.cpp')
            for mode in ('Default', 'Adaptive', 'SingleShot', 'Burst')) and
        'Mode=AlwaysHigh' in read('Net/SharpUpdatePolicy.cpp'),
    'A startup state anchor is published':
        'PublishStateKeyframe(true)' in read('Experiment/SharpEnemyExperimentComponent.cpp'),
    'Recovery states use a configurable interval':
        'EventRecoveryIntervalSeconds' in read('Net/SharpSnapshotChannelComponent.cpp') and
        'PublishStateKeyframe(false)' in read('Net/SharpSnapshotChannelComponent.cpp'),
    'One sequence stream for events and keyframes':
        'BuildSnapshot(Observed, Event)' in read('Detector/SharpMovementDetectorComponent.cpp') and
        'BuildSnapshot(Sample(), State)' in read('Detector/SharpMovementDetectorComponent.cpp'),
    'Client CMC tick is switched off and restored':
        'Movement->SetComponentTickEnabled(false)' in read('Net/SharpSnapshotChannelComponent.cpp') and
        'Movement->SetComponentTickEnabled(bSavedCMCTickEnabled)' in read('Net/SharpSnapshotChannelComponent.cpp'),
    'EventOnly moves capsule without extrapolation':
        'SetActorLocationAndRotation(Snapshot.StartPosition' in read('Net/SharpSnapshotChannelComponent.cpp'),
    'EventPredictor moves capsule by extrapolation':
        'Extrapolate(Anchor, ServerNow, Horizon)' in read('Net/SharpSnapshotChannelComponent.cpp') and
        'SetActorLocationAndRotation(Target' in read('Net/SharpSnapshotChannelComponent.cpp'),
    'Recorder counts event transport receipts':
        'ObserveEventReceipt(' in read('Measurement/SharpMovementRecorderComponent.cpp') and
        'ObserveEventReceipt(' in read('Net/SharpSnapshotChannelComponent.cpp'),
    'Old mesh predictor no longer receives new event snapshots':
        'VisualPredictor->QueueSnapshot' not in read('Net/SharpSnapshotChannelComponent.cpp'),
    'Diagnostic synthetic events cannot collide with live sequence':
        'EventModeSyntheticTestDisabled' in read('Net/SharpPolicyDiagnosticActor.cpp'),
    'Event transport has explicit payload kind':
        'ESharpSnapshotKind' in (NET / 'SharpMoveSnapshot.h').read_text(encoding='utf-8'),
}
for cpp in list(ROOT.rglob('*.cpp')) + list(ROOT.rglob('*.h')):
    if ('Net/SharpSnapshotChannelComponent' in str(cpp) or
        'Detector/SharpMovementDetectorComponent' in str(cpp) or
        'Net/SharpUpdatePolicy' in str(cpp)):
        balanced(cpp.read_text(encoding='utf-8'))
for name, good in checks.items():
    print(f'{"PASS" if good else "FAIL"}: {name}')
assert all(checks.values()), 'The event transport patch is missing a required wiring point'
print(f'{len(checks)} static source checks passed; Unreal build and PIE network tests still required.')
