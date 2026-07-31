#include "Misc/AutomationTest.h"
#include "UI/HermesChatCorrelation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesChatCorrelationTest,
	"Hermes.Chat.Correlation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesChatCorrelationTest::RunTest(const FString& Parameters)
{
	using HermesChatCorrelation::ShouldDisplayDelta;
	using HermesChatCorrelation::ShouldDisplayResponse;

	// 방금 보낸 발화의 응답은 표시한다.
	TestTrue(TEXT("현재 턴의 응답"), ShouldDisplayResponse(TEXT("c-0002"), TEXT("c-0002")));
	TestTrue(TEXT("현재 턴의 델타"), ShouldDisplayDelta(TEXT("c-0002"), TEXT("c-0002")));

	// 늦게 도착한 이전 턴의 응답은 버린다.
	TestFalse(TEXT("이전 턴의 응답"), ShouldDisplayResponse(TEXT("c-0001"), TEXT("c-0002")));
	TestFalse(TEXT("이전 턴의 델타"), ShouldDisplayDelta(TEXT("c-0001"), TEXT("c-0002")));

	// 핵심: id 없는 chat_response 는 자발 발화다 (스펙 §4.4 에서 id 는 optional).
	// action_event 이후 서버가 먼저 거는 말이며, 버리면 플레이어에게 영영 닿지 않는다.
	TestTrue(TEXT("자발 발화는 표시한다"),
		ShouldDisplayResponse(FString(), TEXT("c-0002")));

	// 발화를 한 번도 보내지 않은 상태에서도 자발 발화는 표시한다.
	TestTrue(TEXT("발화 전에도 자발 발화는 표시한다"),
		ShouldDisplayResponse(FString(), FString()));

	// 델타에는 id 가 필수다(§4.9). 비어 있으면 규격 위반이므로 버린다 —
	// 자발 발화는 델타 없이 chat_response 하나로 오기 때문에 여기서 관대할 이유가 없다.
	TestFalse(TEXT("id 없는 델타는 버린다"),
		ShouldDisplayDelta(FString(), TEXT("c-0002")));
	TestFalse(TEXT("id 없는 델타는 버린다 (보낸 발화도 없을 때)"),
		ShouldDisplayDelta(FString(), FString()));

	return true;
}
