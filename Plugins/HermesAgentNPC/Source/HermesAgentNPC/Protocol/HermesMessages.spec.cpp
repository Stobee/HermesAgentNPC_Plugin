#include "Misc/AutomationTest.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesTest,
	"Hermes.Protocol.Messages.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesTest::RunTest(const FString& Parameters)
{
	// identify 직렬화 후 다시 파싱하면 필드가 보존된다
	const FString Id = HermesJson::MakeIdentify(TEXT("uuid-1"), TEXT("Aria"));
	TSharedPtr<FJsonObject> Obj;
	TestTrue(TEXT("parse ok"), HermesJson::Parse(Id, Obj));
	if (Obj.IsValid())
	{
		TestEqual(TEXT("type"), Obj->GetStringField(TEXT("type")), HermesMsg::Identify);
		TestEqual(TEXT("player_id"), Obj->GetStringField(TEXT("player_id")), TEXT("uuid-1"));
		TestEqual(TEXT("player_name"), Obj->GetStringField(TEXT("player_name")), TEXT("Aria"));
	}

	// player_name 비어있으면 필드 생략
	const FString Id2 = HermesJson::MakeIdentify(TEXT("uuid-2"), FString());
	TSharedPtr<FJsonObject> ObjNoName;
	HermesJson::Parse(Id2, ObjNoName);
	TestFalse(TEXT("player_name omitted"), ObjNoName->HasField(TEXT("player_name")));

	// action_result 실패 케이스
	const FString Ar = HermesJson::MakeActionResult(TEXT("act-1"), false, nullptr, TEXT("path blocked"));
	TSharedPtr<FJsonObject> Obj2;
	TestTrue(TEXT("parse ar"), HermesJson::Parse(Ar, Obj2));
	if (Obj2.IsValid())
	{
		TestEqual(TEXT("ar type"), Obj2->GetStringField(TEXT("type")), HermesMsg::ActionResult);
		TestFalse(TEXT("ar ok=false"), Obj2->GetBoolField(TEXT("ok")));
		TestEqual(TEXT("ar error"), Obj2->GetStringField(TEXT("error")), TEXT("path blocked"));
	}

	// pong (id 포함)
	const FString Pong = HermesJson::MakePong(TEXT("p-12"));
	TSharedPtr<FJsonObject> Obj3;
	HermesJson::Parse(Pong, Obj3);
	TestEqual(TEXT("pong type"), Obj3->GetStringField(TEXT("type")), HermesMsg::Pong);
	TestEqual(TEXT("pong id"), Obj3->GetStringField(TEXT("id")), TEXT("p-12"));

	// 잘못된 JSON은 파싱 실패
	TSharedPtr<FJsonObject> Bad;
	TestFalse(TEXT("bad json"), HermesJson::Parse(TEXT("{not json"), Bad));
	return true;
}
