"""Verify the generated M0 assets in UnrealEditor-Cmd."""
import unreal


root = "/Game/SharpMovementExperiment"
settings = unreal.EditorAssetLibrary.load_asset(
    f"{root}/Data/DA_SharpExperimentSettings"
)
manager_bp = unreal.EditorAssetLibrary.load_asset(
    f"{root}/Blueprints/BP_SharpExperimentManager"
)
assert settings and manager_bp
manager_class = unreal.EditorAssetLibrary.load_blueprint_class(
    f"{root}/Blueprints/BP_SharpExperimentManager"
)
assert unreal.get_default_object(manager_class).get_editor_property("settings") == settings
assert unreal.EditorLevelLibrary.load_level(
    f"{root}/Maps/SharpMovementExperiment"
)
actors = unreal.EditorLevelLibrary.get_all_level_actors()
managers = [a for a in actors if isinstance(a, unreal.SharpExperimentManager)]
assert len(managers) == 1, f"Expected one manager, got {len(managers)}"
assert managers[0].get_editor_property("settings") == settings
assert any(isinstance(a, unreal.PlayerStart) for a in actors)
assert any(isinstance(a, unreal.StaticMeshActor) for a in actors)
unreal.log(f"M0 verification passed: {len(actors)} actors; manager and settings linked")
