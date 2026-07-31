#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
// TCondensedJsonPrintPolicy 는 여기 있다. 에디터 빌드는 unity/PCH 로 전이적으로
// 얻지만 플러그인 패키징 빌드에는 없어 컴파일이 깨진다.
#include "Policies/CondensedJsonPrintPolicy.h"

bool HermesJson::Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
}

FString HermesJson::Serialize(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& SessionToken,
	const FString& PlayerName)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Identify);
	// 버전을 항상 실어 v1 서버와의 조용한 불일치를 막는다.
	O->SetNumberField(TEXT("protocol_version"), 2);

	// 둘 중 하나라도 비면 신규 발급 요청으로 취급한다. 반쪽 자격 증명을
	// 보내면 서버가 not_authorized 로 끊어 재발급 경로가 막힌다.
	if (!PlayerId.IsEmpty() && !SessionToken.IsEmpty())
	{
		O->SetStringField(TEXT("player_id"), PlayerId);
		O->SetStringField(TEXT("session_token"), SessionToken);
	}
	if (!PlayerName.IsEmpty())
	{
		O->SetStringField(TEXT("player_name"), PlayerName);
	}
	return Serialize(O);
}

FString HermesJson::MakeChat(const FString& Id, const FString& Text)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Chat);
	O->SetStringField(TEXT("id"), Id);
	O->SetStringField(TEXT("text"), Text);
	return Serialize(O);
}

FString HermesJson::MakeActionResult(const FString& Id, bool bOk,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::ActionResult);
	O->SetStringField(TEXT("id"), Id);
	O->SetBoolField(TEXT("ok"), bOk);
	if (Result.IsValid())
	{
		O->SetObjectField(TEXT("result"), Result);
	}
	if (!Error.IsEmpty())
	{
		O->SetStringField(TEXT("error"), Error);
	}
	return Serialize(O);
}

FString HermesJson::MakePing(const FString& Id)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Ping);
	if (!Id.IsEmpty())
	{
		O->SetStringField(TEXT("id"), Id);
	}
	return Serialize(O);
}

FString HermesJson::MakePong(const FString& Id)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Pong);
	if (!Id.IsEmpty())
	{
		O->SetStringField(TEXT("id"), Id);
	}
	return Serialize(O);
}

FString HermesJson::MakeActionEvent(const FString& Id, bool bCompleted,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::ActionEvent);
	O->SetStringField(TEXT("id"), Id);
	O->SetStringField(TEXT("event"), bCompleted ? TEXT("completed") : TEXT("failed"));
	if (Result.IsValid())
	{
		O->SetObjectField(TEXT("result"), Result);
	}
	if (!Error.IsEmpty())
	{
		O->SetStringField(TEXT("error"), Error);
	}
	return Serialize(O);
}

bool HermesJson::ParseIdentified(const TSharedPtr<FJsonObject>& Obj,
	FString& OutPlayerId, FString& OutToken, FString& OutChatId)
{
	if (!Obj.IsValid())
	{
		return false;
	}

	FString Pid, Tok;
	// 둘 다 있어야 재접속에 쓸 수 있다. 하나라도 없으면 v1 서버로 판단한다.
	if (!Obj->TryGetStringField(TEXT("player_id"), Pid) || Pid.IsEmpty())
	{
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("session_token"), Tok) || Tok.IsEmpty())
	{
		return false;
	}

	OutPlayerId = Pid;
	OutToken    = Tok;
	Obj->TryGetStringField(TEXT("chat_id"), OutChatId);
	return true;
}
