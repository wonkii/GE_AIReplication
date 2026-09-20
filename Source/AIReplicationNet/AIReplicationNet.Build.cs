using UnrealBuildTool;

public class AIReplicationNet : ModuleRules
{
	public AIReplicationNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("AIReplicationCore");
	}
}
