using UnrealBuildTool;

public class GEAnimationAnalysisEditor : ModuleRules
{
    public GEAnimationAnalysisEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Json",
            "AnimationBlueprintLibrary", "PythonScriptPlugin", "GE_AIReplication"
        });
    }
}
