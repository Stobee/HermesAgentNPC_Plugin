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
	inline const FString ChatDelta     = TEXT("chat_delta");
	inline const FString ActionEvent   = TEXT("action_event");
}

/** JSON 직렬화/역직렬화 및 아웃바운드 프레임 빌더. */
namespace HermesJson
{
	bool Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj);
	FString Serialize(const TSharedRef<FJsonObject>& Obj);

	/**
	 * v2 identify 프레임. PlayerId/SessionToken 이 비어 있으면 신규 발급 요청으로
	 * 해석되어 해당 필드를 싣지 않는다. protocol_version 은 항상 2로 보낸다.
	 */
	FString MakeIdentify(const FString& PlayerId, const FString& SessionToken,
	                     const FString& PlayerName);
	FString MakeChat(const FString& Id, const FString& Text);
	FString MakeActionResult(const FString& Id, bool bOk,
		const TSharedPtr<FJsonObject>& Result, const FString& Error);
	FString MakePing(const FString& Id);
	FString MakePong(const FString& Id);

	/**
	 * 이미 접수(action_result)한 액션의 완료/실패를 뒤늦게 알린다.
	 * bCompleted=false 면 event="failed" 로 나가며 Error 가 실린다.
	 */
	FString MakeActionEvent(const FString& Id, bool bCompleted,
	                        const TSharedPtr<FJsonObject>& Result, const FString& Error);

	/**
	 * identified 프레임에서 자격 증명을 꺼낸다.
	 * session_token 이 없으면 v1 서버로 판단하고 false 를 반환한다.
	 * true 일 때만 Out 인자가 채워진다.
	 */
	bool ParseIdentified(const TSharedPtr<FJsonObject>& Obj,
	                     FString& OutPlayerId, FString& OutToken, FString& OutChatId);
}
