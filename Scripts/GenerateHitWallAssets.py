"""UnrealEditor-Cmd <project> -run=pythonscript -script=<absolute script path>.

Creates an isolated playable map. Existing generated BPs/maps are never overwritten.
Animation setup adds named sockets only when absent on the shared mannequin skeleton.
"""
import json
import sys
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(PROJECT / 'Scripts' / 'Analysis'))
from task_readme import describe_task, task_id

OUTPUT = PROJECT / 'Saved' / 'Tasks' / task_id('hitwall-assets')
describe_task(OUTPUT, 'HitWall assets', '공격 Montage, Notify, Socket, 테스트 맵 생성',
              'Scripts/GenerateHitWallAssets.py via Unreal Python', ['manifest.json: 생성 애셋과 검증 결과'])
ROOT = '/Game/RootMotionHitWall'
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary


def blueprint(name, parent):
    path = f'{ROOT}/Blueprints/{name}'
    if LIB.does_asset_exist(path):
        raise RuntimeError(f'Existing generated Blueprint preserved: {path}')
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    asset = TOOLS.create_asset(name, f'{ROOT}/Blueprints', unreal.Blueprint, factory)
    if not asset:
        raise RuntimeError(f'Failed to create {path}')
    return asset, unreal.get_default_object(asset.generated_class())


def main():
    if LIB.does_asset_exist(f'{ROOT}/Maps/L_HitWallBaseline'):
        raise RuntimeError('Existing map preserved; use a fresh destination for regeneration.')
    if not unreal.HitWallAssetSetup.prepare_animation_assets():
        raise RuntimeError('Native animation asset setup failed')
    anim_class = LIB.load_blueprint_class(f'{ROOT}/Animations/ABP_HitWall')
    mesh = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    source = unreal.get_default_object(LIB.load_blueprint_class('/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter'))
    player_bp, player = blueprint('BP_HitWallPlayer', unreal.HitWallPlayerCharacter)
    for key in ('jump_action', 'move_action', 'look_action', 'mouse_look_action'):
        player.set_editor_property(key, source.get_editor_property(key))
    player.set_editor_property('attack_montages', [unreal.load_asset(f'{ROOT}/Animations/AM_PlayerAttack_{i:02d}') for i in range(1, 4)])
    action = TOOLS.create_asset('IA_HitWallAttack', f'{ROOT}/Input', unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
    context = TOOLS.create_asset('IMC_HitWall', f'{ROOT}/Input', unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    key = unreal.Key()
    key.set_editor_property('key_name', 'LeftMouseButton')
    context.map_key(action, key)
    player.set_editor_property('attack_action', action)
    enemy_bp, enemy = blueprint('BP_HitWallEnemy', unreal.HitWallEnemyCharacter)
    enemy.set_editor_property('hit_montage', unreal.load_asset(f'{ROOT}/Animations/AM_HitReaction_Front'))
    for obj in (player, enemy):
        obj.mesh.set_skeletal_mesh_asset(mesh)
        obj.mesh.set_relative_location(unreal.Vector(0, 0, -96), False, False)
        obj.mesh.set_relative_rotation(unreal.Rotator(0, 0, -90), False, False)
        obj.mesh.set_anim_instance_class(anim_class)
    # Same default mapping contexts as the existing Player, without the LAN game-mode wait.
    pc_bp, pc = blueprint('BP_HitWallController', unreal.GE_AIReplicationPlayerController)
    pc.set_editor_property('default_mapping_contexts', [unreal.load_asset('/Game/Input/IMC_Default'), unreal.load_asset('/Game/Input/IMC_MouseLook')])
    mode_bp, mode = blueprint('BP_HitWallGameMode', unreal.GameModeBase)
    mode.set_editor_property('default_pawn_class', player_bp.generated_class())
    mode.set_editor_property('player_controller_class', pc_bp.generated_class())
    for asset in (action, context, player_bp, enemy_bp, pc_bp, mode_bp):
        if isinstance(asset, unreal.Blueprint):
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        if not LIB.save_loaded_asset(asset):
            raise RuntimeError(f'Save failed: {asset.get_path_name()}')
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(f'{ROOT}/Maps/L_HitWallBaseline'):
        raise RuntimeError('Map creation failed')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', mode_bp.generated_class())
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    for label, location, scale in [('Floor', (0, 0, -50), (30, 20, 1)), ('ReactionWall', (130, 0, 150), (0.3, 6, 3))]:
        obj = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
        obj.set_actor_label(label)
        if label == 'ReactionWall':
            obj.set_editor_property('tags', ['HitWallWall'])
        obj.static_mesh_component.set_static_mesh(cube)
        obj.set_actor_scale3d(unreal.Vector(*scale))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-155, 0, 100), unreal.Rotator(0, 0, 0))
    foe = actors.spawn_actor_from_class(enemy_bp.generated_class(), unreal.Vector(0, 0, 96), unreal.Rotator(0, 0, 180))
    foe.set_actor_label('HitWall Enemy')
    actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, 0, -30))
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 400))
    if not levels.save_current_level():
        raise RuntimeError('Map save failed')
    manifest = {'map': f'{ROOT}/Maps/L_HitWallBaseline', 'status': 'generated',
                'note': 'Notify frames and limb sockets require pose validation; runtime smoke test follows.'}
    (OUTPUT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('HITWALL_ASSETS_OK ' + str(OUTPUT))


try:
    main()
except Exception as error:
    with (OUTPUT / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write(f'\n결과: 실패. {type(error).__name__}: {error}\n에디터 로그와 함께 원인 확인용으로 보관합니다.\n')
    raise
else:
    with (OUTPUT / 'README.md').open('a', encoding='utf-8') as stream:
        stream.write('\n결과: 애셋 생성 성공. manifest.json의 맵에서 실행하고 TestHitWall.ps1로 검증합니다.\n')
