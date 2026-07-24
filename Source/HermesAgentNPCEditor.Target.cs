using UnrealBuildTool;

public class HermesAgentNPCEditorTarget : TargetRules
{
	public HermesAgentNPCEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		// 설치형(바이너리) 엔진과 빌드 환경을 공유하므로, V5가 바꾸는 경고 레벨을
		// 강제 적용하도록 허용한다. (Unique 환경은 엔진 재컴파일이 필요해 회피)
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("HermesAgentNPC");
	}
}
