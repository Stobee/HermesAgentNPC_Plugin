#include "Misc/AutomationTest.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Inventory/HermesItem.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesInventoryTest,
	"Hermes.Inventory.AddRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesInventoryTest::RunTest(const FString& Parameters)
{
	UHermesInventoryComponent* Inv = NewObject<UHermesInventoryComponent>(GetTransientPackage());
	Inv->Add(TEXT("health_potion"), 2);
	TestEqual(TEXT("qty 2"), Inv->GetQuantity(TEXT("health_potion")), 2);

	TestTrue(TEXT("remove 1 ok"), Inv->Remove(TEXT("health_potion"), 1));
	TestEqual(TEXT("qty 1"), Inv->GetQuantity(TEXT("health_potion")), 1);

	TestFalse(TEXT("remove too many fails"), Inv->Remove(TEXT("health_potion"), 5));
	TestEqual(TEXT("qty unchanged"), Inv->GetQuantity(TEXT("health_potion")), 1);

	// 0이 되면 목록에서 제거
	TestTrue(TEXT("remove last"), Inv->Remove(TEXT("health_potion"), 1));
	TestEqual(TEXT("qty 0"), Inv->GetQuantity(TEXT("health_potion")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesInventoryOverflowTest,
	"Hermes.Inventory.AddSaturates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesInventoryOverflowTest::RunTest(const FString& Parameters)
{
	UHermesInventoryComponent* Inv = NewObject<UHermesInventoryComponent>(GetTransientPackage());

	Inv->Add(TEXT("gold"), MAX_int32);
	TestEqual(TEXT("at max"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	// 더 더해도 음수로 뒤집히지 않고 상한에서 멈춰야 한다.
	Inv->Add(TEXT("gold"), 1);
	TestEqual(TEXT("saturated, not negative"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	Inv->Add(TEXT("gold"), MAX_int32);
	TestEqual(TEXT("still saturated"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	return true;
}
