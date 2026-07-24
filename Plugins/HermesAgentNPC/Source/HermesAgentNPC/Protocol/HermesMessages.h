#pragma once
#include "CoreMinimal.h"

class FJsonObject;

/** 프로토콜 메시지 type 문자열 상수. ue5-socket-protocol.md와 정확히 일치. */
namespace HermesMsg
{
	inline const FString Identify      = TEXT("identify");
	inline const FString Identified    = TEXT("identified");
	inline const FString Chat          = TEXT("chat");
	inline const FString ChatResponse  = TEXT("chat_response");
	inline const FString ActionRequest = TEXT("action_request");
	inline const FString ActionResult  = TEXT("action_result");
	inline const FString Ping          = TEXT("ping");
	inline const FString Pong          = TEXT("pong");
	inline const FString Error         = TEXT("error");
}

/** JSON 직렬화/역직렬화 및 아웃바운드 프레임 빌더. */
namespace HermesJson
{
	bool Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj);
	FString Serialize(const TSharedRef<FJsonObject>& Obj);

	FString MakeIdentify(const FString& PlayerId, const FString& PlayerName);
	FString MakeChat(const FString& Id, const FString& Text);
	FString MakeActionResult(const FString& Id, bool bOk,
		const TSharedPtr<FJsonObject>& Result, const FString& Error);
	FString MakePong(const FString& Id);
}
