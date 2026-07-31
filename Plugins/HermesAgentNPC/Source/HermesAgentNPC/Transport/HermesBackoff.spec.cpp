#include "Misc/AutomationTest.h"
#include "Transport/HermesBackoff.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesBackoffTest,
	"Hermes.Backoff.Ladder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesBackoffTest::RunTest(const FString& Parameters)
{
	using namespace HermesBackoff;

	constexpr float Initial = 0.5f;
	constexpr float Max     = 30.f;
	constexpr float Healthy = 5.f;

	// 접속 실패는 사다리를 올린다.
	TestEqual(TEXT("0.5 -> 1"), NextAfterFailedConnect(0.5f, Initial, Max), 1.f);
	TestEqual(TEXT("1 -> 2"), NextAfterFailedConnect(1.f, Initial, Max), 2.f);

	// 상한을 넘지 않는다.
	TestEqual(TEXT("상한에서 멈춘다"), NextAfterFailedConnect(20.f, Initial, Max), Max);
	TestEqual(TEXT("상한 이상은 상한"), NextAfterFailedConnect(Max, Initial, Max), Max);

	// 0 이나 음수에서 시작해도 초기값 아래로 내려가지 않는다. 그대로 두면
	// 곱해도 0 이라 사다리가 서지 않고 무한 재시도가 된다.
	TestEqual(TEXT("0 에서도 초기값부터"), NextAfterFailedConnect(0.f, Initial, Max), Initial);

	// 핵심: 쓸 만큼 살아 있었으면 되돌린다.
	TestTrue(TEXT("충분히 살았다"), WasHealthy(10.0, Healthy));
	TestEqual(TEXT("오래 살았으면 초기값으로"),
		NextAfterDisconnect(8.f, /*Lifetime*/ 10.0, Healthy, Initial, Max), Initial);

	// 경계: 정확히 임계값이면 건강한 것으로 본다.
	TestTrue(TEXT("경계는 건강"), WasHealthy(5.0, Healthy));
	TestEqual(TEXT("경계에서 초기값으로"),
		NextAfterDisconnect(8.f, 5.0, Healthy, Initial, Max), Initial);

	// 핵심: 붙자마자 끊긴 연결은 접속 실패와 똑같이 다룬다.
	// 이것이 없으면 서버가 매번 즉시 끊을 때 재연결이 왕복 지연 속도로 돈다.
	TestFalse(TEXT("금방 끊겼다"), WasHealthy(0.2, Healthy));
	TestEqual(TEXT("금방 끊기면 사다리를 올린다"),
		NextAfterDisconnect(0.5f, /*Lifetime*/ 0.2, Healthy, Initial, Max), 1.f);
	TestEqual(TEXT("반복하면 계속 오른다"),
		NextAfterDisconnect(4.f, 0.2, Healthy, Initial, Max), 8.f);

	// 짧게 끊기는 것이 반복되어도 상한을 넘지 않는다.
	TestEqual(TEXT("짧은 연결도 상한을 지킨다"),
		NextAfterDisconnect(20.f, 0.1, Healthy, Initial, Max), Max);

	return true;
}
