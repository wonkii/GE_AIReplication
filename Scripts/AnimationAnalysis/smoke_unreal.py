"""Headless integration through the same console commands exposed to the user."""
from pathlib import Path
import sys
import json

import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(PROJECT / 'Scripts/AnimationAnalysis'))
import anim_analysis as driver


def main():
    summary_task = driver.new_task('Scripts/TestAnimationAnalysis.ps1', 'joint-integration')
    results = {}
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        def command(text):
            unreal.SystemLibrary.execute_console_command(world, text)
            return driver.LAST_TASK
        task = command('AnimAnalysis.SelfTest')
        assert driver.read(task / 'selftest.json')['passed']
        results['numerical_tests'] = str(task)
        task = command('AnimAnalysis.Run ' + driver.DEFAULT_ASSET)
        assert (task / 'library.json').is_file(), 'Run did not finish'
        raw = driver.read(task / 'export.json')
        analysis = driver.read(task / 'analysis.json')
        results['montage_run'] = str(task)
        results['montage_segments'] = len(analysis['segments'])
        results['montage_warp_proposals'] = len(analysis['warp_windows'])
        # Independent hierarchy reconstruction checks exported coordinate conventions.
        import numpy as np
        from scipy.spatial.transform import Rotation
        worst = 0.
        for sample in raw['samples']:
            positions, rotations, scales = [], [], []
            for bone, joint in zip(raw['bones'], sample['joints']):
                local = np.array(joint['local_position'])
                rotation = Rotation.from_quat(joint['local_rotation'])
                scale = np.array(joint['local_scale'])
                parent = bone['parent']
                if parent >= 0:
                    local = positions[parent] + rotations[parent].apply(scales[parent] * local)
                    rotation = rotations[parent] * rotation
                    scale = scales[parent] * scale
                positions.append(local)
                rotations.append(rotation)
                scales.append(scale)
                worst = max(worst, float(np.linalg.norm(local - joint['component'])))
            for joint, position in zip(sample['joints'], positions):
                expected = rotations[0].inv().apply(position - positions[0]) / scales[0]
                worst = max(worst, float(np.linalg.norm(expected - joint['relative'])))
        assert worst < .001, f'Coordinate reconstruction error {worst}cm'
        results['max_coordinate_error_cm'] = worst
        # Same simple Montage and source Sequence must describe the same pose stream.
        seq_raw = json.loads(unreal.JointMotionExtractor.extract(raw['sequence'], driver.DEFAULT_MESH, '', 60., 1.))
        assert 'error' not in seq_raw
        assert len(raw['samples']) == len(seq_raw['samples'])
        for a, b in zip(raw['samples'], seq_raw['samples']):
            np.testing.assert_allclose(a['root_motion_position'], b['root_motion_position'], atol=1e-5)
            np.testing.assert_allclose([j['relative'] for j in a['joints']], [j['relative'] for j in b['joints']], atol=1e-5)
        results['sequence_montage_equivalence'] = True
        fast = json.loads(unreal.JointMotionExtractor.extract(driver.DEFAULT_ASSET, driver.DEFAULT_MESH, '', 60., 2.))
        assert abs(fast['duration'] * 2 - raw['duration']) < 1e-6
        np.testing.assert_allclose(fast['samples'][-1]['root_motion_position'], raw['samples'][-1]['root_motion_position'], atol=1e-5)
        results['play_rate_mapping'] = True
        bad = json.loads(unreal.JointMotionExtractor.extract(driver.DEFAULT_ASSET, driver.DEFAULT_MESH, 'MissingSection', 60., 1.))
        assert 'error' in bad
        bad = json.loads(unreal.JointMotionExtractor.extract('/Game/MissingAnimation', driver.DEFAULT_MESH, '', 60., 1.))
        assert 'error' in bad
        results['invalid_inputs_rejected'] = True
        # Check actual runtime library access: default query must not return an unreviewed candidate.
        library = driver.make_library(driver.read(task / 'library.json'))
        candidate_id = library.get_editor_property('candidates')[0].get_editor_property('candidate_id')
        rejected = library.find_candidate(candidate_id, True)
        assert rejected is None or rejected is False, f'Unreviewed candidate escaped gate: {rejected}'
        results['unreviewed_access_rejected'] = True
        accessible = library.find_candidate(candidate_id, False)
        assert accessible is not None and accessible is not False
        results['explicit_draft_access'] = True
        # Exercise individual commands, not just the Run convenience command.
        staged = command('AnimAnalysis.Extract ' + driver.DEFAULT_ASSET)
        assert (staged / 'export.json').is_file()
        staged = command('AnimAnalysis.Analyze latest Model=l2 Penalty=1')
        assert (staged / 'analysis.json').is_file()
        command('AnimAnalysis.Build latest')
        assert (staged / 'library.json').is_file()
        before = len(list(staged.glob('*-validation-*.json')))
        command('AnimAnalysis.Validate latest')
        assert len(list(staged.glob('*-validation-*.json'))) == before + 1
        # Preview command wiring, without launching a browser in an unattended test.
        import report_browser
        from unittest.mock import patch
        with patch.object(report_browser, 'open_report', return_value='test browser') as opened:
            command('AnimAnalysis.Preview latest')
            opened.assert_called_once_with(staged / 'report.html')
        old_hash = driver.file_hash(staged / 'analysis.json')
        repeated = command('AnimAnalysis.Analyze latest Model=rbf')
        assert repeated != staged and (repeated / 'analysis.json').is_file()
        assert old_hash == driver.file_hash(staged / 'analysis.json')
        results['individual_commands_and_preserved_rerun'] = True
        results['preview_command_uri'] = True
        results['status'] = 'PASS'
    except Exception as error:
        results['status'] = 'FAIL'
        results['error'] = str(error)
        raise
    finally:
        driver.write(summary_task / 'summary.json', results)
        driver.outcome(summary_task, results['status'] + ' Integration: ' + json.dumps(results, ensure_ascii=False))


main()
