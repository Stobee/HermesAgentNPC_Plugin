#include "Misc/AutomationTest.h"
#include "Actions/HermesActionParams.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsCoordinateTest,
	"Hermes.ActionParams.Coordinate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsCoordinateTest::RunTest(const FString& Parameters)
{
	const float Limit = 1.0e7f;

	TestTrue(TEXT("0 ok"), HermesParams::IsValidCoordinate(0.0, Limit));
	TestTrue(TEXT("normal ok"), HermesParams::IsValidCoordinate(1200.0, Limit));
	TestTrue(TEXT("negative ok"), HermesParams::IsValidCoordinate(-1200.0, Limit));

	// 경계: Limit 은 통과, 그보다 크면 거부. 음수 방향도 대칭이어야 한다.
	TestTrue(TEXT("at limit ok"), HermesParams::IsValidCoordinate((double)Limit, Limit));
	TestTrue(TEXT("at -limit ok"), HermesParams::IsValidCoordinate(-(double)Limit, Limit));
	TestFalse(TEXT("over limit rejected"), HermesParams::IsValidCoordinate((double)Limit + 1.0, Limit));
	TestFalse(TEXT("under -limit rejected"), HermesParams::IsValidCoordinate(-(double)Limit - 1.0, Limit));

	// non-finite 는 전부 거부. 이것이 FVector(inf,...) 가 엔진에 들어가는 경로를 막는다.
	const double Inf = std::numeric_limits<double>::infinity();
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("+inf rejected"), HermesParams::IsValidCoordinate(Inf, Limit));
	TestFalse(TEXT("-inf rejected"), HermesParams::IsValidCoordinate(-Inf, Limit));
	TestFalse(TEXT("nan rejected"), HermesParams::IsValidCoordinate(NaN, Limit));
	TestFalse(TEXT("1e308 rejected"), HermesParams::IsValidCoordinate(1.0e308, Limit));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsQuantityTest,
	"Hermes.ActionParams.Quantity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsQuantityTest::RunTest(const FString& Parameters)
{
	const int32 Max = 999999;
	int32 Qty = -1;

	TestTrue(TEXT("1 ok"), HermesParams::IsValidQuantity(1.0, Max, Qty));
	TestEqual(TEXT("1 value"), Qty, 1);

	TestTrue(TEXT("max ok"), HermesParams::IsValidQuantity((double)Max, Max, Qty));
	TestEqual(TEXT("max value"), Qty, Max);

	TestFalse(TEXT("0 rejected"), HermesParams::IsValidQuantity(0.0, Max, Qty));
	TestFalse(TEXT("-1 rejected"), HermesParams::IsValidQuantity(-1.0, Max, Qty));
	TestFalse(TEXT("max+1 rejected"), HermesParams::IsValidQuantity((double)Max + 1.0, Max, Qty));

	// int32 범위를 넘는 값. 캐스트 전에 걸러야 미정의 동작을 피한다.
	TestFalse(TEXT("2e9 rejected"), HermesParams::IsValidQuantity(2.0e9, Max, Qty));
	TestFalse(TEXT("1e18 rejected"), HermesParams::IsValidQuantity(1.0e18, Max, Qty));

	// 소수는 거부. quantity 는 정수여야 한다.
	TestFalse(TEXT("1.5 rejected"), HermesParams::IsValidQuantity(1.5, Max, Qty));

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("nan rejected"), HermesParams::IsValidQuantity(NaN, Max, Qty));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsItemIdTest,
	"Hermes.ActionParams.ItemId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsItemIdTest::RunTest(const FString& Parameters)
{
	const int32 MaxLen = 64;

	TestTrue(TEXT("normal ok"), HermesParams::IsValidItemId(TEXT("health_potion"), MaxLen));
	TestFalse(TEXT("empty rejected"), HermesParams::IsValidItemId(TEXT(""), MaxLen));

	const FString AtLimit = FString::ChrN(MaxLen, TEXT('a'));
	const FString OverLimit = FString::ChrN(MaxLen + 1, TEXT('a'));
	TestTrue(TEXT("at limit ok"), HermesParams::IsValidItemId(AtLimit, MaxLen));
	TestFalse(TEXT("over limit rejected"), HermesParams::IsValidItemId(OverLimit, MaxLen));

	return true;
}
