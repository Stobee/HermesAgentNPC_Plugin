#include "Misc/AutomationTest.h"
#include "Connection/HermesConnectionEdge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesConnectionEdgeTest,
	"Hermes.Connection.Edge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesConnectionEdgeTest::RunTest(const FString& Parameters)
{
	using HermesConnectionEdge::EDecision;
	using HermesConnectionEdge::Evaluate;

	// 끊긴 채로 아무 일도 없었다.
	TestTrue(TEXT("disconnected and idle => Nothing"),
		Evaluate(/*bWas*/ false, /*SeenGen*/ 0, /*bNow*/ false, /*CurGen*/ 0) == EDecision::Nothing);

	// 붙은 채로 아무 일도 없었다.
	TestTrue(TEXT("connected and idle => Nothing"),
		Evaluate(true, 3, true, 3) == EDecision::Nothing);

	// 첫 연결.
	TestTrue(TEXT("first connect => Opened"),
		Evaluate(false, 0, true, 1) == EDecision::Opened);

	// 끊김.
	TestTrue(TEXT("disconnect => Closed"),
		Evaluate(true, 1, false, 1) == EDecision::Closed);

	// 핵심: 두 틱 사이에 끊김과 재연결이 모두 끝났다. 불린만 보면 참에서 참이라
	// 변화가 없어 보이지만 세대가 올라갔으므로 새 연결이다.
	// 이것을 놓치면 서버는 identify 없는 연결을 들고 있게 된다.
	TestTrue(TEXT("reconnect between polls => Reopened"),
		Evaluate(true, 2, true, 3) == EDecision::Reopened);

	// 두 틱 사이에 여러 번 붙었다 끊겼어도 마지막 연결 하나만 살아 있다.
	TestTrue(TEXT("several reconnects between polls => Reopened once"),
		Evaluate(true, 2, true, 7) == EDecision::Reopened);

	// 붙었다가 다시 끊긴 채로 관측되면 결과는 끊김이다. 살아 있는 연결이
	// 없으므로 identify 할 대상도 없다.
	TestTrue(TEXT("connected then dropped between polls => Closed"),
		Evaluate(true, 2, false, 3) == EDecision::Closed);

	// 끊겨 있다고 알던 중에 붙었다 끊겼다면 관측자 입장에서는 여전히 끊김이다.
	TestTrue(TEXT("connect and drop while believed disconnected => Nothing"),
		Evaluate(false, 2, false, 3) == EDecision::Nothing);

	// 세대가 올라가며 붙었는데 이전에 끊긴 것으로 알고 있었다면 평범한 연결이다.
	TestTrue(TEXT("reconnect after observed disconnect => Opened"),
		Evaluate(false, 2, true, 3) == EDecision::Opened);

	return true;
}
