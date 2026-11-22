"""Build a portable joint-motion comparison from existing, identity-matched trial records.
No Unreal launch and no new optimizer: planned offsets come from the C++ exported CSV.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / 'Scripts/Analysis'))
from task_readme import describe_task, task_id

MODES = ('Off', 'Uniform', 'MiddleOnly', 'MultiPhase')
JOINTS = {'root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05',
          'neck_01', 'neck_02', 'head', 'clavicle_l', 'clavicle_r', 'upperarm_l', 'upperarm_r',
          'lowerarm_l', 'lowerarm_r', 'hand_l', 'hand_r', 'thigh_l', 'thigh_r', 'calf_l', 'calf_r',
          'foot_l', 'foot_r', 'ball_l', 'ball_r'}


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def rotate(v, q):
    x, y, z, w = q
    a, b, c = v
    tx, ty, tz = 2*(y*c-z*b), 2*(z*a-x*c), 2*(x*b-y*a)
    return [a+w*tx+y*tz-z*ty, b+w*ty+z*tx-x*tz, c+w*tz+x*ty-y*tx]


def build(compare, source, probe, output=None):
    output = output or PROJECT / 'Saved/Tasks' / task_id('warp-motion-replay')
    if output.exists():
        raise ValueError('Choose a new output directory; existing reports are retained.')
    describe_task(output, '모드별 관절 이동 재생', '같은 벽 간격에서 실제 이동을 적용한 골격 비교',
                  f'motion_comparison.py --compare "{compare}" --source "{source}" --probe "{probe}"',
                  ['report.html: 오프라인 재생 문서; 브라우저에서 열기', 'replay.json: 내장한 정규화 데이터',
                   'summary.csv: trial별 실측/계획 이동 거리', 'validation.json: 데이터 연결·좌표 검증',
                   'provenance.json: 입력 경로와 SHA256; 원본과 함께 보관'])
    try:
        raw = read(source / 'export.json')
        analysis = read(source / 'analysis.json')
        snapshot = read(compare / 'input.json')
        candidate = snapshot['candidate']
        if candidate['sourceHash'] != analysis['source_hash'] or candidate['analysisHash'] != analysis['analysis_hash']:
            raise ValueError('Analysis identity differs from the compared candidate')
        trials, runtime = rows(compare / 'trials.csv'), rows(compare / 'runtime_samples.csv')
        events, frames = rows(probe / 'hit_events.csv'), rows(probe / 'hit_frames.csv')
        planned = rows(compare / 'planned_trajectory.csv')
        end = raw['samples'][-1]['root_motion_position']
        length = math.hypot(*end[:2])
        if length < 1e-6:
            raise ValueError('A nonzero planar retreat direction is required')
        d = [end[0]/length, end[1]/length]
        def local(v):
            return [v[0]*d[0]+v[1]*d[1], -v[0]*d[1]+v[1]*d[0], v[2]]
        indices = [i for i,b in enumerate(raw['bones']) if b['name'] in JOINTS]
        edges = []
        for i in indices:
            parent = raw['bones'][i]['parent']
            while parent >= 0 and parent not in indices:
                parent = raw['bones'][parent]['parent']
            if parent >= 0:
                edges.append([indices.index(parent), indices.index(i)])
        # Root-relative joints have already had cumulative root translation removed.
        # Restore root orientation once, then add either measured or planned translation once.
        poses = [[local(rotate(s['joints'][i]['relative'], s['root_rotation'])) for i in indices] for s in raw['samples']]
        tracks, summary = [], []
        files = [source/'export.json', source/'analysis.json', compare/'input.json', compare/'trials.csv',
                 compare/'runtime_samples.csv', compare/'planned_trajectory.csv', probe/'hit_frames.csv', probe/'hit_events.csv']
        for trial in trials:
            rid, ph = trial['reaction_id'], trial['plan_hash']
            if trial['status'] not in ('Completed', 'Fallback'):
                continue
            rr = [r for r in runtime if r['reaction_id']==rid and r['plan_hash']==ph]
            if not rr:
                raise ValueError('Missing runtime rows for trial '+trial['trial'])
            actors = {r['actor'] for r in rr}
            if len(actors)!=1 or {r['mode'] for r in rr}!={trial['mode']}:
                raise ValueError('Ambiguous runtime identity')
            actor = next(iter(actors))
            starts = [r for r in events if r['reaction_id']==rid and r['actor']==actor and r['event']=='start']
            if len(starts)!=1:
                raise ValueError('Probe event identity mismatch')
            event = starts[0]
            ff = sorted([r for r in frames if r['reaction_id']==rid and r['actor']==actor], key=lambda r:float(r['world_t']))
            if not ff:
                raise ValueError('Missing pose-time/frame observations')
            # The probe directory must be from this exact run, not another run
            # which happened to reuse reaction IDs and actor names.
            for r in rr:
                f = min(ff, key=lambda f:abs(float(f['server_t'])-float(r['server_time'])))
                if abs(float(f['server_t'])-float(r['server_time'])) > .002 or any(
                    abs(float(f[k])-float(r[k])) > .01 for k in ('cap_x','cap_y','cap_z')):
                    raise ValueError('Probe time/position differs from comparison runtime')
            yaw = math.radians(float(event['facing_yaw']))
            world_d = [-math.cos(yaw), -math.sin(yaw)]
            start = [float(event[k]) for k in ('x','y','z')]
            floor = float(ff[0]['floor_z'])
            radius, half = float(event['cap_radius']), float(event['cap_half_height'])
            t0 = float(event['world_t'])
            def world(v):
                a,b = v[0]-start[0], v[1]-start[1]
                return [a*world_d[0]+b*world_d[1], -a*world_d[1]+b*world_d[0], v[2]-floor]
            observations = []
            seen_pose = False
            for f in ff:
                t = max(0., float(f['world_t'])-t0)
                asset = float(f['montage_pos'])
                if asset >= 0:
                    seen_pose = True
                else:
                    asset = raw['duration'] if seen_pose else 0.
                cap = world([float(f[k]) for k in ('cap_x','cap_y','cap_z')])
                mesh = world([float(f[k]) for k in ('mesh_x','mesh_y','mesh_z')])
                feet = []
                for bone in ('ball_l','ball_r'):
                    feet.append(world([float(f[bone+'_'+a]) for a in 'xyz']) if f[bone+'_x'] else None)
                observations.append({'t':t,'asset':min(asset,raw['duration']), 'cap':cap, 'mesh':mesh,
                                     'feet':feet, 'wallHits':int(f['wall_hits']), 'overlap':int(f['overlap'])})
            plan_file = compare / ('plan-'+int(trial['trial']).__format__('04d')+'.json')
            plan = read(plan_file)
            if plan['planHash']!=ph or plan['reactionId']!=int(rid):
                raise ValueError('Plan does not match runtime')
            files.append(plan_file)
            pp = [r for r in planned if r['trial']==trial['trial'] and r['reaction_id']==rid]
            if not pp:
                raise ValueError('Missing C++ trajectory')
            pp = [{'t':float(r['time']), 'p':local([float(r['planned_x']),float(r['planned_y']),0]),
                   'scale':float(r['scale'])} for r in pp]
            observed_gap = float(event['sweep_dist'])
            requested_gap = float(trial['gap_cm'])
            if abs(observed_gap-requested_gap)>2.1:
                raise ValueError('Measured wall does not match requested gap')
            track = {'mode':trial['mode'], 'gap':requested_gap, 'rep':int(trial['rep']), 'trial':int(trial['trial']),
                     'reactionId':int(rid), 'status':trial['status'], 'reason':trial['reason'], 'planHash':ph,
                     'radius':radius, 'half':half, 'wall':radius+requested_gap, 'observedGap':observed_gap,
                     'samples':observations,'planned':pp,'keys':plan['keys'], 'phaseTravel':plan['phaseTravel']}
            tracks.append(track)
            summary.append({'trial':track['trial'],'mode':track['mode'],'gap_cm':requested_gap,'rep':track['rep']+1,
                            'status':track['status'],'actual_final_cm':observations[-1]['cap'][0],
                            'actual_max_cm':max(o['cap'][0] for o in observations),
                            'planned_final_cm':pp[-1]['p'][0] if trial['status']!='Fallback' else '',
                            'wall_contact_frames':sum(o['wallHits']>0 for o in observations), 'plan_hash':ph})
        identities = {(r['gap'],r['rep'],r['mode']) for r in tracks}
        groups = {(r['gap'],r['rep']) for r in tracks}
        if len(identities)!=len(tracks) or any((g,r,m) not in identities for g,r in groups for m in MODES):
            raise ValueError('Every gap/repetition must contain exactly four modes')
        payload = {'modes':MODES,'names':[raw['bones'][i]['name'] for i in indices], 'edges':edges,
                   'poseTimes':[s['asset_time'] for s in raw['samples']], 'poses':poses, 'duration':raw['duration'],
                   'phases':analysis['segments'], 'tracks':tracks,'sourceHash':analysis['source_hash']}
        encoded = json.dumps(payload,ensure_ascii=False,allow_nan=False,separators=(',',':'))
        (output/'replay.json').write_text(encoded,encoding='utf-8')
        template = Path(__file__).with_name('motion_comparison.html').read_text(encoding='utf-8')
        (output/'report.html').write_text(template.replace('__DATA__',encoded.replace('<','\\u003c')),encoding='utf-8')
        with (output/'summary.csv').open('w',encoding='utf-8-sig',newline='') as stream:
            writer = csv.DictWriter(stream,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
        provenance = {str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
        (output/'provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf-8')
        checks = {'passed':True,'trials':len(tracks),'groups':len(groups),'joint_count':len(indices),
                  'checks':['source/analysis hashes match','trial/reaction/actor/plan identities match',
                            'probe server time and Capsule position match runtime rows',
                            'four modes per condition','wall gap measured within 2.1cm (probe uses shrunken capsule)',
                            'root translation removed from pose before one measured/planned translation is added'],
                  'limits':['Full pose is reconstructed from source joints, not captured runtime IK/blend.',
                            'C++ plan trajectory excludes collision/blend. Fallback has no feasible warped plan.',
                            'Runtime records predate the final blend-reference code refinement.']}
        (output/'validation.json').write_text(json.dumps(checks,indent=2),encoding='utf-8')
        with (output/'README.md').open('a',encoding='utf-8') as stream:
            stream.write('\n결과 / Outcome: PASS data assembly; '+str(len(tracks))+' trials. Browser QA recorded separately.\n')
        return output
    except Exception as error:
        (output/'validation.json').write_text(json.dumps({'passed':False,'error':str(error)}),encoding='utf-8')
        with (output/'README.md').open('a',encoding='utf-8') as stream: stream.write('\nOutcome: FAIL '+str(error)+'\n')
        raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('compare','source','probe'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    print(build(args.compare,args.source,args.probe,args.output).resolve())
