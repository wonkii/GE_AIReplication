#!/usr/bin/env python3
"""Local baseline diagnostics: correction-linked sampled motion, not a rubber-band detector.
Uses the existing local CSVs only. Standard library. Outputs per-run and per-replay CSVs.
"""
import argparse
import csv
import math
from pathlib import Path
from local_rubberband_metrics import rows, keyvalues, summarize, vec, quantile


def stamp(r):
    t = float(r['HostTime'])
    if not math.isfinite(t):
        raise ValueError('Nonfinite HostTime')
    return t


def frame(r):
    return int(r['Frame'])


def analyze(folder, tail_ms=250):
    base = summarize(folder)
    meta = keyvalues(folder / 'condition.txt')
    samples = sorted(rows(folder / 'local_samples.csv'), key=stamp)
    corrections = rows(folder / 'local_corrections.csv')
    replays = sorted(rows(folder / 'local_replays.csv'), key=stamp)
    script = sorted(rows(folder / 'local_script.csv'), key=stamp)
    by_id = {int(r['EventId']): r for r in corrections}
    if len(by_id) != len(corrections):
        raise ValueError('Duplicate correction ID')
    burst_start = [stamp(r) for r in script if r['Event'] == 'BurstStart']
    burst_end = [stamp(r) for r in script if r['Event'] == 'BurstEnd']
    injected = [r for r in script if r['Event'].startswith('SyntheticDesync:')]
    burst_ms = int(meta.get('BurstMs', '0'))
    burst_ok = not burst_ms or (len(burst_start) == len(burst_end) == 1 and burst_end[0] > burst_start[0])
    synthetic_ok = float(meta.get('SyntheticDesyncCm', '0')) == 0 or (
        len(injected) == 1 and injected[0]['Event'].startswith('SyntheticDesync:1:'))
    windows = []
    used_ids = set()
    for r in replays:
        first, last = int(r['FirstEvent']), int(r['LastEvent'])
        if first > last or last-first+1 > len(by_id):
            raise ValueError('Invalid replay ID range')
        ids = set(range(first, last+1))
        if ids - by_id.keys() or ids & used_ids:
            raise ValueError('Missing or reused correction ID')
        used_ids.update(ids)
        c = by_id[first]
        start, end = stamp(c), stamp(r)
        if end < start or any(not start <= stamp(by_id[i]) <= end for i in ids):
            raise ValueError('Replay chronology invalid')
        windows.append((start, end + tail_ms/1000, c, r, ids))
    complete = bool(base['Complete']) and burst_ok and synthetic_ok
    base.update(Complete=int(complete), BurstTargetMs=burst_ms,
                BurstObservedMs=round((burst_end[0]-burst_start[0])*1000, 2) if burst_ms and burst_ok else '',
                BurstMarkerOK=int(burst_ok), SyntheticMarkerOK=int(synthetic_ok),
                WindowTailMs=tail_ms, LinkedWindows=len(windows))
    events = []
    for i, (start, end, c, r, ids) in enumerate(windows):
        overlap = any(i != j and a <= end and b >= start for j,(a,b,*_) in enumerate(windows))
        overlap = overlap or any(k not in ids and start <= stamp(v) <= end for k,v in by_id.items())
        # Exclude the artificial teleport's frame/interval, not just its timestamp.
        synthetic_overlap = any(stamp(v) <= end and (stamp(v) >= start or frame(v) >= frame(c)-1) for v in injected)
        covered = any(stamp(v) < start for v in samples) and any(stamp(v) >= end for v in samples)
        item = dict(Run=str(folder), Condition=meta['Condition'], Direction=meta['Direction'],
                    SyntheticDesyncCm=meta.get('SyntheticDesyncCm','0'), FirstEvent=r['FirstEvent'], LastEvent=r['LastEvent'],
                    CallbackToReplayMs=round((stamp(r)-start)*1000, 3), ReplayShiftCm=r['NetShiftCm'],
                    ReplayResult=r['ReplayResult'], Overlap=int(overlap), SyntheticOverlap=int(synthetic_overlap),
                    WindowCovered=int(covered), RunComplete=int(complete))
        usable = complete and covered and not overlap and not synthetic_overlap
        for prefix in ('Capsule', 'Visual', 'Camera'):
            ds, rates = [], []
            skipped = 0
            for a,b in zip(samples,samples[1:]):
                ta,tb = stamp(a),stamp(b)
                if tb < start or tb > end or ta >= end:
                    continue
                dt = tb-ta
                if frame(b)-frame(a) != 1 or not 0 < dt <= .25:
                    skipped += 1
                    continue
                if prefix == 'Camera' and (a['CameraValid'] != '1' or b['CameraValid'] != '1'):
                    skipped += 1
                    continue
                d = math.dist(vec(a,prefix),vec(b,prefix))
                if math.isfinite(d):
                    ds.append(d); rates.append(d/dt)
                else:
                    skipped += 1
            item[prefix+'Pairs'] = len(ds)
            item[prefix+'SkippedPairs'] = skipped
            item[prefix+'PeakStepCm'] = round(max(ds),4) if ds else ''
            item[prefix+'PeakSpeedCmS'] = round(max(rates),4) if rates else ''
            if not ds or skipped:
                usable = False
        item['UsableWindow'] = int(usable)
        events.append(item)
    eligible = [r for r in events if r['UsableWindow']]
    base['UsableWindows'] = len(eligible)
    base['ObservedCorrectionsPerMinute'] = round(len(corrections)*60/(stamp(samples[-1])-stamp(samples[0])),3) if complete and len(samples)>1 and stamp(samples[-1])>stamp(samples[0]) else ''
    for prefix in ('Capsule','Visual','Camera'):
        vals = [r[prefix+'PeakStepCm'] for r in eligible]
        base[prefix+'WindowPeakStepP95Cm'] = quantile(vals)
        base[prefix+'WindowPeakStepMaxCm'] = max(vals,default='')
    if not complete:
        base['Diagnostic'] = 'Incomplete or missing injection markers'
    elif not corrections:
        base['Diagnostic'] = 'No native correction callback observed'
    elif not eligible:
        base['Diagnostic'] = 'Corrections observed; no isolated fully covered usable window'
    else:
        base['Diagnostic'] = 'Correction-linked sampled motion; not causal or rendered-frame measurement'
    return base, events


def write(path, data, minimum):
    fields = list(dict.fromkeys(minimum + [k for r in data for k in r]))
    with path.open('w',encoding='utf-8-sig',newline='') as f:
        w = csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(data)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--tail-ms',type=int,default=250)
    a=p.parse_args()
    if not 0<=a.tail_ms<=1000:
        p.error('--tail-ms must be 0..1000')
    paths=sorted(a.root.rglob('condition.txt'))
    if not paths:
        p.error('No condition.txt found')
    runs, events=[],[]
    for path in paths:
        try:
            r,e=analyze(path.parent,a.tail_ms); runs.append(r); events.extend(e)
        except (OSError,ValueError,KeyError,TypeError,OverflowError) as exc:
            runs.append(dict(Run=str(path.parent),Complete=0,Diagnostic=str(exc)))
    a.output.mkdir(parents=True,exist_ok=True)
    write(a.output/'runs.csv',runs,['Run','Complete','Diagnostic'])
    write(a.output/'events.csv',events,['Run','FirstEvent','LastEvent','UsableWindow'])
    print(f'{len(runs)} runs, {sum(r.get("Complete",0) for r in runs)} complete; {len(events)} replay groups; {sum(e["UsableWindow"] for e in events)} usable windows')


if __name__=='__main__':
    main()
