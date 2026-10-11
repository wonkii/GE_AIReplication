using UnrealBuildTool;

public class AIReplicationNet : ModuleRules
{
	public AIReplicationNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            }
        );

        PrivateDependencyModuleNames.Add("AIReplicationCore");
	}
}
