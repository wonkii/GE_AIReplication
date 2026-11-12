#!/usr/bin/env python3
"""Static wiring checks only; NOT an Unreal Engine/UHT compile or runtime test."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
src=root/'Source/GE_AIReplication/SharpMovement'
def read(path):return (src/path).read_text(encoding='utf-8')

types=read('Experiment/SharpOptimizationTypes.h')
manager_h=read('Experiment/SharpExperimentManager.h')
manager=read('Experiment/SharpExperimentManager.cpp')
impl=read('Experiment/SharpExperimentManagerOptimization.cpp')
control=read('Experiment/SharpExperimentManagerControl.cpp')
enemy_h=read('Enemy/SharpEnemyCharacter.h')
enemy=read('Enemy/SharpEnemyOptimization.cpp')
enemy_main=read('Enemy/SharpEnemyCharacter.cpp')
recorder=read('Measurement/SharpMovementRecorderComponent.cpp')
predictor=read('Prediction/SharpVisualPredictorComponent.cpp')
manifest=read('Measurement/SharpMovementRecorderManifest.cpp')
for k in ['Baseline','ResearchOptimized','Scalability']:
 assert k in types
assert 'OptimizationProfile = ESharpOptimizationProfile::Baseline' in manager_h
assert 'SharpOptimizationProfile=' in impl
assert 'ResolvedProfile == ESharpOptimizationProfile::Scalability && bScalabilityEnableDistanceCulling' in impl
assert 'bOverrideDistanceRelevancy = true' in impl
assert 'OptimizationRuntime.bSuppressDiagnosticLogging = true' in impl
assert 'DOREPLIFETIME(ASharpExperimentManager, OptimizationRuntime);' in manager
assert 'DOREPLIFETIME(ASharpEnemyCharacter, OptimizationRuntime);' in enemy_main
assert 'OnRep_OptimizationRuntime()' in enemy and 'OnRep_OptimizationRuntime()' in impl
assert 'ApplyOptimizationToRegisteredEnemies();' in manager
assert 'Target->ApplyOptimizationRuntime(OptimizationRuntime);' in control
assert 'Paths' in impl and 'RandomWalkEnemies' in impl
assert 'bEnableUpdateRateOptimizations' in enemy
assert 'bOriginalMeshURO' in enemy and 'bOriginalAlwaysRelevant' in enemy
assert 'Diagnostics->bEnableLogging' in enemy
assert 'Recorder->SetIdleTickOptimization' in enemy
assert 'VisualPredictor->SetIdleTickOptimization' in enemy
assert 'bOptimizeIdleTick ? bRecording' in recorder
assert 'RefreshOptimizedTick();' in recorder and 'HandleSessionChanged()' in recorder
assert 'bEventMode' in predictor and 'ESharpExperimentMode::EventPredictor' in predictor and 'ESharpExperimentMode::EventOnly' in predictor
assert 'FJsonObjectConverter::UStructToJsonObject(Enemy->OptimizationRuntime)' in manifest
assert 'server_samples.csv' in recorder and 'client_' in recorder
assert 'bAutoSpawnAlwaysRelevant' in read('Experiment/SharpExperimentManagerAutoSpawn.cpp')
assert 'UCharacterMovementComponent' not in enemy or 'SetNetUpdateFrequency' not in enemy
assert 'SetIsReplicatedByDefault' not in enemy
print('PASS: Optimization profile enum, authority resolution, auto/manual enemy integration, replication, manifest, idle tick gating, URO and opt-in culling')
print('NOTE: UE 5.8 UHT/UBT build and server/client PIE functional validation remain required.')
