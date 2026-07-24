using UnrealBuildTool;

public class HermesAgentNPC : ModuleRules
{
	public HermesAgentNPC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"Sockets", "Networking",
			"Json", "JsonUtilities",
			"AIModule", "NavigationSystem",
			"UMG", "Slate", "SlateCore"
		});
	}
}
