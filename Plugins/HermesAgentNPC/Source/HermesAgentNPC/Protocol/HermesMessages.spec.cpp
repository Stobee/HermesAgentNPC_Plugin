#include "Misc/AutomationTest.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesTest,
	"Hermes.Protocol.Messages.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesTest::RunTest(const FString& Parameters)
{
	// identify 직렬화 후 다시 파싱하면 필드가 보존된다 (3인자: PlayerId, SessionToken, PlayerName)
	const FString Id = HermesJson::MakeIdentify(TEXT("uuid-1"), TEXT("tok-1"), TEXT("Aria"));
	TSharedPtr<FJsonObject> Obj;
	TestTrue(TEXT("parse ok"), HermesJson::Parse(Id, Obj));
	if (Obj.IsValid())
	{
		TestEqual(TEXT("type"), Obj->GetStringField(TEXT("type")), HermesMsg::Identify);
		TestEqual(TEXT("player_id"), Obj->GetStringField(TEXT("player_id")), TEXT("uuid-1"));
		TestEqual(TEXT("session_token"), Obj->GetStringField(TEXT("session_token")), TEXT("tok-1"));
		TestEqual(TEXT("player_name"), Obj->GetStringField(TEXT("player_name")), TEXT("Aria"));
	}

	// player_name 비어있으면 필드 생략
	const FString Id2 = HermesJson::MakeIdentify(TEXT("uuid-2"), TEXT("tok-2"), FString());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesIdentifyV2Test,
	"Hermes.Protocol.Messages.IdentifyV2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesIdentifyV2Test::RunTest(const FString& Parameters)
{
	// 자격 증명이 없으면 신규 발급 요청: player_id / session_token 을 싣지 않는다.
	{
		const FString Json = HermesJson::MakeIdentify(FString(), FString(), TEXT("Aria"));
		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

		double Ver = 0;
		TestTrue(TEXT("has version"), Obj->TryGetNumberField(TEXT("protocol_version"), Ver));
		TestEqual(TEXT("version is 2"), (int32)Ver, 2);

		TestFalse(TEXT("no player_id"), Obj->HasField(TEXT("player_id")));
		TestFalse(TEXT("no session_token"), Obj->HasField(TEXT("session_token")));

		FString Name;
		TestTrue(TEXT("has player_name"), Obj->TryGetStringField(TEXT("player_name"), Name));
		TestEqual(TEXT("player_name value"), Name, TEXT("Aria"));
	}

	// 자격 증명이 있으면 재접속 요청: 둘 다 싣는다.
	{
		const FString Json = HermesJson::MakeIdentify(TEXT("pid-1"), TEXT("tok-1"), FString());
		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

		FString Pid, Tok;
		TestTrue(TEXT("has player_id"), Obj->TryGetStringField(TEXT("player_id"), Pid));
		TestTrue(TEXT("has session_token"), Obj->TryGetStringField(TEXT("session_token"), Tok));
		TestEqual(TEXT("player_id value"), Pid, TEXT("pid-1"));
		TestEqual(TEXT("session_token value"), Tok, TEXT("tok-1"));

		// 이름이 비면 필드를 넣지 않는다 (기존 동작 유지).
		TestFalse(TEXT("no empty player_name"), Obj->HasField(TEXT("player_name")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesParseIdentifiedTest,
	"Hermes.Protocol.Messages.ParseIdentified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesParseIdentifiedTest::RunTest(const FString& Parameters)
{
	// v2 응답: 자격 증명이 정확히 추출된다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"player_id\":\"pid-9\",")
			TEXT("\"session_token\":\"tok-9\",\"chat_id\":\"chat-9\"}"), Obj);

		FString Pid, Tok, Chat;
		TestTrue(TEXT("v2 recognized"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
		TestEqual(TEXT("player_id"), Pid, TEXT("pid-9"));
		TestEqual(TEXT("session_token"), Tok, TEXT("tok-9"));
		TestEqual(TEXT("chat_id"), Chat, TEXT("chat-9"));
	}

	// v1 응답: session_token 이 없으므로 false. 이것이 조용한 불일치를 막는 판정이다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"chat_id\":\"chat-1\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("v1 rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// 토큰이 빈 문자열이어도 v1 과 동일하게 거부한다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"player_id\":\"p\",\"session_token\":\"\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("empty token rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// player_id 가 없으면 거부한다. 둘 다 있어야 재접속에 쓸 수 있다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"session_token\":\"t\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("missing player_id rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// null 오브젝트에도 크래시하지 않는다.
	{
		TSharedPtr<FJsonObject> Null;
		FString Pid, Tok, Chat;
		TestFalse(TEXT("null safe"), HermesJson::ParseIdentified(Null, Pid, Tok, Chat));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesPingTest,
	"Hermes.Protocol.Messages.Ping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesPingTest::RunTest(const FString& Parameters)
{
	const FString Json = HermesJson::MakePing(TEXT("k-1"));
	TSharedPtr<FJsonObject> Obj;
	TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

	FString Type, Id;
	TestTrue(TEXT("has type"), Obj->TryGetStringField(TEXT("type"), Type));
	TestEqual(TEXT("type is ping"), Type, TEXT("ping"));
	TestTrue(TEXT("has id"), Obj->TryGetStringField(TEXT("id"), Id));
	TestEqual(TEXT("id value"), Id, TEXT("k-1"));

	return true;
}
