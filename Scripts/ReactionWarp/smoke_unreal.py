"""W0-W2 commandlet test. Never saves original Library/animation assets."""
import json
from pathlib import Path
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(ROOT / 'Scripts/ReactionWarp'))
import reaction_warp as driver


def main():
    summary = driver.task('Scripts/TestReactionWarp.ps1 - offline integration')
    results = {}
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        def command(text):
            previous = driver.LAST_TASK
            unreal.SystemLibrary.execute_console_command(world, text)
            current = driver.LAST_TASK
            assert current != previous, 'Command did not dispatch: ' + text
            assert not (current / 'failure.json').exists(), (current / 'failure.json').read_text() if (current / 'failure.json').exists() else ''
            return current
        if not unreal.EditorAssetLibrary.does_asset_exist(driver.DEFAULT_POLICY):
            command('ReactionWarp.Migrate ' + driver.DEFAULT_LIBRARY + ' NewPolicy=' + driver.DEFAULT_POLICY)
        tests = command('ReactionWarp.SelfTest')
        assert driver.read(tests / 'selftest.json')['passed']
        results['selftest'] = str(tests)
        validation = command('ReactionWarp.Validate ' + driver.DEFAULT_LIBRARY)
        data = driver.read(validation / 'validation.json')
        assert not data['cooked_track_error'], data
        assert data['runtime_policy_error'], 'Draft was unexpectedly approved'
        results['validation'] = data
        for mode in ('Off', 'Uniform', 'MiddleOnly', 'MultiPhase'):
            directory = command('ReactionWarp.Plan Candidate=Front BudgetCm=100 Mode=' + mode)
            assert (directory / 'report.html').exists()
            results[mode] = driver.read(directory / 'plan.json')
        equal = command('ReactionWarp.Plan Candidate=Front BudgetCm=100 Policy=Equal')
        results['equal_cost'] = driver.read(equal / 'plan.json')
        assert results['MultiPhase']['result'] in ('Planned', 'Unmodified'), results['MultiPhase']
        driver.write(summary / 'integration.json', {'passed': True, 'results': results})
        driver.finish(summary, 'PASS Integration: source hash, migration, C++ tests, four modes, equal-cost policy and reports')
        unreal.log('[ReactionWarp] PASS Integration')
    except Exception as error:
        driver.write(summary / 'integration.json', {'passed': False, 'error': str(error), 'results': results})
        driver.finish(summary, 'FAIL Integration: ' + str(error))
        raise


main()
