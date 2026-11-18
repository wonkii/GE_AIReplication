"""Deterministic numerical contracts and known-boundary regression tests."""
import copy
import io
import json
from pathlib import Path
import unittest

import numpy as np
from motion_analysis import analyze, angular_speed, arrays, digest, segment_signal, validate_analysis


def config():
    result = json.loads((Path(__file__).parent / 'manny_profile.json').read_text())
    result['groups'] = {'torso': ['torso'], 'leg': ['foot']}
    result['feet'] = ['foot']
    result['model'] = 'l2'
    result['penalty'] = 0.5
    result['smooth_samples'] = 1
    return result


def fixture(rate=60, duration=2., travel=True):
    times = np.unique(np.minimum(np.arange(int(np.ceil(duration * rate)) + 1) / rate, duration))
    raw = {'schema_version': 1, 'sample_rate': rate, 'source_fps': 30, 'duration': duration,
           'asset_start': 0, 'asset_end': duration, 'effective_asset_rate': 1, 'root_motion_enabled': True,
           'bones': [{'name': 'root', 'parent': -1}, {'name': 'torso', 'parent': 0}, {'name': 'foot', 'parent': 0}],
           'samples': []}
    for t in times:
        x = max(0, min(t - .5, 1.)) * 100 if travel else 0
        positions = [[0, 0, 0], [0, 0, 100], [0, 0, 0]]
        raw['samples'].append({'time': float(t), 'asset_time': float(t), 'source_frame': float(t * 30),
                               'root_motion_position': [x, 0, 0], 'root_motion_rotation': [0, 0, 0, 1],
                               'joints': [{'relative': p, 'component': [p[0] + x, p[1], p[2]],
                                           'local_rotation': [0, 0, 0, 1]} for p in positions]})
    return raw


class MotionAnalysisTests(unittest.TestCase):
    def test_known_change_points(self):
        signal = np.repeat([[0., 0.], [4., 2.], [-2., 5.]], 30, axis=0)
        for model in ['l2', 'rbf']:
            cfg = {**config(), 'model': model, 'penalty': 3.}
            self.assertEqual(segment_signal(signal, 60, cfg), [30, 60, 90])

    def test_static_clip_has_one_segment(self):
        raw = fixture(travel=False)
        result, _ = analyze(raw, config())
        self.assertEqual(len(result['segments']), 1)
        self.assertEqual(result['warp_windows'], [])

    def test_root_translation_does_not_count_as_articulation(self):
        result, metrics = analyze(fixture(), config())
        self.assertTrue(np.all(metrics['speed'] == 0))
        bounds = [s['end'] for s in result['segments'][:-1]]
        self.assertTrue(any(abs(b - .5) <= 1/60 for b in bounds))
        self.assertTrue(any(abs(b - 1.5) <= 1/60 for b in bounds))
        self.assertTrue(result['warp_windows'])
        self.assertFalse(result['reviewed'])

    def test_quaternion_sign_is_not_motion(self):
        q = np.array([[0., 0., 0., 1.], [0., 0., 0., -1.]])
        self.assertAlmostEqual(float(angular_speed(q, np.array([.1]))[0]), 0)

    def test_rotation_without_translation_is_articulation(self):
        raw = fixture(travel=False)
        for sample in raw['samples']:
            angle = sample['time']
            sample['joints'][1]['local_rotation'] = [0, 0, np.sin(angle / 2), np.cos(angle / 2)]
        result, _ = analyze(raw, config())
        self.assertTrue(all(s['label'] == 'articulation' for s in result['segments']))

    def test_known_angular_speed(self):
        q = np.array([[0., 0., 0., 1.], [0., 0., np.sin(.1), np.cos(.1)]])
        self.assertAlmostEqual(float(angular_speed(q, np.array([.1]))[0]), 2., places=10)

    def test_partial_last_interval_and_endpoint(self):
        raw = fixture(duration=.757)
        result, _ = analyze(raw, config())
        self.assertEqual(result['segments'][-1]['end'], .757)
        self.assertTrue(validate_analysis(raw, result))

    def test_rate_changes_speed_not_travel(self):
        raw = fixture()
        fast = copy.deepcopy(raw)
        fast['sample_rate'] *= 2
        fast['duration'] /= 2
        fast['effective_asset_rate'] = 2
        for s in fast['samples']:
            s['time'] /= 2
        _, a = analyze(raw, config())
        _, b = analyze(fast, config())
        np.testing.assert_allclose(b['root_speed'], a['root_speed'] * 2)

    def test_missing_pose_and_nonfinite_rejected(self):
        raw = fixture()
        del raw['samples'][4]
        with self.assertRaises(ValueError): arrays(raw)
        raw = fixture()
        raw['samples'][2]['joints'][0]['relative'][0] = float('nan')
        with self.assertRaises(ValueError): arrays(raw)

    def test_missing_profile_bones_rejected(self):
        cfg = config()
        cfg['feet'] = ['missing']
        with self.assertRaises(ValueError): analyze(fixture(), cfg)

    def test_stale_or_tampered_data_rejected(self):
        raw = fixture()
        result, _ = analyze(raw, config())
        raw['samples'][3]['root_motion_position'][0] += 1
        with self.assertRaises(ValueError): validate_analysis(raw, result)
        raw = fixture()
        result['segments'][0]['end'] += .1
        with self.assertRaises(ValueError): validate_analysis(raw, result)

    def test_invalid_warp_paths_rejected(self):
        for kind in ['vertical', 'lateral', 'rotation', 'reverse', 'disabled']:
            raw = fixture()
            if kind == 'disabled': raw['root_motion_enabled'] = False
            if kind in ['vertical', 'lateral']:
                raw['samples'][60]['root_motion_position'][2 if kind == 'vertical' else 1] = 5
            if kind == 'rotation': raw['samples'][60]['root_motion_rotation'] = [0, 0, .6, .8]
            if kind == 'reverse': raw['samples'][60]['root_motion_position'][0] = -20
            result, _ = analyze(raw, config())
            self.assertFalse(result['warp_windows'], kind)
            self.assertTrue(result['warp_rejection_reasons'], kind)

    def test_deterministic_result(self):
        a, _ = analyze(fixture(), config())
        b, _ = analyze(fixture(), config())
        self.assertEqual(digest(a), digest(b))

    def test_short_clip_and_bad_penalty(self):
        raw = fixture(duration=.05)
        result, _ = analyze(raw, config())
        self.assertEqual(len(result['segments']), 1)
        with self.assertRaises(ValueError): analyze(raw, {**config(), 'penalty': float('nan')})

    def test_rbf_memory_limit(self):
        with self.assertRaisesRegex(ValueError, 'quadratic'):
            segment_signal(np.zeros((4097, 2)), 60, {**config(), 'model': 'rbf'})


def run_tests(task):
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(MotionAnalysisTests))
    (task / 'tests.txt').write_text(stream.getvalue(), encoding='utf-8')
    return {'passed': result.wasSuccessful(), 'tests_run': result.testsRun,
            'failures': [str(test) + ': ' + detail for test, detail in result.failures + result.errors]}
