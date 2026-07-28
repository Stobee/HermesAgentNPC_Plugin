#include "Misc/AutomationTest.h"
#include "Connection/HermesUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesUtilPushBoundedTest,
	"Hermes.Util.PushBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesUtilPushBoundedTest::RunTest(const FString& Parameters)
{
	// 상한 미만에서는 아무것도 버리지 않는다.
	{
		TArray<FString> A;
		TestEqual(TEXT("no drop 1"), HermesUtil::PushBounded(A, TEXT("a"), 3), 0);
		TestEqual(TEXT("no drop 2"), HermesUtil::PushBounded(A, TEXT("b"), 3), 0);
		TestEqual(TEXT("no drop 3"), HermesUtil::PushBounded(A, TEXT("c"), 3), 0);
		TestEqual(TEXT("size 3"), A.Num(), 3);
		TestEqual(TEXT("order kept 0"), A[0], TEXT("a"));
		TestEqual(TEXT("order kept 2"), A[2], TEXT("c"));
	}

	// 상한 도달 후에는 가장 오래된 것부터 버린다. 최신 발화가 살아남아야 한다.
	{
		TArray<FString> A;
		HermesUtil::PushBounded(A, TEXT("a"), 3);
		HermesUtil::PushBounded(A, TEXT("b"), 3);
		HermesUtil::PushBounded(A, TEXT("c"), 3);

		TestEqual(TEXT("drops one"), HermesUtil::PushBounded(A, TEXT("d"), 3), 1);
		TestEqual(TEXT("size still 3"), A.Num(), 3);
		TestEqual(TEXT("oldest dropped"), A[0], TEXT("b"));
		TestEqual(TEXT("newest kept"), A[2], TEXT("d"));
	}

	// 상한이 줄어들면 한 번에 여러 개가 버려지고 그 개수가 반환된다.
	{
		TArray<FString> A;
		for (int32 i = 0; i < 5; ++i)
		{
			HermesUtil::PushBounded(A, FString::Printf(TEXT("%d"), i), 10);
		}
		TestEqual(TEXT("size 5"), A.Num(), 5);

		// 이제 상한 2 로 넣으면 (5+1) - 2 = 4 개가 버려진다.
		TestEqual(TEXT("drops four"), HermesUtil::PushBounded(A, TEXT("new"), 2), 4);
		TestEqual(TEXT("size 2"), A.Num(), 2);
		TestEqual(TEXT("kept newest"), A[1], TEXT("new"));
	}

	// MaxNum 0 이하는 1 로 취급한다.
	{
		TArray<FString> A;
		HermesUtil::PushBounded(A, TEXT("a"), 0);
		HermesUtil::PushBounded(A, TEXT("b"), 0);
		TestEqual(TEXT("clamped to 1"), A.Num(), 1);
		TestEqual(TEXT("newest kept"), A[0], TEXT("b"));
	}

	return true;
}
