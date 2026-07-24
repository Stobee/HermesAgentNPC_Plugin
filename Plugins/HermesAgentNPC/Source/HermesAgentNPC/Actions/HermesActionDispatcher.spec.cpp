#include "Misc/AutomationTest.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/HermesTestActionHandler.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesDispatcherTest,
	"Hermes.Actions.Dispatcher.Route",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesDispatcherTest::RunTest(const FString& Parameters)
{
	UHermesActionDispatcher* D = NewObject<UHermesActionDispatcher>(GetTransientPackage());
	UHermesTestActionHandler* Handler = NewObject<UHermesTestActionHandler>(GetTransientPackage());
	D->RegisterHandler(Handler);

	// 등록된 command → ok:true, id 에코
	FHermesActionPayload P;
	P.Id = TEXT("act-1");
	P.Command = TEXT("echo");
	P.Params = MakeShared<FJsonObject>();
	FString Result1;
	D->Dispatch(P, [&](const FString& Json) { Result1 = Json; });
	TSharedPtr<FJsonObject> O1;
	TestTrue(TEXT("parse result1"), HermesJson::Parse(Result1, O1));
	if (O1.IsValid())
	{
		TestEqual(TEXT("id echoed back"), O1->GetStringField(TEXT("id")), TEXT("act-1"));
		TestTrue(TEXT("ok true"), O1->GetBoolField(TEXT("ok")));
	}

	// 미등록 command → ok:false, error:"unsupported command"
	FHermesActionPayload P2;
	P2.Id = TEXT("act-2");
	P2.Command = TEXT("delete_world");
	P2.Params = MakeShared<FJsonObject>();
	FString Result2;
	D->Dispatch(P2, [&](const FString& Json) { Result2 = Json; });
	TSharedPtr<FJsonObject> O2;
	TestTrue(TEXT("parse result2"), HermesJson::Parse(Result2, O2));
	if (O2.IsValid())
	{
		TestFalse(TEXT("unsupported ok false"), O2->GetBoolField(TEXT("ok")));
		TestEqual(TEXT("unsupported error"), O2->GetStringField(TEXT("error")), TEXT("unsupported command"));
	}
	return true;
}
