using UnrealBuildTool;

public class HermesAgentNPC : ModuleRules
{
	public HermesAgentNPC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 소스를 Public/Private 없이 모듈 루트 하위 폴더로 구성하므로,
		// 모듈 루트를 인클루드 경로에 추가해 "Subdir/Header.h" 형태가 해석되게 한다.
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"Sockets", "Networking",
			"Json", "JsonUtilities",
			"AIModule", "NavigationSystem",
			"UMG", "Slate", "SlateCore",
			"DeveloperSettings"
		});
	}
}
