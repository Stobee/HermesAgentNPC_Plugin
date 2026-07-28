#include "Misc/AutomationTest.h"
#include "Settings/HermesSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesSettingsCommandLineTest,
	"Hermes.Settings.CommandLineOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesSettingsCommandLineTest::RunTest(const FString& Parameters)
{
	// 인자가 없으면 ini/기본값이 그대로 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-SomeOtherFlag"), Host, Port);
		TestEqual(TEXT("host unchanged"), Host, TEXT("ini.example.com"));
		TestEqual(TEXT("port unchanged"), Port, 8770);
	}

	// Host만 지정하면 Port는 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesHost=10.0.0.5"), Host, Port);
		TestEqual(TEXT("host overridden"), Host, TEXT("10.0.0.5"));
		TestEqual(TEXT("port still ini"), Port, 8770);
	}

	// Port만 지정하면 Host는 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=9999"), Host, Port);
		TestEqual(TEXT("host still ini"), Host, TEXT("ini.example.com"));
		TestEqual(TEXT("port overridden"), Port, 9999);
	}

	// 정수로 파싱되지 않는 포트는 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=abc"), Host, Port);
		TestEqual(TEXT("bad port ignored"), Port, 8770);
	}

	// 범위 밖 포트는 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=0"), Host, Port);
		TestEqual(TEXT("port 0 ignored"), Port, 8770);

		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=70000"), Host, Port);
		TestEqual(TEXT("port 70000 ignored"), Port, 8770);
	}

	// 빈 Host 값은 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesHost="), Host, Port);
		TestEqual(TEXT("empty host ignored"), Host, TEXT("ini.example.com"));
	}

	// 둘 다 지정하면 둘 다 덮인다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(
			TEXT("-HermesHost=hermes.local -HermesPort=1234"), Host, Port);
		TestEqual(TEXT("both host"), Host, TEXT("hermes.local"));
		TestEqual(TEXT("both port"), Port, 1234);
	}

	return true;
}
