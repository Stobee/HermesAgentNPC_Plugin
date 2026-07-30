#include "Misc/AutomationTest.h"
#include "Connection/HermesPendingChats.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesPendingChatsTest,
	"Hermes.PendingChats.Timeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesPendingChatsTest::RunTest(const FString& Parameters)
{
	const float Timeout = 60.f;

	// 타임아웃 전에는 수집되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TArray<FString> Out;
		P.CollectTimedOut(159.0, Timeout, Out);
		TestEqual(TEXT("not yet timed out"), Out.Num(), 0);
		TestEqual(TEXT("still tracked"), P.Num(), 1);
	}

	// 타임아웃 경과 후 수집되고 목록에서 제거된다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TArray<FString> Out;
		P.CollectTimedOut(160.0, Timeout, Out);
		TestEqual(TEXT("timed out"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("correct id"), Out[0], TEXT("c-1"));
		}
		TestEqual(TEXT("removed after collect"), P.Num(), 0);

		// 두 번째 호출에서 다시 통지되지 않는다.
		TArray<FString> Out2;
		P.CollectTimedOut(300.0, Timeout, Out2);
		TestEqual(TEXT("not reported twice"), Out2.Num(), 0);
	}

	// Touch 가 타이머를 갱신해 긴 생성이 타임아웃되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		// 50초마다 델타가 오면 총 200초가 지나도 살아있어야 한다.
		P.Touch(TEXT("c-1"), 150.0);
		P.Touch(TEXT("c-1"), 200.0);
		P.Touch(TEXT("c-1"), 250.0);
		P.Touch(TEXT("c-1"), 300.0);

		TArray<FString> Out;
		P.CollectTimedOut(340.0, Timeout, Out);
		TestEqual(TEXT("kept alive by deltas"), Out.Num(), 0);

		// 델타가 끊기면 그때부터 다시 센다.
		TArray<FString> Out2;
		P.CollectTimedOut(360.0, Timeout, Out2);
		TestEqual(TEXT("dies after deltas stop"), Out2.Num(), 1);
	}

	// 추적 중이 아닌 Id 의 Touch 는 무시한다(항목이 생기지 않는다).
	{
		FHermesPendingChats P;
		P.Touch(TEXT("unknown"), 100.0);
		TestEqual(TEXT("touch does not create"), P.Num(), 0);
	}

	// Remove 후에는 수집되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);
		P.Remove(TEXT("c-1"));

		TArray<FString> Out;
		P.CollectTimedOut(1000.0, Timeout, Out);
		TestEqual(TEXT("removed not collected"), Out.Num(), 0);
	}

	// 여러 건 중 만료된 것만 선별 수집된다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("old"), 100.0);
		P.Add(TEXT("new"), 150.0);

		TArray<FString> Out;
		P.CollectTimedOut(170.0, Timeout, Out);
		TestEqual(TEXT("only old collected"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("old id"), Out[0], TEXT("old"));
		}
		TestEqual(TEXT("new still tracked"), P.Num(), 1);
	}

	// Clear 는 전부 비운다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("a"), 100.0);
		P.Add(TEXT("b"), 100.0);
		P.Clear();
		TestEqual(TEXT("cleared"), P.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesPendingChatsFailByIdTest,
	"Hermes.PendingChats.FailById",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesPendingChatsFailByIdTest::RunTest(const FString& Parameters)
{
	const float Timeout = 60.f;

	// 진행 중인 두 발화 중 하나의 id 로 실패시키면 그 턴만 사라지고
	// 다른 턴은 살아 있다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);
		P.Add(TEXT("c-2"), 100.0);

		TestTrue(TEXT("known id reported"), P.FailById(TEXT("c-1")));
		TestEqual(TEXT("only one removed"), P.Num(), 1);

		// 실패 처리된 턴은 이후 타임아웃 수집에 다시 나오지 않는다.
		TArray<FString> Out;
		P.CollectTimedOut(1000.0, Timeout, Out);
		TestEqual(TEXT("only survivor collected"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("survivor is c-2"), Out[0], TEXT("c-2"));
		}
	}

	// 미지의 id 는 아무 일도 일으키지 않는다. 어느 턴인지 모르는 채로 아무거나
	// 실패시키면 무관한 대화가 끊긴다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TestFalse(TEXT("unknown id not reported"), P.FailById(TEXT("no_such_turn")));
		TestEqual(TEXT("nothing removed"), P.Num(), 1);
	}

	// 같은 id 를 두 번 실패시켜도 한 번만 보고된다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TestTrue(TEXT("first fail reported"), P.FailById(TEXT("c-1")));
		TestFalse(TEXT("second fail not reported"), P.FailById(TEXT("c-1")));
	}

	return true;
}
