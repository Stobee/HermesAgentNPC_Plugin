#include "Misc/AutomationTest.h"
#include "Connection/HermesTrace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTraceTest,
	"Hermes.Trace.FormatFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTraceTest::RunTest(const FString& Parameters)
{
	using HermesTrace::FormatFrame;

	// 토큰 값이 로그에 남으면 안 된다. 그것 하나로 남의 세션에 붙을 수 있다.
	{
		const FString In = TEXT("{\"type\":\"identified\",\"session_token\":\"deadbeefcafe\"}");
		const FString Out = FormatFrame(In);
		TestFalse(TEXT("토큰 값이 사라진다"), Out.Contains(TEXT("deadbeefcafe")));
		TestTrue(TEXT("자리는 남는다"), Out.Contains(TEXT("session_token")));
	}

	// 공백이 섞인 표기도 가려야 한다. 서버가 어떻게 직렬화할지는 우리가 정하지 않는다.
	{
		const FString In = TEXT("{ \"session_token\" : \"secret123\" }");
		TestFalse(TEXT("공백이 있어도 가린다"), FormatFrame(In).Contains(TEXT("secret123")));
	}

	// identify 는 토큰을 실어 보낸다. 송신 쪽도 가려져야 한다.
	{
		const FString In = TEXT("{\"type\":\"identify\",\"protocol_version\":2,")
			TEXT("\"player_id\":\"p-123\",\"session_token\":\"tok-abc\"}");
		const FString Out = FormatFrame(In);
		TestFalse(TEXT("송신 토큰도 가린다"), Out.Contains(TEXT("tok-abc")));
		TestTrue(TEXT("player_id 는 남긴다 — 비밀이 아니고 추적에 필요하다"),
			Out.Contains(TEXT("p-123")));
	}

	// 토큰이 없는 프레임은 손대지 않는다.
	{
		const FString In = TEXT("{\"type\":\"ping\",\"id\":\"k-0001\"}");
		TestEqual(TEXT("토큰이 없으면 그대로"), FormatFrame(In), In);
	}

	// 짧은 프레임은 자르지 않는다.
	{
		const FString In = TEXT("{\"type\":\"pong\"}");
		TestEqual(TEXT("짧으면 그대로"), FormatFrame(In, 100), In);
	}

	// 긴 프레임은 로그를 덮지 않게 자른다. 다만 원래 크기는 알아야 한다.
	{
		const FString Long = FString::ChrN(200, TEXT('x'));
		const FString Out = FormatFrame(Long, 50);
		TestTrue(TEXT("잘린 결과가 원본보다 짧다"), Out.Len() < Long.Len());
		TestTrue(TEXT("원래 길이를 알려준다"), Out.Contains(TEXT("200")));
	}

	// 토큰이 여러 번 나와도 전부 가린다.
	{
		const FString In = TEXT("{\"session_token\":\"aaa\",\"nested\":{\"session_token\":\"bbb\"}}");
		const FString Out = FormatFrame(In);
		TestFalse(TEXT("첫 번째 토큰"), Out.Contains(TEXT("aaa")));
		TestFalse(TEXT("두 번째 토큰"), Out.Contains(TEXT("bbb")));
	}

	return true;
}
