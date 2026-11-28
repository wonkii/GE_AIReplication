"""Synthetic CSV unit tests, no UE runtime required."""
import csv
from pathlib import Path
import tempfile
import unittest
from local_rubberband_events import analyze


def put(folder, name, fields, records):
    with (folder/name).open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(records)


def fixture(folder, correction=True, synthetic=False, burst=False, gap=False, missing_marker=False):
    (folder/'condition.txt').write_text(f'Condition={"B500" if burst else "N0"}\nDirection=In\nLoops=1\nPlayerId=1\nSyntheticDesyncCm={200 if synthetic else 0}\nBurstMs={500 if burst else 0}\nVariant=NativeBaseline\n')
    for prefix in ('local','server'):
        (folder/f'{prefix}_status.txt').write_text(f'Reason=Completed\nWriterHealthy=1\nCorrectionCallbacks={int(correction) if prefix=="local" else 0}\n')
    samples=[]
    for i in range(60):
        if gap and i==11: continue
        x=i*2-(100 if correction and i>=10 else 0)
        r=dict(HostTime=i*.02,Frame=i,PlayerId='1',CameraValid='1')
        for prefix in ('Capsule','Visual','Camera'):
            r.update({prefix+'X':x,prefix+'Y':0,prefix+'Z':0})
        samples.append(r)
    for prefix in ('local','server'):
        put(folder,prefix+'_samples.csv',list(samples[0]),samples)
    put(folder,'local_corrections.csv',['HostTime','Frame','EventId'],[dict(HostTime=.19,Frame=10,EventId=1)] if correction else [])
    put(folder,'local_replays.csv',['HostTime','Frame','FirstEvent','LastEvent','NetShiftCm','ReplayResult'],[dict(HostTime=.195,Frame=10,FirstEvent=1,LastEvent=1,NetShiftCm=100,ReplayResult=1)] if correction else [])
    script=[dict(HostTime=0,Frame=0,Event='ScriptStart'),dict(HostTime=1,Frame=50,Event='ScriptStop:Completed')]
    if synthetic: script.append(dict(HostTime=.18,Frame=9,Event='SyntheticDesync:1:200.00'))
    if burst:
        script.append(dict(HostTime=.1,Frame=5,Event='BurstStart'))
        if not missing_marker: script.append(dict(HostTime=.62,Frame=31,Event='BurstEnd'))
    put(folder,'local_script.csv',['HostTime','Frame','Event'],script)


class Diagnostics(unittest.TestCase):
    def run_case(self, **kwargs):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); fixture(folder,**kwargs); return analyze(folder)
    def test_native_event(self):
        r,e=self.run_case(); self.assertEqual(r['Complete'],1); self.assertEqual(r['UsableWindows'],1); self.assertEqual(e[0]['VisualPeakStepCm'],98)
    def test_no_correction(self):
        r,e=self.run_case(correction=False); self.assertEqual(r['Complete'],1); self.assertEqual(e,[]); self.assertEqual(r['VisualWindowPeakStepMaxCm'],'')
    def test_synthetic_excluded(self):
        r,e=self.run_case(synthetic=True); self.assertEqual(r['Complete'],1); self.assertEqual(e[0]['SyntheticOverlap'],1); self.assertEqual(r['UsableWindows'],0)
    def test_gap(self):
        r,e=self.run_case(gap=True); self.assertEqual(e[0]['UsableWindow'],0)
    def test_burst(self):
        r,e=self.run_case(burst=True); self.assertEqual(r['BurstObservedMs'],520); self.assertEqual(r['Complete'],1)
    def test_missing_marker(self):
        r,e=self.run_case(burst=True,missing_marker=True); self.assertEqual(r['Complete'],0)

if __name__=='__main__': unittest.main()
