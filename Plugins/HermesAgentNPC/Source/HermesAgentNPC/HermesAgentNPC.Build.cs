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

		// TLS 는 SSL 모듈(OpenSSL)로 구현한다. 플랫폼에 따라 없을 수 있으므로
		// 관련 코드는 전부 #if WITH_SSL 로 감싼다.
		PrivateDependencyModuleNames.Add("SSL");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
