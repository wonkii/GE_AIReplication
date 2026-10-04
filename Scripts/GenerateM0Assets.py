"""Run once with UnrealEditor-Cmd -ExecutePythonScript to create M0 assets."""
import unreal


ROOT = "/Game/SharpMovementExperiment"
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDITOR_ASSETS = unreal.EditorAssetLibrary


def ensure_directory(path):
    if not EDITOR_ASSETS.does_directory_exist(path):
        EDITOR_ASSETS.make_directory(path)


for subdir in ("Maps", "Blueprints", "Data"):
    ensure_directory(f"{ROOT}/{subdir}")

settings_path = f"{ROOT}/Data/DA_SharpExperimentSettings"
if not EDITOR_ASSETS.does_asset_exist(settings_path):
    settings = ASSET_TOOLS.create_asset(
        "DA_SharpExperimentSettings", f"{ROOT}/Data",
        unreal.SharpExperimentSettings, unreal.DataAssetFactory()
    )
    if not settings:
        raise RuntimeError("Could not create settings asset")
    EDITOR_ASSETS.save_loaded_asset(settings)
else:
    settings = EDITOR_ASSETS.load_asset(settings_path)

manager_path = f"{ROOT}/Blueprints/BP_SharpExperimentManager"
if not EDITOR_ASSETS.does_asset_exist(manager_path):
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.SharpExperimentManager)
    manager_bp = ASSET_TOOLS.create_asset(
        "BP_SharpExperimentManager", f"{ROOT}/Blueprints",
        unreal.Blueprint, factory
    )
    if not manager_bp:
        raise RuntimeError("Could not create manager Blueprint")
else:
    manager_bp = EDITOR_ASSETS.load_asset(manager_path)

manager_class = unreal.EditorAssetLibrary.load_blueprint_class(manager_path)
manager_defaults = unreal.get_default_object(manager_class)
manager_defaults.set_editor_property("settings", settings)
EDITOR_ASSETS.save_loaded_asset(manager_bp)

map_path = f"{ROOT}/Maps/SharpMovementExperiment"
if not EDITOR_ASSETS.does_asset_exist(map_path):
    if not unreal.EditorLevelLibrary.new_level(map_path):
        raise RuntimeError("Could not create experiment level")
    world = unreal.EditorLevelLibrary.get_editor_world()
    floor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(0, 0, -50)
    )
    floor.set_actor_label("Experiment Floor")
    floor.set_actor_scale3d(unreal.Vector(20, 20, 1))
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
    floor.static_mesh_component.set_static_mesh(cube)
    light = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.DirectionalLight, unreal.Vector(0, 0, 400)
    )
    light.set_actor_label("Experiment Light")
    light.set_actor_rotation(unreal.Rotator(-45, -30, 0), False)
    unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(-500, 0, 100)
    )
    manager = unreal.EditorLevelLibrary.spawn_actor_from_class(
        manager_class, unreal.Vector(0, 0, 100)
    )
    manager.set_actor_label("Sharp Experiment Manager")
    manager.set_editor_property("settings", settings)
    unreal.EditorLevelLibrary.save_current_level()
else:
    unreal.EditorLevelLibrary.load_level(map_path)
    managers = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if isinstance(actor, unreal.SharpExperimentManager)
    ]
    if len(managers) != 1:
        raise RuntimeError(f"Expected one manager in existing map, found {len(managers)}")
    managers[0].set_editor_property("settings", settings)
    unreal.EditorLevelLibrary.save_current_level()

for path in (settings_path, manager_path, map_path):
    if not EDITOR_ASSETS.does_asset_exist(path):
        raise RuntimeError(f"Missing generated asset: {path}")
    unreal.log(f"M0 asset ready: {path}")
