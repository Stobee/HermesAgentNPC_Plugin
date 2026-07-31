#include "Misc/AutomationTest.h"
#include "Connection/HermesSuspendState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesSuspendStateTest,
	"Hermes.Reconnect.SuspendState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesSuspendStateTest::RunTest(const FString& Parameters)
{
	constexpr float Initial = 5.f;
	constexpr float Max     = 300.f;
	constexpr float Healthy = 5.f;

	// 첫 정지는 즉시 재개할 수 있다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		TestTrue(TEXT("정지됨"), S.IsSuspended());
		TestEqual(TEXT("첫 정지는 대기 없음"),
			S.CooldownRemaining(100.0, Initial, Max), 0.f);
		TestTrue(TEXT("즉시 재개된다"), S.TryResume(100.0, Initial, Max));
		TestFalse(TEXT("재개 후 정지 해제"), S.IsSuspended());
	}

	// 두 번째 정지부터 대기가 생긴다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);
		S.NoteConnectionOpened();
		S.NoteSuspended(110.0);

		TestEqual(TEXT("2회째는 5초 대기"),
			S.CooldownRemaining(110.0, Initial, Max), 5.f);
		TestFalse(TEXT("대기 중에는 거부"), S.TryResume(112.0, Initial, Max));
		TestTrue(TEXT("여전히 정지 상태"), S.IsSuspended());
		TestTrue(TEXT("대기가 지나면 승인"), S.TryResume(115.0, Initial, Max));
	}

	// 핵심: 탈취로 끝난 연결은 사다리를 되돌리지 않는다.
	// 5초를 넘게 살아 있었어도 그 연결에서 정지가 있었으면 되돌리지 않는다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.NoteConnectionClosed(/*Lifetime*/ 30.0, Healthy);
		S.TryResume(100.0, Initial, Max);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("탈취는 사다리를 되돌리지 않는다"),
			S.CooldownRemaining(200.0, Initial, Max), 5.f);
	}

	// 정지 없이 끝난 건강한 연결은 사다리를 되돌린다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);

		// 정지 없이 30초 살다 끊긴 연결
		S.NoteConnectionOpened();
		S.NoteConnectionClosed(30.0, Healthy);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("건강한 연결 뒤에는 다시 첫 정지"),
			S.CooldownRemaining(200.0, Initial, Max), 0.f);
	}

	// 짧게 끝난 연결은 정지가 없었더라도 되돌리지 않는다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);

		S.NoteConnectionOpened();
		S.NoteConnectionClosed(/*Lifetime*/ 0.5, Healthy);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("짧은 연결은 되돌리지 않는다"),
			S.CooldownRemaining(200.0, Initial, Max), 5.f);
	}

	// 정지 상태가 아니면 재개할 것이 없다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		TestFalse(TEXT("정지가 아니면 false"), S.TryResume(100.0, Initial, Max));
		TestEqual(TEXT("정지가 아니면 대기 0"),
			S.CooldownRemaining(100.0, Initial, Max), 0.f);
	}

	// 되받아치기를 반복하면 사다리가 계속 오른다 — 이것이 싸움을 수렴시킨다.
	{
		FHermesSuspendState S;
		double Now = 0.0;
		for (int32 i = 0; i < 4; ++i)
		{
			S.NoteConnectionOpened();
			S.NoteSuspended(Now);
			Now += 1000.0;                     // 충분히 기다렸다고 본다
			S.TryResume(Now, Initial, Max);
			S.NoteConnectionClosed(0.2, Healthy);
		}
		S.NoteConnectionOpened();
		S.NoteSuspended(Now);
		TestEqual(TEXT("5회째는 40초"),
			S.CooldownRemaining(Now, Initial, Max), 40.f);
	}

	return true;
}
