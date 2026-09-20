using UnrealBuildTool;

public class AIReplicationCore : ModuleRules
{
	public AIReplicationCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.Add("Core");
	}
}
