using UnrealBuildTool;

public class HermesAgentNPCTarget : TargetRules
{
	public HermesAgentNPCTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("HermesAgentNPC");
	}
}
