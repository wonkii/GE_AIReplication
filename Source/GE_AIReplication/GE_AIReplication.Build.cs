// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GE_AIReplication : ModuleRules
{
	public GE_AIReplication(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"GE_AIReplication",
			"GE_AIReplication/Variant_Platforming",
			"GE_AIReplication/Variant_Platforming/Animation",
			"GE_AIReplication/Variant_Combat",
			"GE_AIReplication/Variant_Combat/AI",
			"GE_AIReplication/Variant_Combat/Animation",
			"GE_AIReplication/Variant_Combat/Gameplay",
			"GE_AIReplication/Variant_Combat/Interfaces",
			"GE_AIReplication/Variant_Combat/UI",
			"GE_AIReplication/Variant_SideScrolling",
			"GE_AIReplication/Variant_SideScrolling/AI",
			"GE_AIReplication/Variant_SideScrolling/Gameplay",
			"GE_AIReplication/Variant_SideScrolling/Interfaces",
			"GE_AIReplication/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
