#include "Misc/AutomationTest.h"
#include "Actions/HermesRateLimiter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesRateLimiterTest,
	"Hermes.RateLimiter.TokenBucket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesRateLimiterTest::RunTest(const FString& Parameters)
{
	// 용량 내 연속 요청은 모두 통과한다.
	{
		FHermesRateLimiter L;
		L.Configure(5);
		for (int32 i = 0; i < 5; ++i)
		{
			TestTrue(FString::Printf(TEXT("burst %d passes"), i), L.TryConsume(100.0));
		}
		// 소진 후 같은 시각의 요청은 거부된다.
		TestFalse(TEXT("exhausted rejected"), L.TryConsume(100.0));
	}

	// 1초 경과 후 용량만큼 다시 충전된다.
	{
		FHermesRateLimiter L;
		L.Configure(5);
		for (int32 i = 0; i < 5; ++i)
		{
			L.TryConsume(100.0);
		}
		TestFalse(TEXT("still exhausted"), L.TryConsume(100.0));

		TestTrue(TEXT("refilled after 1s"), L.TryConsume(101.0));
		for (int32 i = 0; i < 4; ++i)
		{
			TestTrue(FString::Printf(TEXT("refill burst %d"), i), L.TryConsume(101.0));
		}
		TestFalse(TEXT("exhausted again"), L.TryConsume(101.0));
	}

	// 부분 충전: 0.5초면 절반만 찬다.
	{
		FHermesRateLimiter L;
		L.Configure(10);
		for (int32 i = 0; i < 10; ++i)
		{
			L.TryConsume(200.0);
		}
		// 0.5초 => 5개 충전
		for (int32 i = 0; i < 5; ++i)
		{
			TestTrue(FString::Printf(TEXT("half refill %d"), i), L.TryConsume(200.5));
		}
		TestFalse(TEXT("half refill exhausted"), L.TryConsume(200.5));
	}

	// 시간이 역행해도 토큰이 폭증하거나 음수가 되지 않는다.
	{
		FHermesRateLimiter L;
		L.Configure(3);
		TestTrue(TEXT("first ok"), L.TryConsume(500.0));
		TestTrue(TEXT("backward time ok"), L.TryConsume(400.0));
		TestTrue(TEXT("backward time ok 2"), L.TryConsume(400.0));
		// 용량 3 을 넘겨 소비할 수 없다.
		TestFalse(TEXT("no free tokens from time travel"), L.TryConsume(400.0));
	}

	// 충전은 용량을 넘지 않는다.
	{
		FHermesRateLimiter L;
		L.Configure(2);
		TestTrue(TEXT("t0 a"), L.TryConsume(0.0));
		TestTrue(TEXT("t0 b"), L.TryConsume(0.0));
		// 100초를 기다려도 용량 2 를 넘게 쌓이지 않는다.
		TestTrue(TEXT("t100 a"), L.TryConsume(100.0));
		TestTrue(TEXT("t100 b"), L.TryConsume(100.0));
		TestFalse(TEXT("capped at capacity"), L.TryConsume(100.0));
	}

	return true;
}
