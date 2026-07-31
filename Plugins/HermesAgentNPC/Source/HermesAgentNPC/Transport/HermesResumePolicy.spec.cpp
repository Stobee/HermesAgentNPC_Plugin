#include "Misc/AutomationTest.h"
#include "Transport/HermesResumePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesResumePolicyTest,
	"Hermes.Reconnect.Cooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesResumePolicyTest::RunTest(const FString& Parameters)
{
	using HermesResumePolicy::RequiredCooldown;

	constexpr float Initial = 5.f;
	constexpr float Max     = 300.f;

	// 첫 정지는 대가가 없다. 정당하게 밀려난 사람을 벌하지 않는다.
	TestEqual(TEXT("1회는 즉시"), RequiredCooldown(1, Initial, Max), 0.f);

	// 두 번째부터 사다리가 선다.
	TestEqual(TEXT("2회는 초기값"), RequiredCooldown(2, Initial, Max), 5.f);
	TestEqual(TEXT("3회는 2배"), RequiredCooldown(3, Initial, Max), 10.f);
	TestEqual(TEXT("4회는 4배"), RequiredCooldown(4, Initial, Max), 20.f);

	// 상한을 넘지 않는다.
	TestEqual(TEXT("상한에서 멈춘다"), RequiredCooldown(20, Initial, Max), Max);

	// 0 이나 음수는 정지가 없었다는 뜻이다. 기다릴 것이 없다.
	TestEqual(TEXT("0회는 즉시"), RequiredCooldown(0, Initial, Max), 0.f);
	TestEqual(TEXT("음수도 즉시"), RequiredCooldown(-3, Initial, Max), 0.f);

	// Initial 이 Max 보다 크게 설정되어도 Max 를 넘지 않는다.
	TestEqual(TEXT("잘못된 설정에서도 상한을 지킨다"),
		RequiredCooldown(2, 1000.f, Max), Max);

	return true;
}
