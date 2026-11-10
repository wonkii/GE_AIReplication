"""Entry point shared by AnimAnalysis.* console commands and Unreal Python smoke tests."""
import csv
import hashlib
import importlib
import json
from pathlib import Path
import sys
import traceback

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
sys.path.insert(0, str(PROJECT / 'Scripts' / 'Analysis'))
from task_readme import describe_task, task_id

LAST_TASK = None
DEFAULT_ASSET = '/Game/Combat/Animations/AM_HitReaction_Front'
DEFAULT_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, value):
    # Exclusive creation protects raw data and previous reports from accidental reruns.
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)


def log(message):
    try:
        import unreal
        unreal.log('[AnimAnalysis] ' + message)
    except ImportError:
        print('[AnimAnalysis] ' + message)


def outcome(task, message):
    with (task / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write('\n- 결과: ' + message + '\n')
    log(message + ' | ' + str(task))


def new_task(command, name='joint-analysis'):
    global LAST_TASK
    task = PROJECT / 'Saved' / 'Tasks' / task_id(name)
    describe_task(task, 'Joint extraction and PELT segmentation',
                  'I1 후보 Library 및 I2 Warp 구간 설계를 위한 오프라인 분석', command,
                  ['export.json: 전체 관절 원본, 좌표/시간/에셋 설정',
                   'analysis.json: PELT 구간과 Warp 후보/거부 근거',
                   'frame_features.csv / joint_metrics.csv: 구간별·관절별 속도',
                   'report.html: 브라우저에서 열어 타임라인·관절 움직임·포즈 확인',
                   'library.json: I1/I2 전달 계약; reviewed=false',
                   'validation-*.json / selftest.json: 테스트 결과'])
    LAST_TASK = task
    return task


def resolve_task(value='latest'):
    if value == 'latest':
        if LAST_TASK and (LAST_TASK / 'export.json').is_file():
            return LAST_TASK
        tasks = sorted((PROJECT / 'Saved' / 'Tasks').glob('*-joint-analysis-*/export.json'))
        if not tasks:
            raise ValueError('No extraction yet. Run AnimAnalysis.Extract first.')
        return tasks[-1].parent
    path = Path(value)
    if not path.is_absolute():
        path = PROJECT / path
    path = path.resolve()
    if not (path / 'README.md').is_file() or not (path / 'export.json').is_file():
        raise ValueError('Expected an extraction task directory containing README.md and export.json')
    return path


def dependencies():
    expected = hashlib.sha256((HERE / 'requirements.txt').read_bytes()).hexdigest()
    for path in sorted((PROJECT / 'Saved' / 'Tasks').glob('*-anim-analysis-environment-*/environment.json'), reverse=True):
        env = read(path)
        if env.get('status') == 'ready' and env.get('requirements_sha256') == expected and Path(env['packages']).is_dir():
            sys.path.insert(0, env['packages'])
            break
    else:
        raise RuntimeError('Run Scripts/SetupAnimationAnalysis.ps1 once to install the pinned analysis environment.')
    module = importlib.import_module('motion_analysis')
    import numpy, scipy, ruptures
    actual = (numpy.__version__, scipy.__version__, ruptures.__version__)
    if actual != ('1.26.4', '1.13.1', 'v1.1.10') and actual != ('1.26.4', '1.13.1', '1.1.10'):
        raise RuntimeError(f'Unexpected dependency versions {actual}; restart the editor after setup.')
    return module


def file_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def provenance(raw):
    files = {}
    for key in ['asset', 'sequence', 'mesh', 'skeleton']:
        package = raw[key].split('.')[0]
        if not package.startswith('/Game/'):
            raise ValueError('V1 provenance requires project /Game assets')
        filename = PROJECT / 'Content' / (package[6:] + '.uasset')
        files[str(filename.relative_to(PROJECT))] = file_hash(filename)
    return files


def extract(asset, options, task=None):
    import unreal
    task = task or new_task('AnimAnalysis.Extract ' + asset + ' ' + str(options))
    result = unreal.JointMotionExtractor.extract(asset, options.get('mesh', DEFAULT_MESH),
                                                 options.get('section', ''), float(options.get('hz', 60)),
                                                 float(options.get('rate', 1)))
    raw = json.loads(result)
    if 'error' in raw:
        raise ValueError(raw['error'])
    raw['asset_files'] = provenance(raw)
    raw['extractor_sha256'] = file_hash(PROJECT / 'Source/GEAnimationAnalysisEditor/Private/JointMotionExtractor.cpp')
    write(task / 'export.json', raw)
    outcome(task, f'PASS Extract: {len(raw["samples"])} poses, {len(raw["bones"])} bones, {raw["duration"]:.4f}s')
    return task


def check_current_assets(raw):
    for relative, expected in raw.get('asset_files', {}).items():
        if file_hash(PROJECT / relative) != expected:
            raise ValueError(f'Stale export: {relative} changed. Extract again.')
    expected = raw.get('extractor_sha256')
    if expected and file_hash(PROJECT / 'Source/GEAnimationAnalysisEditor/Private/JointMotionExtractor.cpp') != expected:
        raise ValueError('Extractor implementation changed. Rebuild and Extract again.')


def analyze_task(task, options):
    module = dependencies()
    raw = read(task / 'export.json')
    check_current_assets(raw)
    if (task / 'analysis.json').exists():
        previous = task
        task = new_task('AnimAnalysis.Analyze ' + str(previous) + ' ' + str(options))
        write(task / 'export.json', raw)
    profile = Path(options.get('profile', HERE / 'manny_profile.json'))
    config = read(profile)
    for option, key, converter in [('penalty', 'penalty', float), ('minduration', 'min_duration', float), ('model', 'model', str)]:
        if option in options:
            config[key] = converter(options[option])
    analysis, metrics = module.analyze(raw, config)
    analysis['implementation_sha256'] = file_hash(HERE / 'motion_analysis.py')
    analysis['analysis_hash'] = module.digest({k: v for k, v in analysis.items() if k != 'analysis_hash'})
    module.validate_analysis(raw, analysis)
    write(task / 'analysis.json', analysis)
    with (task / 'frame_features.csv').open('x', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(['start_seconds', 'end_seconds'] + analysis['feature_names'])
        for index, row in enumerate(metrics['features']):
            writer.writerow([metrics['times'][index], metrics['times'][index + 1]] + row.tolist())
    with (task / 'joint_metrics.csv').open('x', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(['start_seconds', 'end_seconds', 'bone', 'relative_speed_cm_s', 'local_angular_speed_rad_s'])
        for index in range(len(metrics['speed'])):
            for bone, info in enumerate(raw['bones']):
                writer.writerow([metrics['times'][index], metrics['times'][index + 1], info['name'],
                                 metrics['speed'][index, bone], metrics['omega'][index, bone]])
    from motion_report import render
    render(task / 'report.html', raw, analysis, metrics)
    outcome(task, f'PASS Analyze: {len(analysis["segments"])} segments, {len(analysis["warp_windows"])} Warp proposals (unreviewed)')
    return task


def library_payload(raw, analysis):
    return {'schema_version': 1, 'coordinate_contract': raw['coordinates'],
            'time_contract': 'seconds are relative playback; asset_seconds are Montage/Sequence track time',
            'candidates': [{'candidate_id': 'Motion_' + analysis['source_hash'][:12], 'asset': raw['asset'],
                            'section': raw['section'], 'source_hash': analysis['source_hash'],
                            'analysis_hash': analysis['analysis_hash'], 'play_rate': raw['play_rate'],
                            'duration': raw['duration'], 'reviewed': False,
                            'trajectory': [{'time': s['time'], 'asset_time': s['asset_time'],
                                            'position': s['root_motion_position'], 'rotation': s['root_motion_rotation']}
                                           for s in raw['samples']],
                            'phases': analysis['segments'], 'warp_windows': analysis['warp_windows']}]}


def make_library(payload, asset_path=None):
    import unreal
    # Prepare all data before creating a persistent asset; never replace an existing package.
    candidates = []
    for entry in payload['candidates']:
        candidate = unreal.HitWallReactionCandidate()
        for prop, value in [('candidate_id', entry['candidate_id']), ('animation', unreal.load_asset(entry['asset'])),
                            ('section', entry['section']), ('source_hash', entry['source_hash']),
                            ('analysis_hash', entry['analysis_hash']), ('play_rate', entry['play_rate']),
                            ('duration_seconds', entry['duration']), ('reviewed', False)]:
            candidate.set_editor_property(prop, value)
        trajectory = []
        for point in entry['trajectory']:
            sample = unreal.HitWallTrajectorySample()
            sample.set_editor_property('time_seconds', point['time'])
            sample.set_editor_property('asset_time_seconds', point['asset_time'])
            transform = unreal.Transform()
            transform.set_editor_property('translation', unreal.Vector(*point['position']))
            transform.set_editor_property('rotation', unreal.Quat(*point['rotation']))
            sample.set_editor_property('root_motion', transform)
            trajectory.append(sample)
        candidate.set_editor_property('trajectory', trajectory)
        for prop, entries in [('phases', entry['phases']), ('proposed_warp_windows', entry['warp_windows'])]:
            intervals = []
            for entry_interval in entries:
                interval = unreal.HitWallMotionInterval()
                for source, target in [('start', 'start_seconds'), ('end', 'end_seconds'),
                                       ('asset_start', 'asset_start_seconds'), ('asset_end', 'asset_end_seconds'), ('label', 'label')]:
                    interval.set_editor_property(target, entry_interval[source])
                interval.set_editor_property('evidence', json.dumps(entry_interval['evidence'], ensure_ascii=False))
                intervals.append(interval)
            candidate.set_editor_property(prop, intervals)
        candidates.append(candidate)
    library = unreal.new_object(unreal.HitWallReactionLibrary)
    library.set_editor_property('candidates', candidates)
    if len(library.get_editor_property('candidates')) != len(candidates):
        raise ValueError('Library round-trip failed')
    for stored, original in zip(library.get_editor_property('candidates'), payload['candidates']):
        if stored.get_editor_property('reviewed') or len(stored.get_editor_property('trajectory')) != len(original['trajectory']):
            raise ValueError('Library review flag or trajectory round-trip failed')
        for point, source in zip(stored.get_editor_property('trajectory'), original['trajectory']):
            if abs(point.get_editor_property('time_seconds') - source['time']) > 1e-5:
                raise ValueError('Library time round-trip failed')
    if asset_path:
        if not asset_path.startswith('/Game/') or '.' in asset_path or '..' in asset_path:
            raise ValueError('Asset must be a new /Game/folder/Name package path')
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            raise ValueError('Library asset already exists; choose a new name')
        folder, name = asset_path.rsplit('/', 1)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.HitWallReactionLibrary)
        saved = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.HitWallReactionLibrary, factory)
        if not saved:
            raise ValueError('Unable to create Library asset')
        saved.set_editor_property('candidates', candidates)
        if not unreal.EditorAssetLibrary.save_loaded_asset(saved):
            raise ValueError('Unable to save Library asset')
    return library


def build(task, options):
    module = dependencies()
    raw, analysis = read(task / 'export.json'), read(task / 'analysis.json')
    check_current_assets(raw)
    module.validate_analysis(raw, analysis)
    payload = library_payload(raw, analysis)
    make_library(payload, options.get('asset'))
    path = task / 'library.json'
    if path.exists():
        if read(path) != payload:
            raise ValueError('Existing Library differs; analyze into a new task')
    else:
        write(path, payload)
    if options.get('asset'):
        write(task / (task_id('library-asset') + '.json'), {'asset': options['asset']})
    outcome(task, 'PASS Build: I1 trajectory/phases + I2 proposed windows; UE DataAsset round-trip passed'
            + ((' | saved ' + options['asset']) if options.get('asset') else ' (transient; use Asset=/Game/... to save)'))
    return task


def validate(task):
    module = dependencies()
    raw = read(task / 'export.json')
    module.arrays(raw)
    check_current_assets(raw)
    checks = ['finite complete poses', 'ordered times and source-time mapping', 'current asset hashes']
    if (task / 'analysis.json').exists():
        analysis = read(task / 'analysis.json')
        module.validate_analysis(raw, analysis)
        checks += ['analysis integrity', 'continuous segment coverage', 'unreviewed bounded Warp proposals']
        if (task / 'library.json').exists():
            if read(task / 'library.json') != library_payload(raw, analysis):
                raise ValueError('Library differs from validated analysis')
            checks.append('Library matches source analysis')
    write(task / (task_id('validation') + '.json'), {'status': 'PASS', 'checks': checks})
    outcome(task, 'PASS Validate: ' + ', '.join(checks))
    return task


HELP = '''AnimAnalysis commands (Editor Output Log, command type Cmd; not Python):
  AnimAnalysis.Extract <asset> [Mesh=/Game/...] [Section=Default] [Hz=60] [Rate=1]
  AnimAnalysis.Analyze latest [Profile=<json>] [Penalty=3] [MinDuration=0.1] [Model=rbf]
  AnimAnalysis.Build latest [Asset=/Game/Combat/Analysis/DA_MyCandidate]
  AnimAnalysis.Validate latest
  AnimAnalysis.Preview latest
  AnimAnalysis.SelfTest
  AnimAnalysis.Run <asset> [all Extract/Analyze/Build options]
Use a quoted task directory instead of latest to revisit results. Analyze reruns create a new task.
Default mesh is SKM_Manny_Simple; use a matching mesh/profile for other skeletons.
Build without Asset= checks a transient UE DataAsset and writes library.json. No gameplay changes.
First run Scripts/SetupAnimationAnalysis.ps1 once. Run without an asset uses AM_HitReaction_Front.
'''


def dispatch(arguments):
    action = arguments[0].lower() if arguments else 'help'
    if action == 'help':
        log(HELP)
        return None
    options, positional = {}, []
    for arg in arguments[1:]:
        if '=' in arg:
            key, value = arg.split('=', 1)
            options[key.lower()] = value
        else:
            positional.append(arg)
    allowed = {'extract': {'mesh', 'section', 'hz', 'rate'}, 'analyze': {'profile', 'penalty', 'minduration', 'model'},
               'build': {'asset'}, 'validate': set(), 'preview': set(), 'selftest': set()}
    allowed['run'] = allowed['extract'] | allowed['analyze'] | allowed['build']
    if action not in allowed or set(options) - allowed[action] or len(positional) > 1:
        raise ValueError('Unknown command/options. ' + HELP)
    task = None
    try:
        if action == 'selftest':
            dependencies()
            task = new_task('AnimAnalysis.SelfTest', 'joint-selftest')
            from test_motion_analysis import run_tests
            result = run_tests(task)
            write(task / 'selftest.json', result)
            if not result['passed']:
                raise AssertionError('SelfTest failed; see selftest.json and tests.txt')
            outcome(task, f'PASS SelfTest: {result["tests_run"]} tests')
            return task
        if action in ('extract', 'run'):
            asset = positional[0] if positional else DEFAULT_ASSET
            task = new_task('AnimAnalysis.' + action + ' ' + asset + ' ' + str(options))
            extract(asset, options, task)
            if action == 'run':
                task = analyze_task(task, options)
                build(task, options)
                validate(task)
        else:
            task = resolve_task(positional[0] if positional else 'latest')
            if action == 'analyze':
                task = analyze_task(task, options)
            elif action == 'build':
                build(task, options)
            elif action == 'validate':
                validate(task)
            elif action == 'preview':
                report = task / 'report.html'
                if not report.is_file():
                    raise ValueError('Run Analyze first')
                from report_browser import open_report
                browser = open_report(report)
                log('Preview via ' + browser + ' ' + str(report))
        return task
    except Exception as error:
        if task:
            write(task / (task_id('failure') + '.json'), {'status': 'FAIL', 'error': str(error), 'traceback': traceback.format_exc()})
            outcome(task, 'FAIL ' + action + ': ' + str(error))
        raise


if __name__ == '__main__':
    dispatch(sys.argv[1:])
