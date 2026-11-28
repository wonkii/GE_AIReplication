"""Offline orchestration checks, using synthetic CSVs and a mocked editor, not UE."""
import builtins
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile
from test_local_rubberband_events import fixture
from local_rubberband_events import analyze, write
from local_rubberband_metrics import keyvalues

script = Path(__file__).resolve().parents[1]/'RunLocalRubberband.py'
spec = importlib.util.spec_from_file_location('lrb_automation_test', script)
a = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {'unreal': types.SimpleNamespace(log=lambda x:None, log_error=lambda x:None, unregister_slate_post_tick_callback=lambda x:None)}):
    spec.loader.exec_module(a)


class Level:
    def __init__(self): self.playing = False
    def is_in_play_in_editor(self): return self.playing
    def editor_request_begin_play(self): self.playing = True
    def editor_request_end_play(self): self.playing = False


class Tests(unittest.TestCase):
    def test_plan(self):
        p = a.plan()
        self.assertEqual(len(p), 15)
        self.assertEqual(len([j for j in p if j['Stage']=='Common']), 7)
        self.assertEqual(len(a.plan(2,3,False)), 16)
        self.assertEqual(p[1]['Command'], 'lrb.Run N0 In 1 200')
        self.assertTrue(all(j['Direction']=='In' and j['Loops']==3 for j in p if j['Stage']=='Common'))

    def test_one_job_lifecycle_and_output(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); r=a.Runner.__new__(a.Runner)
            r.level=Level(); r.raw=root/'raw'; r.raw.mkdir(); (r.raw/'old').mkdir()
            r.output=root/'out'; r.output.mkdir(); r.jobs=a.plan()[:1]; r.i=0
            r.runs=[]; r.events=[]; r.folders=[]; r.handle=None; r.owned_pie=False; r.cancel_requested=False
            r.write=write; r.analyze=analyze; r.keyvalues=keyvalues; r.enter('start')
            r.worlds=lambda:types.SimpleNamespace(get_path_name=lambda:'client1')
            def cmd(command):
                self.assertEqual(command,'lrb.Run N0 In 1')
                folder=r.raw/'new'/'001_N0_In'; folder.mkdir(parents=True); fixture(folder)
            r.command=cmd
            r.step(); self.assertEqual(r.state,'ready'); r.step(); r.ready_since-=3
            r.step(); self.assertEqual(r.state,'running'); r.step(); self.assertEqual(r.state,'settle')
            r.entered-=3; r.step(); self.assertEqual(r.state,'ending')
            a.unreal.EditorLevelLibrary=types.SimpleNamespace(get_pie_worlds=lambda include:[])
            r.step(); self.assertEqual(r.state,'gap'); r.entered-=2; r.step(); r.step()
            report=json.loads((r.output/'status.json').read_text()); self.assertEqual(report['Status'],'COMPLETED')
            self.assertEqual(report['Complete'],1)
            with zipfile.ZipFile(r.output.with_suffix('.zip')) as z:
                self.assertTrue(any(n.startswith('Raw/new/') for n in z.namelist()))
                self.assertFalse(any('Raw/old/' in n for n in z.namelist()))
                self.assertIsNone(z.testzip())

    def test_metadata_mismatch(self):
        j=a.plan()[0]; m=dict(Condition='N0',Direction='Out',Loops='1',SyntheticDesyncCm='0',Variant='NativeBaseline')
        self.assertFalse(a.matches(m,j))

    def test_abort_does_not_restart(self):
        with tempfile.TemporaryDirectory() as t:
            r=a.Runner.__new__(a.Runner); r.level=Level(); r.level.playing=True
            r.owned_pie=True; r.selected_world='client1'; r.command=lambda c: self.assertEqual(c,'lrb.Stop')
            r.close=lambda status,reason: self.assertEqual(status,'ABORTED')
            r.abort('test'); self.assertFalse(r.level.playing); self.assertEqual(r.state,'abort_wait')
            r.step(); self.assertFalse(r.owned_pie)

if __name__=='__main__': unittest.main()
