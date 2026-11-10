"""Editor diagnostics call the same C++ planner used by gameplay; no Python runtime solver."""
import csv
import html
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent
sys.path.insert(0, str(PROJECT / 'Scripts' / 'Analysis'))
sys.path.insert(0, str(PROJECT / 'Scripts' / 'AnimationAnalysis'))
from task_readme import describe_task, task_id

DEFAULT_LIBRARY = '/Game/Combat/Analysis/DA_HitReaction_Front_PELT'
DEFAULT_POLICY = '/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft'
LAST_TASK = None
HELP = '''ReactionWarp (Editor Output Log, Cmd):
  ReactionWarp.Migrate <Library> NewPolicy=/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft [CandidateId=...]
  ReactionWarp.Validate <Library> [Policy=/Game/...] [AnalysisTask=<original task>]
  ReactionWarp.SelfTest
  ReactionWarp.Plan Candidate=Front BudgetCm=80 Policy=Balanced [Mode=MultiPhase] [Library=/Game/...]
  ReactionWarp.Preview latest
Authority PIE console (configure every relevant Blueprint with Library/Policy for packaged builds):
  ReactionWarp.Mode MultiPhase AllowDraft=1 Library=/Game/Combat/Analysis/DA_HitReaction_Front_PELT Policy=/Game/Combat/Analysis/DA_ReactionWarp_Front_Draft
  hitwall.Hit
  ReactionWarp.Compare gaps=250,80,40,10 modes=Off,Uniform,MiddleOnly,MultiPhase reps=3 AllowDraft=1
  hitwall.Stop
  ReactionWarp.NetTest
Front and Balanced are explicit aliases for the default library's unique Front Montage and default policy.
Migrate creates a NEW unapproved policy. Existing libraries/animations are never saved.
Policy=Equal creates a transient equal-cost comparison from the configured policy; it does not save it.
Draft permission is scoped to this PIE world; default game lookup requires separate policy approval.
BudgetCm is root projected distance; Compare gap is capsule surface-to-wall distance.
Owner prediction/replay is BLOCKED and player-controlled targets explicitly fallback.
'''


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, data):
    with Path(path).open('w', encoding='utf-8') as stream:
        json.dump(data, stream, indent=2, ensure_ascii=False, allow_nan=False)


def log(message):
    import unreal
    unreal.log('[ReactionWarp] ' + message)


def task(command):
    global LAST_TASK
    directory = PROJECT / 'Saved' / 'Tasks' / task_id('reaction-warp')
    describe_task(directory, 'Reaction Warp diagnostics', 'C++ solver and baked-source validation', command,
                  ['input.json / policy.json / plan.json: immutable input snapshot and C++ result',
                   'planned_trajectory.csv: shared exact integration; report.html: scale and XY curves',
                   'validation.json / selftest.json: checks and explicit unverified items',
                   'trials.csv / runtime_samples.csv (Compare): runtime results and pose/trace contact metrics'])
    LAST_TASK = directory
    return directory


def finish(directory, result):
    with (directory / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write('\nOutcome: ' + result + '\nRetain raw inputs/CSV with the report. Naturalness: user evaluation.\n')
    log(result + ' | ' + str(directory))


def load(library_path=DEFAULT_LIBRARY, policy_path=DEFAULT_POLICY):
    import unreal
    library = unreal.load_asset(library_path)
    policy = unreal.load_asset(policy_path)
    if not isinstance(library, unreal.HitWallReactionLibrary) or not isinstance(policy, unreal.ReactionWarpPolicy):
        raise ValueError('Load Library/Policy failed. Run ReactionWarp.Migrate with a new policy path first.')
    return library, policy


def source_check(library, directory=None, candidate_id=None):
    """Check SHA256 provenance and payload against original analysis, without modifying that task."""
    import anim_analysis
    candidates = library.get_editor_property('candidates')
    if candidate_id is not None:
        candidates = [c for c in candidates if str(c.get_editor_property('candidate_id')) == str(candidate_id)]
    if len(candidates) != 1:
        raise ValueError('Specify a one-candidate library for the Front fixture')
    candidate = candidates[0]
    paths = [Path(directory) / 'analysis.json'] if directory else sorted((PROJECT / 'Saved' / 'Tasks').glob('*-joint-analysis-*/analysis.json'), reverse=True)
    selected = None
    for path in paths:
        analysis = read(path)
        if analysis['analysis_hash'] == candidate.get_editor_property('analysis_hash'):
            selected = path.parent
            break
    if selected is None:
        raise ValueError('Original analysis not found; supply AnalysisTask=<directory>. No hash approval was inferred.')
    raw = read(selected / 'export.json')
    analysis = read(selected / 'analysis.json')
    anim_analysis.check_current_assets(raw)
    anim_analysis.dependencies().validate_analysis(raw, analysis)
    if analysis['source_hash'] != candidate.get_editor_property('source_hash'):
        raise ValueError('Library source hash mismatch')
    # Compare all generated fields through a fresh transient library. Human legacy
    # bReviewed is intentionally ignored; it cannot authorize a Warp policy.
    expected = anim_analysis.make_library(anim_analysis.library_payload(raw, analysis)).get_editor_property('candidates')[0]
    for prop in ('candidate_id', 'animation', 'section', 'source_hash', 'analysis_hash', 'play_rate', 'duration_seconds',
                 'trajectory', 'phases', 'proposed_warp_windows'):
        if candidate.get_editor_property(prop) != expected.get_editor_property(prop):
            raise ValueError('Library differs from analysis: ' + prop)
    return {'analysis_task': str(selected), 'source_hash': analysis['source_hash'], 'analysis_hash': analysis['analysis_hash'],
            'asset_sha256': anim_analysis.provenance(raw)}


def migrate(library_path, policy_path, candidate_id=None):
    import unreal
    if not policy_path.startswith('/Game/') or unreal.EditorAssetLibrary.does_asset_exist(policy_path):
        raise ValueError('NewPolicy must be a NEW /Game/ asset. Existing assets are never overwritten.')
    library = unreal.load_asset(library_path)
    if not isinstance(library, unreal.HitWallReactionLibrary):
        raise ValueError('Invalid Library')
    candidates = library.get_editor_property('candidates')
    if candidate_id is None:
        if len(candidates) != 1:
            raise ValueError('CandidateId is required for an ambiguous Library')
        candidate_id = candidates[0].get_editor_property('candidate_id')
    provenance = source_check(library, candidate_id=candidate_id)
    transient = unreal.new_object(unreal.ReactionWarpPolicy)
    error = unreal.ReactionWarpTools.bind_policy(library, transient, candidate_id)
    if error:
        raise ValueError(error)
    folder, name = policy_path.rsplit('/', 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.ReactionWarpPolicy)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.ReactionWarpPolicy, factory)
    if asset is None:
        raise ValueError('Unable to create policy')
    for prop in ('schema_version', 'policy_version', 'candidate_id', 'source_hash', 'analysis_hash', 'analysis_binding',
                 'approved_policy_hash', 'lateral_limit_cm', 'smoothness_weight', 'phases'):
        asset.set_editor_property(prop, transient.get_editor_property(prop))
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise ValueError('Unable to save new policy')
    return provenance


def inspect(library, policy, budget=80., mode='MultiPhase'):
    import unreal
    modes = {'Off': unreal.ReactionWarpMode.OFF, 'Uniform': unreal.ReactionWarpMode.UNIFORM,
             'MiddleOnly': unreal.ReactionWarpMode.MIDDLE_ONLY, 'MultiPhase': unreal.ReactionWarpMode.MULTI_PHASE}
    if mode not in modes:
        raise ValueError('Unsupported mode ' + mode)
    data = json.loads(unreal.ReactionWarpTools.inspect(library, policy, float(budget), modes[mode], True))
    if 'error' in data:
        raise ValueError(data['error'])
    return data


def export_plan(directory, data):
    write(directory / 'input.json', data)
    write(directory / 'policy.json', data['policy'])
    write(directory / 'plan.json', data['plan'])
    samples = data.get('samples', [])
    if samples:
        with (directory / 'planned_trajectory.csv').open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(samples[0]))
            writer.writeheader()
            writer.writerows(samples)
    render(directory)


def svg(samples, x, ys, title):
    if not samples:
        return ''
    xs = [float(s[x]) for s in samples]
    values = [float(s[y]) for s in samples for y in ys]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(values), max(values)
    sx = lambda v: 30 + 690 * (v - xmin) / max(1e-8, xmax - xmin)
    sy = lambda v: 170 - 140 * (v - ymin) / max(1e-8, ymax - ymin)
    lines = []
    for y, color in zip(ys, ('#1769aa', '#c94c19', '#239456')):
        points = ' '.join(f'{sx(float(s[x])):.3f},{sy(float(s[y])):.3f}' for s in samples)
        lines.append(f'<polyline fill="none" stroke="{color}" points="{points}"/>')
    return f'<h3>{html.escape(title)}</h3><p>{html.escape(x)} [{xmin:.3f}, {xmax:.3f}] / {html.escape(", ".join(ys))} [{ymin:.3f}, {ymax:.3f}]</p><svg viewBox="0 0 750 200" role="img">' + ''.join(lines) + '</svg>'


def table(rows):
    if not rows:
        return '<p>No runtime trials recorded.</p>'
    keys = list(rows[0])
    return '<table><tr>' + ''.join('<th>' + html.escape(k) + '</th>' for k in keys) + '</tr>' + ''.join(
        '<tr>' + ''.join('<td>' + html.escape(str(row.get(k, ''))) + '</td>' for k in keys) + '</tr>' for row in rows) + '</table>'


def render(directory):
    body = '<h1>Reaction Warp</h1><p>Naturalness: user evaluation. Owner prediction/replay: BLOCKED. Recorded execution does not imply visual approval.</p>'
    input_path = directory / 'input.json'
    if input_path.exists():
        data = read(input_path)
        body += '<pre>' + html.escape(json.dumps(data['plan'], indent=2, ensure_ascii=False)) + '</pre>'
        samples = data.get('samples', [])
        body += svg(samples, 'time', ['scale'], 'Continuous scale s(t)')
        body += svg(samples, 'time', ['rawX', 'plannedX'], 'Root X (blue original / orange planned)')
        body += svg(samples, 'plannedX', ['plannedY'], 'Planned XY path (lateral retained)')
    trials_path = directory / 'trials.csv'
    if trials_path.exists():
        with trials_path.open(encoding='utf-8-sig', newline='') as stream:
            body += '<h2>All scheduled trials</h2>' + table(list(csv.DictReader(stream)))
        for path in sorted(directory.glob('plan-*.json')):
            plan = read(path)
            body += '<details><summary>' + html.escape(path.name) + '</summary><pre>' + html.escape(json.dumps(plan, indent=2)) + '</pre></details>'
            body += svg(plan.get('keys', []), 'time', ['scale'], path.name + ' scale')
    runtime = directory / 'runtime_samples.csv'
    if runtime.exists():
        with runtime.open(encoding='utf-8-sig', newline='') as stream:
            rows = list(csv.DictReader(stream))
        grouped = {}
        for row in rows:
            grouped.setdefault((row['actor'], row['reaction_id'], row['plan_hash']), []).append(row)
        for identity, samples in grouped.items():
            body += '<h2>' + html.escape(str(identity)) + '</h2>'
            body += svg(samples, 'server_time', ['cap_x', 'cap_y'], 'Actual capsule world XY')
            body += svg(samples, 'cap_x', ['cap_y'], 'Actual world XY path')
            if 'planned_x' in samples[0]:
                body += svg(samples, 'server_time', ['planned_x', 'cap_x'], 'Planned / actual world X (blend and CMC errors are separate from solver error)')
            body += svg(samples, 'server_time', ['left_contact_travel_cm', 'right_contact_travel_cm'], 'Pose + ground-trace contact travel (cm)')
    (directory / 'report.html').write_text('<!doctype html><meta charset="utf-8"><title>Reaction Warp</title><style>body{font:15px system-ui;margin:32px;max-width:1100px}svg{max-width:850px;border:1px solid #ccc;width:100%}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:5px}pre{white-space:pre-wrap;overflow-wrap:anywhere}</style>' + body, encoding='utf-8')


def dispatch(arguments):
    import unreal
    action = arguments[0].lower() if arguments else 'help'
    if action == 'help':
        log(HELP)
        return
    opts, pos = {}, []
    for arg in arguments[1:]:
        if '=' in arg:
            k, v = arg.split('=', 1)
            opts[k.lower()] = v
        else:
            pos.append(arg)
    if action == 'preview':
        if pos and pos[0] != 'latest':
            directory = Path(pos[0])
        else:
            choices = [p.parent for p in (PROJECT / 'Saved' / 'Tasks').glob('*-reaction-warp-*/README.md')
                       if (p.parent / 'input.json').exists() or (p.parent / 'trials.csv').exists()]
            if not choices:
                raise ValueError('No plan or comparison yet')
            directory = max(choices, key=lambda p: p.stat().st_mtime)
        render(directory)
        from report_browser import open_report
        log('Preview: ' + open_report(directory / 'report.html'))
        return directory
    directory = task('ReactionWarp.' + ' '.join(arguments))
    try:
        if action == 'selftest':
            report = unreal.ReactionWarpTools.run_self_test()
            write(directory / 'selftest.json', {'report': report, 'passed': report.startswith('PASS')})
            if not report.startswith('PASS'):
                raise AssertionError(report)
            finish(directory, report)
            return directory
        library_path = opts.get('library', pos[0] if pos else DEFAULT_LIBRARY)
        if action == 'migrate':
            if 'newpolicy' not in opts:
                raise ValueError('NewPolicy=/Game/... is required')
            provenance = migrate(library_path, opts['newpolicy'], opts.get('candidateid'))
            write(directory / 'validation.json', provenance)
            finish(directory, 'PASS: new DRAFT policy ' + opts['newpolicy'] + '; runtime approval remains false')
            return directory
        if action not in ('validate', 'plan'):
            raise ValueError(HELP)
        policy_name = opts.get('policy', 'Balanced')
        library, policy = load(library_path, DEFAULT_POLICY if policy_name in ('Balanced', 'Equal') else policy_name)
        alias = opts.get('candidate', 'Front')
        if alias == 'Front':
            matches = [c for c in library.get_editor_property('candidates')
                       if c.get_editor_property('animation').get_path_name().split('.')[0] == '/Game/Combat/Animations/AM_HitReaction_Front']
            if len(matches) != 1 or matches[0].get_editor_property('candidate_id') != policy.get_editor_property('candidate_id'):
                raise ValueError('Front alias is missing/ambiguous; specify Candidate=<actual CandidateId>')
        elif str(policy.get_editor_property('candidate_id')) != alias:
            raise ValueError('Candidate does not match the bound policy')
        provenance = source_check(library, opts.get('analysistask'), policy.get_editor_property('candidate_id'))
        if policy_name == 'Equal':
            original = policy
            policy = unreal.new_object(unreal.ReactionWarpPolicy)
            for prop in ('schema_version', 'policy_version', 'candidate_id', 'source_hash', 'analysis_hash', 'analysis_binding',
                         'approved_policy_hash', 'lateral_limit_cm', 'smoothness_weight', 'phases'):
                policy.set_editor_property(prop, original.get_editor_property(prop))
            phases = policy.get_editor_property('phases')
            for phase in phases:
                phase.set_editor_property('preservation_weight', 1.)
            policy.set_editor_property('phases', phases)
        data = inspect(library, policy, float(opts.get('budgetcm', '80')), opts.get('mode', 'MultiPhase'))
        export_plan(directory, data)
        provenance.update({'cooked_track_error': data['cookedTrackError'], 'runtime_policy_error': data['runtimePolicyError'],
                           'candidate_id': str(policy.get_editor_property('candidate_id')), 'alias': alias,
                           'max_lateral_cm': data['plan']['maxLateralCm'], 'max_lateral_time': data['plan']['maxLateralTime'],
                           'naturalness': 'user evaluation', 'owner_fixture': 'BLOCKED'})
        write(directory / 'validation.json', provenance)
        if data['cookedTrackError']:
            raise ValueError(data['cookedTrackError'])
        finish(directory, 'PASS diagnostics DRAFT: ' + data['plan']['reason'] + '; runtime policy: ' + data['runtimePolicyError'])
        return directory
    except Exception as error:
        write(directory / 'failure.json', {'status': 'FAIL', 'reason': str(error)})
        finish(directory, 'FAIL: ' + str(error))
        raise
