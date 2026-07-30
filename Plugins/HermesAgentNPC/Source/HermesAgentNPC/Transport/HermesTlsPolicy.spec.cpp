#include "Misc/AutomationTest.h"
#include "Transport/HermesTlsPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsServerNameTest,
	"Hermes.TlsPolicy.ServerName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsServerNameTest::RunTest(const FString& Parameters)
{
	// 지정하지 않으면 Host 를 그대로 쓴다.
	TestEqual(TEXT("empty falls back to host"),
		HermesTls::ResolveServerName(TEXT("192.168.0.111"), FString()),
		TEXT("192.168.0.111"));

	// 지정하면 그 값을 쓴다. IP 로 접속하면서 인증서에 도메인명이 든 경우다.
	TestEqual(TEXT("override wins"),
		HermesTls::ResolveServerName(TEXT("192.168.0.111"), TEXT("hermes.local")),
		TEXT("hermes.local"));

	// 공백만 있는 값도 미지정으로 취급한다.
	TestEqual(TEXT("whitespace falls back"),
		HermesTls::ResolveServerName(TEXT("hermes.example.com"), TEXT("   ")),
		TEXT("hermes.example.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsVerifyModeTest,
	"Hermes.TlsPolicy.VerifyMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsVerifyModeTest::RunTest(const FString& Parameters)
{
	using HermesTls::EVerifyMode;

	// 둘 다 없으면 시스템 루트 CA.
	{
		TArray<FString> NoPins;
		TestTrue(TEXT("no pins, no ca => SystemCa"),
			HermesTls::ResolveVerifyMode(NoPins, FString()) == EVerifyMode::SystemCa);
	}

	// 사설 CA 만 있으면 PrivateCa.
	{
		TArray<FString> NoPins;
		TestTrue(TEXT("ca only => PrivateCa"),
			HermesTls::ResolveVerifyMode(NoPins, TEXT("Certs/private-ca.pem")) == EVerifyMode::PrivateCa);
	}

	// 핀이 있으면 핀이 우선한다 (사설 CA 가 함께 있어도).
	{
		TArray<FString> Pins;
		Pins.Add(TEXT("abc123="));
		TestTrue(TEXT("pins only => PinnedKey"),
			HermesTls::ResolveVerifyMode(Pins, FString()) == EVerifyMode::PinnedKey);
		TestTrue(TEXT("pins beat ca"),
			HermesTls::ResolveVerifyMode(Pins, TEXT("Certs/private-ca.pem")) == EVerifyMode::PinnedKey);
	}

	// 빈 문자열만 든 핀 배열은 핀 없음으로 취급한다. ini 편집 실수로
	// 검증이 통과해버리는 일이 없어야 한다.
	{
		TArray<FString> EmptyPins;
		EmptyPins.Add(FString());
		EmptyPins.Add(TEXT("  "));
		TestTrue(TEXT("blank pins are not pins"),
			HermesTls::ResolveVerifyMode(EmptyPins, FString()) == EVerifyMode::SystemCa);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsUseTlsTest,
	"Hermes.TlsPolicy.UseTls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsUseTlsTest::RunTest(const FString& Parameters)
{
	// 비-Shipping 에서는 설정을 따른다. 개발 중 평문 사용을 허용한다.
	TestTrue(TEXT("dev true => true"),  HermesTls::ResolveUseTls(true,  false));
	TestFalse(TEXT("dev false => false"), HermesTls::ResolveUseTls(false, false));

	// Shipping 에서는 false 여도 강제로 켠다.
	TestTrue(TEXT("shipping true => true"),  HermesTls::ResolveUseTls(true,  true));
	TestTrue(TEXT("shipping false => forced true"), HermesTls::ResolveUseTls(false, true));

	return true;
}
