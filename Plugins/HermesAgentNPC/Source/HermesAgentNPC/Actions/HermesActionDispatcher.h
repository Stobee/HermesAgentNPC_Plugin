#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionTypes.h"
#include "Actions/HermesActionHandler.h"
#include "Actions/HermesRateLimiter.h"
#include "HermesActionDispatcher.generated.h"

/**
 * 등록된 핸들러 중 command 를 처리할 수 있는 첫 핸들러로 라우팅한다.
 * 화이트리스트에 없는 command 는 즉시 거부(unsupported command)하고,
 * 설정된 타임아웃 안에 결과가 없으면 timeout 으로 회신한다.
 * 초당 처리량은 토큰 버킷으로 제한한다.
 */
UCLASS()
class UHermesActionDispatcher : public UObject
{
	GENERATED_BODY()
public:
	void RegisterHandler(TScriptInterface<IHermesActionHandler> Handler);

	/** 등록된 핸들러를 모두 비운다. NPC 를 갈아끼울 때 옛 핸들러를 남기지 않는다. */
	void ResetHandlers();

	/** 라우팅 후 action_result JSON 문자열을 OnResult 로 반환한다. */
	void Dispatch(const FHermesActionPayload& Payload,
		TFunction<void(const FString& ActionResultJson)> OnResult);

private:
	UPROPERTY()
	TArray<TScriptInterface<IHermesActionHandler>> Handlers;

	FHermesRateLimiter RateLimiter;
	bool bRateLimiterConfigured = false;
};
