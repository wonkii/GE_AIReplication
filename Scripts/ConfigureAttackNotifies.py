"""Unreal Python commandlet: add missing attack Notify windows without rebuilding BPs/maps."""
import sys
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(project / 'Scripts' / 'Analysis'))
from task_readme import describe_task, task_id

output = project / 'Saved' / 'Tasks' / task_id('attack-notify-assets')
describe_task(output, 'Attack Notify windows', '기존 공격 Montage에 타격/다음 입력 Notify 구간 추가',
              'Scripts/ConfigureAttackNotifies.py via Unreal Python, -SCCProvider=None',
              ['에디터 로그의 [CombatAssets]: 각 Montage의 Notify 시작/종료와 다음 공격 인덱스'])
try:
    assert unreal.CombatAssetSetup.ensure_attack_notify_windows(), 'Attack Notify configuration failed'
except Exception as error:
    with (output / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write(f'\n결과: 실패. {error}\n검사 로그와 함께 보관합니다.\n')
    raise
else:
    with (output / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write('\n결과: 성공. 이미 존재하는 Notify의 시간/설정은 보존했습니다. 로그와 함께 기능 검증 근거로 보관합니다.\n')
    unreal.log('ATTACK_NOTIFY_ASSETS_OK ' + str(output))
