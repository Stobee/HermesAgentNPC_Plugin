#include "Misc/AutomationTest.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/HermesTestActionHandler.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"

/**
 * 이 플러그인은 단일 NPC 를 대상으로 한다. NPC 가 다시 등록되면 이전 NPC 의
 * 핸들러는 남아 있으면 안 된다. 남아 있으면 Dispatch 의 "첫 매치" 규칙 때문에
 * 액션이 영영 옛 NPC 로 가고, 새로 등록한 NPC 는 아무것도 받지 못한다.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesDispatcherRebindTest,
	"Hermes.Actions.Dispatcher.Rebind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesDispatcherRebindTest::RunTest(const FString& Parameters)
{
	UHermesActionDispatcher* D = NewObject<UHermesActionDispatcher>(GetTransientPackage());

	UHermesTestActionHandler* Old = NewObject<UHermesTestActionHandler>(GetTransientPackage());
	Old->Tag = TEXT("npc-old");
	D->RegisterHandler(Old);

	// 새 NPC 로 갈아끼운다.
	D->ResetHandlers();
	UHermesTestActionHandler* New = NewObject<UHermesTestActionHandler>(GetTransientPackage());
	New->Tag = TEXT("npc-new");
	D->RegisterHandler(New);

	FHermesActionPayload P;
	P.Id = TEXT("act-1");
	P.Command = TEXT("echo");
	P.Params = MakeShared<FJsonObject>();

	FString ResultJson;
	D->Dispatch(P, [&](const FString& Json) { ResultJson = Json; });

	TSharedPtr<FJsonObject> O;
	TestTrue(TEXT("parse result"), HermesJson::Parse(ResultJson, O));
	if (O.IsValid())
	{
		const TSharedPtr<FJsonObject>* R = nullptr;
		TestTrue(TEXT("result object present"), O->TryGetObjectField(TEXT("result"), R));
		if (R)
		{
			// 옛 핸들러가 남아 있으면 first-match 로 npc-old 가 잡힌다.
			TestEqual(TEXT("newest NPC handles the action"),
				(*R)->GetStringField(TEXT("tag")), TEXT("npc-new"));
		}
	}
	return true;
}
