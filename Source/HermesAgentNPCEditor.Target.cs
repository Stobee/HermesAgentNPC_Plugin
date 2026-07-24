using UnrealBuildTool;

public class HermesAgentNPCEditorTarget : TargetRules
{
	public HermesAgentNPCEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("HermesAgentNPC");
	}
}
