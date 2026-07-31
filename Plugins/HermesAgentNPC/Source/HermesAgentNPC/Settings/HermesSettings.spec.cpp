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

	// TLS 도 덮을 수 있어야 한다.
	//
	// 스텁 서버는 평문이고 실서버는 TLS 라, 이것이 없으면 검증할 때마다
	// Config/DefaultGame.ini 를 고쳐야 한다. 고치는 순간 스텁 시나리오 14종이
	// 전부 막히므로 실제로 문제가 된다.
	{
		bool bUseTLS = true;
		UHermesSettings::ApplyTlsOverride(TEXT("-HermesUseTLS=0"), bUseTLS);
		TestFalse(TEXT("0 이면 끈다"), bUseTLS);

		bUseTLS = false;
		UHermesSettings::ApplyTlsOverride(TEXT("-HermesUseTLS=1"), bUseTLS);
		TestTrue(TEXT("1 이면 켠다"), bUseTLS);

		bUseTLS = true;
		UHermesSettings::ApplyTlsOverride(TEXT("-HermesUseTLS=false"), bUseTLS);
		TestFalse(TEXT("false 도 받는다"), bUseTLS);

		bUseTLS = false;
		UHermesSettings::ApplyTlsOverride(TEXT("-HermesUseTLS=true"), bUseTLS);
		TestTrue(TEXT("true 도 받는다"), bUseTLS);
	}

	// 인자가 없으면 ini 값을 유지한다.
	{
		bool bUseTLS = true;
		UHermesSettings::ApplyTlsOverride(TEXT("-SomeOtherFlag"), bUseTLS);
		TestTrue(TEXT("인자 없으면 유지"), bUseTLS);
	}

	// 해석할 수 없는 값은 무시한다. 오타로 평문 통신이 되면 안 된다.
	{
		bool bUseTLS = true;
		UHermesSettings::ApplyTlsOverride(TEXT("-HermesUseTLS=maybe"), bUseTLS);
		TestTrue(TEXT("알 수 없는 값은 무시"), bUseTLS);
	}

	return true;
}
