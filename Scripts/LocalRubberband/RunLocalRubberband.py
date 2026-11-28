"""Run in Unreal Editor (not external Python). Existing SCB assets only.
Save All; set PIE to Play As Client / 2 players / single process / no editor network emulation.
Execute once in edit mode. No asset, C++ or remote-research files are changed.
"""
import builtins
import json
import sys
import time
import traceback
import uuid
import zipfile
from pathlib import Path
from datetime import datetime
import unreal

# One independent PIE session per condition; change repeat count only for formal experiments.
COMMON_LOOPS = 3
INDEPENDENT_REPEATS = 1
INCLUDE_BURSTS = True
MAP = '/Game/SCB/LocalRubberband/Lv_LocalRubberband'
PREFIX = '/Game/SCB/LocalRubberband/'
KEY = '_local_rubberband_one_click_runner'
COMMON = ('N0', 'L100', 'L200', 'L300', 'J50-150', 'J100-300', 'L200-P3')
BURSTS = ('B500', 'B1000', 'B2000')


def plan(repeats=1, loops=3, bursts=True):
    assert 1 <= repeats <= 100 and 1 <= loops <= 100
    jobs = [('Smoke', 'N0', 'In', 1, 0, 1), ('Synthetic', 'N0', 'In', 1, 200, 1)]
    for r in range(1, repeats + 1):
        jobs.extend(('Common', c, 'In', loops, 0, r) for c in COMMON)
        if bursts:
            jobs.extend(('Burst', c, d, 1, 0, r) for d in ('Out', 'Both') for c in BURSTS)
    return [dict(Stage=s, Condition=c, Direction=d, Loops=n, SyntheticDesyncCm=x,
                 Repeat=r, Command=f'lrb.Run {c} {d} {n}' + (f' {x}' if x else ''))
            for s,c,d,n,x,r in jobs]


def child_dirs(root):
    return {p for p in root.iterdir() if p.is_dir()} if root.is_dir() else set()


def matches(meta, job):
    return (meta.get('Condition') == job['Condition'] and meta.get('Direction') == job['Direction']
            and int(meta['Loops']) == job['Loops']
            and float(meta['SyntheticDesyncCm']) == job['SyntheticDesyncCm']
            and meta.get('Variant') == 'NativeBaseline')


def log(msg):
    unreal.log('[LRB Auto] ' + str(msg))


def require(ok, msg):
    if not ok:
        raise RuntimeError(msg)


class Runner:
    def __init__(self):
        self.level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        self.editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
        self.saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
        self.raw = self.saved / 'Experiments/LocalRubberband'
        stamp = datetime.now().strftime('%Y%m%d_%H%M%S') + '_' + uuid.uuid4().hex[:8]
        self.output = self.saved / 'LocalRubberbandAnalysis/Auto' / stamp
        self.jobs = plan(INDEPENDENT_REPEATS, COMMON_LOOPS, INCLUDE_BURSTS)
        self.i = 0
        self.runs, self.events, self.folders = [], [], []
        self.handle = None
        self.owned_pie = False
        self.cancel_requested = False
        self.state = 'start'
        self.last_poll = 0
        self.entered = time.monotonic()
        self.ready_since = None
        self.saw_pie = False
        self.batch = None
        self.folder = None
        self.selected_world = None
        self.final_state = None
        self.final_reason = ''

    def enter(self, state):
        self.state, self.entered = state, time.monotonic()

    def preflight(self):
        require(not self.level.is_in_play_in_editor(), 'Stop current PIE before running.')
        util = unreal.EditorLoadingAndSavingUtils
        require(not util.get_dirty_map_packages() and not util.get_dirty_content_packages(),
                'Unsaved assets/levels. Save All first; no assets were changed by automation.')
        for path in (MAP, PREFIX+'BP_LocalRubberbandCharacter_Clean', PREFIX+'BP_LocalRubberbandGameMode'):
            require(unreal.EditorAssetLibrary.does_asset_exist(path), 'Missing existing asset: '+path)
        # Verify the same CDO types that the native batch runner requires. Do not reparent or save.
        cls = unreal.EditorAssetLibrary.load_blueprint_class(PREFIX+'BP_LocalRubberbandCharacter_Clean')
        require(cls, 'Cannot load Clean BP generated class')
        cdo = unreal.get_default_object(cls)
        require(isinstance(cdo, unreal.LocalRubberbandCharacter), 'Wrong Clean BP parent class')
        require(isinstance(cdo.get_editor_property('character_movement'), unreal.LocalRubberbandMovementComponent), 'Wrong CMC on Clean BP')
        require(isinstance(cdo.get_editor_property('input_script'), unreal.LocalRubberbandInputScriptComponent), 'Wrong InputScript on Clean BP')
        gm = unreal.EditorAssetLibrary.load_blueprint_class(PREFIX+'BP_LocalRubberbandGameMode')
        require(gm and unreal.get_default_object(gm).get_editor_property('default_pawn_class') == cls,
                'Local GameMode Default Pawn must be BP_LocalRubberbandCharacter_Clean')
        # PIE preferences are user-controlled. Read when accessible, never silently change them.
        try:
            sc = unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings')
            settings = unreal.get_default_object(sc)
            network = settings.get_editor_property('network_emulation_settings')
            enabled = network.get_editor_property('is_network_emulation_enabled')
        except Exception as exc:
            log('Cannot read PIE emulation preference; disable Enable Network Emulation manually: '+str(exc))
        else:
            require(not enabled, 'Disable editor Enable Network Emulation before running.')
        require(hasattr(unreal.EditorLevelLibrary, 'get_pie_worlds'), 'EditorLevelLibrary.get_pie_worlds not available; enable Editor Scripting Utilities if needed')
        analysis = self.project / 'Scripts/Analysis'
        require((analysis/'local_rubberband_events.py').is_file() and (analysis/'local_rubberband_metrics.py').is_file(), 'Missing existing Stage 1/2 analysis scripts')
        sys.path.insert(0, str(analysis))
        from local_rubberband_events import analyze, write
        from local_rubberband_metrics import keyvalues
        self.analyze, self.write, self.keyvalues = analyze, write, keyvalues
        self.output.mkdir(parents=True, exist_ok=False)
        (self.output/'plan.json').write_text(json.dumps(self.jobs, indent=2), encoding='utf-8')
        world = self.editor.get_editor_world()
        current_map = world.get_path_name().split('.')[0] if world else ''
        del world
        if current_map != MAP:
            require(self.level.load_level(MAP), 'Unable to open existing level '+MAP)
        log('START: '+str(len(self.jobs))+' independent PIE runs; '+str(self.output))
        self.report('RUNNING', '')

    def worlds(self):
        # Do not retain UObject references across PIE shutdown.
        worlds = list(unreal.EditorLevelLibrary.get_pie_worlds(True))
        servers = [w for w in worlds if unreal.SystemLibrary.is_dedicated_server(w)]
        clients = [w for w in worlds if not unreal.SystemLibrary.is_server(w)]
        if len(worlds) != 3 or len(servers) != 1 or len(clients) != 2:
            return None
        if any('Lv_LocalRubberband' not in w.get_path_name() for w in worlds):
            raise RuntimeError('Wrong PIE map')
        clients.sort(key=lambda w: w.get_path_name())
        for w in clients:
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            pawn = pc.get_controlled_pawn() if pc else None
            if not pawn or not pawn.is_locally_controlled():
                return None
            require(isinstance(pawn, unreal.LocalRubberbandCharacter), 'PIE spawned wrong pawn; check World Settings GameMode')
            require(isinstance(pawn.get_editor_property('input_script'), unreal.LocalRubberbandInputScriptComponent), 'PIE pawn InputScript invalid')
            if not pawn.get_editor_property('player_state'):
                return None
        # Wait for both authoritative pawns to exist before the existing native command.
        server_pawns = unreal.GameplayStatics.get_all_actors_of_class(servers[0], unreal.LocalRubberbandCharacter)
        if sum(bool(p.get_editor_property('player_state')) for p in server_pawns) < 2:
            return None
        return clients[0]

    def command(self, command):
        # Re-resolve the specific client world; never run in the editor/server world.
        for w in unreal.EditorLevelLibrary.get_pie_worlds(False):
            if w.get_path_name() == self.selected_world and not unreal.SystemLibrary.is_server(w):
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                require(pc, 'Selected client controller missing')
                unreal.SystemLibrary.execute_console_command(w, command, pc)
                return
        raise RuntimeError('Selected client world no longer exists')

    def report(self, status, reason):
        # Rewrite small summaries only. Raw logs remain in their original unique batch folders.
        self.write(self.output/'runs.csv', self.runs, ['Job','Stage','Repeat','Run','Complete','Diagnostic'])
        self.write(self.output/'events.csv', self.events, ['Job','Stage','Repeat','Run','FirstEvent','LastEvent','UsableWindow'])
        data = dict(Status=status, Reason=reason, Planned=len(self.jobs),
                    Analyzed=len(self.runs), Complete=sum(int(r.get('Complete',0)) for r in self.runs),
                    NativeCorrectionRuns=sum(int(r.get('CorrectionCallbacks',0)) > 0 for r in self.runs),
                    UsableWindows=sum(int(e.get('UsableWindow',0)) for e in self.events),
                    RawFolders=[str(p) for p in self.folders])
        (self.output/'status.json').write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf-8')

    def close(self, status, reason):
        try:
            self._write_final(status, reason)
        finally:
            if self.handle is not None:
                unreal.unregister_slate_post_tick_callback(self.handle)
                self.handle = None
            if getattr(builtins, KEY, None) is self:
                delattr(builtins, KEY)

    def _write_final(self, status, reason):
        # Collect a partial current run only if it is uniquely attributable to this job.
        if self.folder is None and self.batch is not None:
            candidates = list(self.batch.glob('*/condition.txt'))
            if len(candidates) == 1:
                try:
                    if matches(self.keyvalues(candidates[0]), self.job):
                        self.folders.append(candidates[0].parent)
                except (OSError, ValueError, KeyError):
                    pass
        self.report(status, reason)
        # Bundle only this invocation's outputs, not old results or the remote study.
        archive = self.output.with_suffix('.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for p in sorted(self.output.iterdir()):
                if p.is_file():
                    z.write(p, 'Analysis/'+p.name)
            for f in dict.fromkeys(self.folders):
                for p in sorted(f.rglob('*')):
                    if p.is_file():
                        z.write(p, 'Raw/'+p.relative_to(self.raw).as_posix())
        log(f'{status}: {reason}; {archive}')

    def abort(self, reason):
        self.final_state, self.final_reason = 'ABORTED', reason
        unreal.log_error('[LRB Auto] '+reason)
        if self.owned_pie and self.level.is_in_play_in_editor():
            if self.selected_world:
                try:
                    self.command('lrb.Stop')
                except Exception as exc:
                    log('Stop command failed; ending owned PIE: '+str(exc))
            self.level.editor_request_end_play()
            self.enter('abort_wait')
        else:
            self.close(self.final_state, self.final_reason)

    def step(self):
        now = time.monotonic()
        elapsed = now-self.entered
        if self.state == 'abort_wait':
            if not self.level.is_in_play_in_editor():
                self.owned_pie = False
                self.close(self.final_state, self.final_reason)
            elif elapsed > 30:
                self.close('ABORTED', self.final_reason+'; PIE did not stop: stop manually; raw data may be partial')
            return
        if self.cancel_requested:
            self.abort('Cancelled by user'); return
        if self.state == 'start':
            if self.i >= len(self.jobs):
                complete = len(self.runs) == len(self.jobs) and all(r.get('Complete') == 1 for r in self.runs)
                self.close('COMPLETED' if complete else 'INCOMPLETE', 'Plan finished; corrections are observations, not pass/fail'); return
            require(not self.level.is_in_play_in_editor(), 'Unexpected PIE session; stop it manually')
            self.job = self.jobs[self.i]
            self.before = child_dirs(self.raw)
            self.batch = self.folder = self.selected_world = None
            self.ready_since = None
            self.saw_pie = False
            log(f'{self.i+1}/{len(self.jobs)} {self.job["Stage"]}: {self.job["Command"]}')
            self.level.editor_request_begin_play()
            self.owned_pie = True
            self.enter('ready'); return
        if self.state == 'ready':
            require(elapsed < 90, 'PIE startup timeout. Expected dedicated server + 2 clients, single process, Play As Client.')
            playing = self.level.is_in_play_in_editor()
            if self.saw_pie and not playing:
                self.abort('PIE stopped during startup'); return
            self.saw_pie = self.saw_pie or playing
            w = self.worlds() if playing else None
            if not w:
                self.ready_since = None; return
            if self.ready_since is None:
                self.ready_since = now; return
            if now-self.ready_since < 2:
                return
            self.selected_world = w.get_path_name()
            self.command(self.job['Command'])
            self.enter('running'); return
        if self.state in ('running','settle'):
            require(self.level.is_in_play_in_editor(), 'PIE stopped before experiment finished')
            if self.state == 'running':
                if not self.batch:
                    new = child_dirs(self.raw)-self.before
                    require(len(new) <= 1, 'Unexpected multiple new batches; no old/manual logs will be mixed')
                    if not new:
                        require(elapsed < 25, 'No batch output. Check lrb.Run command and native LogLocalRubberbandBatch errors.'); return
                    self.batch = next(iter(new))
                if not self.folder:
                    candidates = list(self.batch.glob('*/condition.txt'))
                    require(len(candidates) <= 1, 'Unexpected multiple jobs in single-run batch')
                    if candidates:
                        require(matches(self.keyvalues(candidates[0]), self.job), 'New condition metadata does not match requested job')
                        self.folder = candidates[0].parent
                        self.folders.append(self.folder)
                if self.folder and all((self.folder/n).is_file() for n in ('local_status.txt','server_status.txt')):
                    # Finish() follows completed job recording by ~1s. Give it time to restore emulation.
                    self.enter('settle'); return
                require(elapsed < 90+45*self.job['Loops'], 'Experiment timed out; check movement, FPS and log output'); return
            if elapsed < 2:
                return
            # Analyze only after both native recorders have closed, with no prior-run scan.
            try:
                row, ev = self.analyze(self.folder)
            except Exception as exc:
                row, ev = dict(Run=str(self.folder), Complete=0, Diagnostic=str(exc)), []
            tags = dict(Job=self.i+1, Stage=self.job['Stage'], Repeat=self.job['Repeat'])
            meta = self.keyvalues(self.folder/'condition.txt')
            self.runs.append({**row, **tags, 'InitialClientLocation':meta.get('InitialClientLocation',''),
                              'PlayerId':meta.get('PlayerId',''), 'ClientWorld':self.selected_world})
            self.events.extend({**e, **tags} for e in ev)
            self.report('RUNNING','')
            log(f'Job {self.i+1}: complete={row.get("Complete")}, corrections={row.get("CorrectionCallbacks", "n/a")}')
            if not row.get('Complete'):
                self.abort('Incomplete log or failed validation; inspect this job before proceeding.'); return
            self.level.editor_request_end_play()
            self.enter('ending'); return
        if self.state == 'ending':
            require(elapsed < 30, 'PIE shutdown timeout')
            if not self.level.is_in_play_in_editor() and not unreal.EditorLevelLibrary.get_pie_worlds(True):
                self.owned_pie = False
                self.i += 1
                self.enter('gap')
            return
        if self.state == 'gap' and elapsed >= 1:
            self.enter('start')

    def tick(self, _delta):
        now = time.monotonic()
        if now-self.last_poll < .25:
            return
        self.last_poll = now
        try:
            self.step()
        except Exception:
            reason = traceback.format_exc()
            if self.handle is None or self.state == 'abort_wait':
                unreal.log_error('[LRB Auto] '+reason)
                if self.handle is not None:
                    unreal.unregister_slate_post_tick_callback(self.handle)
                    self.handle = None
                if getattr(builtins, KEY, None) is self:
                    delattr(builtins, KEY)
            else:
                self.abort(reason)


def main():
    if getattr(builtins, KEY, None) is not None:
        unreal.log_warning('[LRB Auto] Already running. Stop PIE or set builtins.'+KEY+'.cancel_requested=True'); return
    runner = Runner()
    runner.preflight()
    setattr(builtins, KEY, runner)
    try:
        runner.handle = unreal.register_slate_post_tick_callback(runner.tick)
    except Exception:
        delattr(builtins, KEY)
        raise


if __name__ == '__main__':
    main()
