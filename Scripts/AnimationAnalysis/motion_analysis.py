"""Offline kinematics and PELT. No Unreal dependency; times are playback seconds.

N poses describe N-1 intervals. PELT's terminal index is an endpoint, not a change.
All proposals remain unreviewed and cannot authorize collision or gameplay changes.
"""
import hashlib
import json
import math

import numpy as np
import ruptures as rpt
from scipy.ndimage import uniform_filter1d


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode('utf-8')).hexdigest()


def arrays(raw):
    if raw.get('schema_version') != 1:
        raise ValueError('Unsupported extraction schema')
    samples, bones = raw['samples'], raw['bones']
    names = [bone['name'] for bone in bones]
    if not names or len(set(names)) != len(names) or len(samples) < 3:
        raise ValueError('Need unique bones and at least three poses')
    for i, bone in enumerate(bones):
        if bone['parent'] != -1 and not 0 <= bone['parent'] < i:
            raise ValueError('Invalid parent hierarchy')
    if bones[0]['parent'] != -1:
        raise ValueError('Bone zero must be the root')
    times = np.asarray([s['time'] for s in samples], dtype=float)
    dt = np.diff(times)
    rate = float(raw['sample_rate'])
    if not math.isfinite(rate) or rate <= 0 or not np.isfinite(times).all() or (dt <= 1e-8).any():
        raise ValueError('Non-finite or non-increasing sample times')
    if abs(times[0]) > 1e-7 or abs(times[-1] - raw['duration']) > 1e-6:
        raise ValueError('Missing first or final pose')
    if (dt > 1 / rate + 1e-6).any():
        raise ValueError('Missing samples')
    def field(key):
        return np.asarray([[joint[key] for joint in s['joints']] for s in samples], dtype=float)
    relative, component, rotation = field('relative'), field('component'), field('local_rotation')
    root = np.asarray([s['root_motion_position'] for s in samples], dtype=float)
    root_q = np.asarray([s['root_motion_rotation'] for s in samples], dtype=float)
    for value, shape in [(relative, (len(times), len(bones), 3)), (component, (len(times), len(bones), 3)),
                         (rotation, (len(times), len(bones), 4)), (root, (len(times), 3)), (root_q, (len(times), 4))]:
        if value.shape != shape or not np.isfinite(value).all():
            raise ValueError('Non-finite or incomplete pose data')
    for q in [rotation, root_q]:
        if not np.allclose(np.linalg.norm(q, axis=-1), 1, atol=1e-4):
            raise ValueError('Quaternion is not normalized')
    asset_times = np.asarray([s['asset_time'] for s in samples])
    expected = raw['asset_start'] + times * raw['effective_asset_rate']
    if not np.allclose(asset_times, expected, atol=1e-5, rtol=0):
        raise ValueError('Playback time and asset time disagree')
    return times, dt, relative, component, rotation, root, root_q


def angular_speed(q, dt):
    dot = np.abs(np.sum(q[:-1] * q[1:], axis=-1))
    denominator = dt.reshape((-1,) + (1,) * (dot.ndim - 1))
    return 2 * np.arccos(np.clip(dot, 0, 1)) / denominator


def validate_config(config):
    if config['model'] not in ('l2', 'rbf'):
        raise ValueError('model must be l2 or rbf')
    for key in ['penalty', 'min_duration', 'contact_height_cm', 'contact_speed_cm_s',
                'linear_scale_cm_s', 'angular_scale_rad_s', 'linear_noise_cm_s',
                'angular_noise_rad_s', 'low_motion_speed_cm_s', 'low_motion_angular_speed_rad_s',
                'moving_speed_cm_s', 'warp_min_distance_cm', 'warp_vertical_tolerance_cm',
                'warp_lateral_tolerance_cm', 'warp_rotation_tolerance_rad']:
        if not math.isfinite(config[key]) or config[key] <= 0:
            raise ValueError(f'{key} must be finite and positive')
    if not isinstance(config['smooth_samples'], int) or not 1 <= config['smooth_samples'] <= 31:
        raise ValueError('smooth_samples must be an integer in 1..31')


def segment_signal(signal, rate, config):
    """Return exclusive endpoints, including N once, even for a short/constant clip."""
    validate_config(config)
    minimum = max(2, math.ceil(config['min_duration'] * rate))
    if config['model'] == 'rbf' and len(signal) > 4096:
        raise ValueError('RBF kernel is quadratic in memory; use Model=l2 or extract a shorter/lower-rate clip (<=4096 intervals)')
    if len(signal) < minimum * 2 or np.max(np.ptp(signal, axis=0)) < 1e-8:
        return [len(signal)]
    return rpt.Pelt(model=config['model'], min_size=minimum, jump=1).fit(signal).predict(pen=config['penalty'])


def analyze(raw, config):
    validate_config(config)
    times, dt, relative, component, rotation, root, root_q = arrays(raw)
    names = [b['name'] for b in raw['bones']]
    velocity = np.diff(relative, axis=0) / dt[:, None, None]
    speed = np.linalg.norm(velocity, axis=2)
    omega = angular_speed(rotation, dt)
    # Suppress sub-perceptual numeric noise before normalizing.
    speed[speed < config['linear_noise_cm_s']] = 0
    omega[omega < config['angular_noise_rad_s']] = 0
    root_v = np.diff(root, axis=0) / dt[:, None]
    root_speed = np.linalg.norm(root_v[:, :2], axis=1)
    root_omega = angular_speed(root_q, dt)
    columns = {'root_vx': root_v[:, 0], 'root_vy': root_v[:, 1], 'root_vz': root_v[:, 2],
               'root_angular_speed': root_omega}
    scales = [config['linear_scale_cm_s']] * 3 + [config['angular_scale_rad_s']]
    groups, warnings = {}, []
    for group, requested in config['groups'].items():
        indices = [names.index(name) for name in requested if name in names]
        missing = sorted(set(requested) - set(names))
        if missing:
            warnings.append(f'{group}: missing {", ".join(missing)}')
        if not indices:
            raise ValueError(f'No bones for group {group}; supply a skeleton-specific profile')
        groups[group] = indices
        columns[group + '_speed'] = speed[:, indices].mean(axis=1)
        columns[group + '_angular_speed'] = omega[:, indices].mean(axis=1)
        scales.extend([config['linear_scale_cm_s'], config['angular_scale_rad_s']])
    contacts = {}
    for foot in config['feet']:
        if foot not in names:
            raise ValueError(f'Missing foot {foot}; supply a skeleton-specific profile')
        index = names.index(foot)
        # Asset-space floor proxy: bone minimum, not a measured game-world collision.
        height = component[:-1, index, 2] - component[:, index, 2].min()
        foot_speed = np.linalg.norm(np.diff(component[:, index], axis=0) / dt[:, None], axis=1)
        contact = (height <= config['contact_height_cm']) & (foot_speed <= config['contact_speed_cm_s'])
        contacts[foot] = contact
        columns[foot + '_contact_proxy'] = contact.astype(float)
        scales.append(1.)
    feature_names = list(columns)
    features = np.column_stack(list(columns.values()))
    normalized = uniform_filter1d(features / np.asarray(scales), size=config['smooth_samples'], axis=0, mode='nearest')
    endpoints = segment_signal(normalized, raw['sample_rate'], config)
    # PELT constrains sample counts; a shortened last interval can violate seconds.
    if len(endpoints) > 1 and times[endpoints[-1]] - times[endpoints[-2]] < config['min_duration'] - 1e-8:
        endpoints.pop(-2)
    segments, start = [], 0
    for number, end in enumerate(endpoints):
        moving = float(root_speed[start:end].mean()) >= config['moving_speed_cm_s']
        selected = sorted(set(sum(groups.values(), [])))
        body = float(speed[start:end, selected].mean())
        body_angular = float(omega[start:end, selected].mean())
        quiet = body < config['low_motion_speed_cm_s'] and body_angular < config['low_motion_angular_speed_rad_s']
        label = 'root_travel' if moving else ('low_motion' if quiet else 'articulation')
        evidence = {'mean_root_speed_cm_s': float(root_speed[start:end].mean()), 'mean_joint_speed_cm_s': body,
                    'mean_joint_angular_speed_rad_s': body_angular,
                    'contact_fraction': {foot: float(v[start:end].mean()) for foot, v in contacts.items()}}
        if start:
            before = normalized[max(0, start - 3):start].mean(axis=0)
            after = normalized[start:min(len(normalized), start + 3)].mean(axis=0)
            strongest = np.argsort(np.abs(after - before))[-3:][::-1]
            evidence['boundary_features'] = [feature_names[int(i)] for i in strongest]
        segments.append({'id': number, 'start_index': start, 'end_index': end,
                         'start': float(times[start]), 'end': float(times[end]),
                         'asset_start': raw['samples'][start]['asset_time'], 'asset_end': raw['samples'][end]['asset_time'],
                         'source_start_frame': raw['samples'][start]['source_frame'],
                         'source_end_frame': raw['samples'][end]['source_frame'],
                         'label': label, 'evidence': evidence, 'semantic_review_required': True})
        start = end
    proposals, rejection = warp_windows(raw, config, segments, root, root_q)
    versions = {'numpy': np.__version__, 'ruptures': rpt.__version__}
    analysis = {'schema_version': 1, 'source_hash': digest(raw), 'config': config,
                'versions': versions, 'feature_names': feature_names, 'segments': segments,
                'warp_windows': proposals, 'warp_rejection_reasons': rejection,
                'warnings': warnings + ['Contact is an asset-space proxy. No world collision, IK or blend validation.',
                                        'Labels are kinematic; gameplay phases require review.'],
                'reviewed': False}
    analysis['analysis_hash'] = digest(analysis)
    metrics = {'times': times, 'features': features, 'speed': speed, 'omega': omega, 'root_speed': root_speed}
    return analysis, metrics


def warp_windows(raw, config, segments, root, root_q):
    reasons = []
    displacement = root - root[0]
    planar = displacement[-1, :2]
    length = np.linalg.norm(planar)
    if not raw['root_motion_enabled']:
        reasons.append('Root Motion is disabled on source Sequence')
    if length < config['warp_min_distance_cm']:
        reasons.append('Insufficient net planar travel')
    if np.max(np.abs(displacement[:, 2])) > config['warp_vertical_tolerance_cm']:
        reasons.append('Vertical motion exceeds planar-warp tolerance')
    angles = 2 * np.arccos(np.clip(np.abs(np.sum(root_q * root_q[0], axis=1)), 0, 1))
    if angles.max() > config['warp_rotation_tolerance_rad']:
        reasons.append('Root rotation exceeds fixed-yaw tolerance')
    if length > 1e-8:
        direction = planar / length
        progress = displacement[:, :2] @ direction
        lateral = displacement[:, :2] - progress[:, None] * direction
        if np.linalg.norm(lateral, axis=1).max() > config['warp_lateral_tolerance_cm']:
            reasons.append('Lateral drift exceeds straight-path tolerance')
        if (np.diff(progress) < -1e-4).any():
            reasons.append('Root trajectory reverses')
    if reasons:
        return [], reasons
    proposals = []
    for segment in segments:
        a, b = segment['start_index'], segment['end_index']
        distance = float(np.linalg.norm(root[b, :2] - root[a, :2]))
        if (segment['label'] != 'root_travel' or distance < config['warp_min_distance_cm']
                or segment['end'] - segment['start'] < config['min_duration']):
            continue
        proposals.append({**{k: segment[k] for k in ['start', 'end', 'asset_start', 'asset_end']},
                          'label': 'warp_proposal', 'distance_cm': distance, 'reviewed': False,
                          'evidence': {'phase_id': segment['id'], 'contact_fraction': segment['evidence']['contact_fraction'],
                                       'required_checks': ['foot sliding', 'pose continuity', 'whole warped path sweep',
                                                           'server / owner / observer playback']}})
    if not proposals:
        reasons.append('No sufficiently long moving phase')
    return proposals, reasons


def validate_analysis(raw, analysis):
    arrays(raw)
    if analysis['source_hash'] != digest(raw):
        raise ValueError('Stale analysis: extraction hash mismatch')
    unhashed = {k: v for k, v in analysis.items() if k != 'analysis_hash'}
    if analysis['analysis_hash'] != digest(unhashed):
        raise ValueError('Analysis hash mismatch')
    previous = 0
    for segment in analysis['segments']:
        a, b = segment['start_index'], segment['end_index']
        if a != previous or not a < b < len(raw['samples']):
            raise ValueError('Segments have a gap, overlap or invalid endpoint')
        for index, key in [(a, 'start'), (b, 'end')]:
            if abs(segment[key] - raw['samples'][index]['time']) > 1e-7:
                raise ValueError('Segment time differs from sample index')
            if abs(segment['asset_' + key] - raw['samples'][index]['asset_time']) > 1e-7:
                raise ValueError('Segment asset time differs from sample index')
        previous = b
    if previous != len(raw['samples']) - 1:
        raise ValueError('Segments do not cover the clip')
    for window in analysis['warp_windows']:
        if not 0 <= window['start'] < window['end'] <= raw['duration'] or window['reviewed']:
            raise ValueError('Invalid or automatically approved Warp proposal')
    if analysis['reviewed']:
        raise ValueError('Offline output must remain unreviewed')
    return True
