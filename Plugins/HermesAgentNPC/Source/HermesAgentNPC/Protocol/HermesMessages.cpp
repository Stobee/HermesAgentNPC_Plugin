#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

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

FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& PlayerName)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Identify);
	O->SetStringField(TEXT("player_id"), PlayerId);
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
