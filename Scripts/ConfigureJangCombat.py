"""UnrealEditor-Cmd <project> -run=pythonscript -script=<absolute path>.

Connect shared combat components to existing BPs, validate Lv_Jang, then remove the
retired isolated demo assets only after checking external package referencers.
Existing shared animation assets are preserved on repeat runs.
"""
import json
import sys
import stat
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(PROJECT / 'Scripts' / 'Analysis'))
from task_readme import describe_task, task_id

OUTPUT = PROJECT / 'Saved' / 'Tasks' / task_id('jang-combat-assets')
describe_task(OUTPUT, 'Lv_Jang combat integration',
              '기존 Player/Sharp Enemy에 공통 공격·피격 에셋 연결, 전용 데모 에셋 정리',
              'Scripts/ConfigureJangCombat.py via Unreal Python',
              ['manifest.json: BP 연결·레벨 검증·삭제한 전용 에셋 목록'])
LIB = unreal.EditorAssetLibrary
ROOT = '/Game/Combat'


def make_writable(asset_path):
    filename = PROJECT / 'Content' / (asset_path.split('.')[0].removeprefix('/Game/') + '.uasset')
    if filename.exists():
        filename.chmod(stat.S_IREAD | stat.S_IWRITE)


def ensure_asset(name, folder, cls, factory):
    return unreal.load_asset(f'{folder}/{name}') if LIB.does_asset_exist(f'{folder}/{name}') else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)


def configure_bp(asset_path, component_class, configure, anim_class):
    bp = unreal.load_asset(asset_path)
    obj = unreal.get_default_object(bp.generated_class())
    component = obj.get_component_by_class(component_class)
    assert component, f'Missing native component: {asset_path}'
    configure(component)
    obj.mesh.set_anim_instance_class(anim_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    make_writable(asset_path)
    assert LIB.save_loaded_asset(bp), f'Cannot save {asset_path}'


def main():
    assert unreal.CombatAssetSetup.prepare_animation_assets(), 'Animation asset preparation failed'
    action = ensure_asset('IA_MeleeAttack', f'{ROOT}/Input', unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
    context = ensure_asset('IMC_MeleeAttack', f'{ROOT}/Input', unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    if not context.get_editor_property('mappings'):
        key = unreal.Key()
        key.set_editor_property('key_name', 'LeftMouseButton')
        context.map_key(action, key)
    for asset in [action, context]:
        assert LIB.save_loaded_asset(asset)

    def attack_settings(component):
        component.set_editor_property('combat_enabled', True)
        component.set_editor_property('attack_action', action)
        component.set_editor_property('attack_mapping_context', context)
        component.set_editor_property('attack_montages', [unreal.load_asset(f'{ROOT}/Animations/AM_PlayerAttack_{i:02d}') for i in range(1, 4)])

    def reaction_settings(component):
        component.set_editor_property('combat_enabled', True)
        component.set_editor_property('hit_montage', unreal.load_asset(f'{ROOT}/Animations/AM_HitReaction_Front'))

    configure_bp('/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter', unreal.MeleeAttackComponent,
                 attack_settings, LIB.load_blueprint_class(f'{ROOT}/Animations/ABP_PlayerCombat'))
    configure_bp('/Game/SharpMovementExperiment/Blueprints/BP_SharpEnemy', unreal.HitReactionComponent,
                 reaction_settings, LIB.load_blueprint_class(f'{ROOT}/Animations/ABP_SharpCombat'))

    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert levels.load_level('/Game/JWJ/Lv_Jang')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    enemies = [a for a in actors if isinstance(a, unreal.SharpEnemyCharacter)]
    assert len(enemies) == 3, f'Unexpected existing Enemy count: {len(enemies)}'
    for enemy in enemies:
        reaction = enemy.get_component_by_class(unreal.HitReactionComponent)
        assert reaction and reaction.get_editor_property('combat_enabled')
        assert enemy.mesh.anim_class == LIB.load_blueprint_class(f'{ROOT}/Animations/ABP_SharpCombat'), f'Instance AnimBP override on {enemy.get_name()}'

    # Asset deletion is scoped to the explicitly retired demo folder. Never remove
    # original mannequin/sword assets or the existing Lv_Jang map/actors.
    old_assets = list(LIB.list_assets('/Game/RootMotionHitWall', recursive=True, include_folder=False))
    for asset_path in old_assets:
        outside = [p for p in LIB.find_package_referencers_for_asset(asset_path, False)
                   if not p.startswith('/Game/RootMotionHitWall/')]
        assert not outside, f'External references prevent demo cleanup: {asset_path}: {outside}'
    # Removed native parents make some retired BPs unloadable. After the registry
    # reference check, remove only their exact package files without loading them
    # or invoking the Editor's source-control provider (which may stage git changes).
    legacy_root = (PROJECT / 'Content' / 'RootMotionHitWall').resolve()
    assert legacy_root.parent == (PROJECT / 'Content').resolve()
    for asset_path in old_assets:
        filename = PROJECT / 'Content' / asset_path.split('.')[0].removeprefix('/Game/')
        for extension in ['.uasset', '.umap', '.uexp', '.ubulk']:
            file = filename.with_suffix(extension).resolve()
            assert file.is_relative_to(legacy_root)
            if file.exists():
                file.chmod(stat.S_IREAD | stat.S_IWRITE)
                file.unlink()
    assert not any(legacy_root.rglob('*.uasset')) and not any(legacy_root.rglob('*.umap')), 'Retired demo package files remain'
    manifest = {'map': '/Game/JWJ/Lv_Jang', 'existing_enemy_count': len(enemies),
                'player': '/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter',
                'enemy': '/Game/SharpMovementExperiment/Blueprints/BP_SharpEnemy',
                'shared_assets': list(LIB.list_assets(ROOT, True, False)), 'removed_demo_assets': old_assets,
                'map_saved': False, 'status': 'configured'}
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('JANG_COMBAT_ASSETS_OK ' + str(OUTPUT))


try:
    main()
except Exception as error:
    with (OUTPUT / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write(f'\n결과: 실패. {type(error).__name__}: {error}\n원인 확인용으로 로그와 함께 보관합니다.\n')
    raise
else:
    with (OUTPUT / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write('\n결과: BP 연결·Lv_Jang 검증·전용 데모 정리 성공. manifest.json과 실행 검증 로그를 함께 보관합니다.\n')
