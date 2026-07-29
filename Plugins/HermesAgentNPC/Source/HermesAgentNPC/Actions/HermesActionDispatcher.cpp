#include "Actions/HermesActionDispatcher.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Settings/HermesSettings.h"
#include "HAL/PlatformTime.h"

void UHermesActionDispatcher::RegisterHandler(TScriptInterface<IHermesActionHandler> Handler)
{
	if (Handler)
	{
		Handlers.Add(Handler);
	}
}

void UHermesActionDispatcher::ResetHandlers()
{
	Handlers.Reset();
}

void UHermesActionDispatcher::Dispatch(const FHermesActionPayload& Payload,
	TFunction<void(const FString&)> OnResult)
{
	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	if (!bRateLimiterConfigured)
	{
		RateLimiter.Configure(Settings->MaxActionsPerSecond);
		bRateLimiterConfigured = true;
	}

	// 정상 서버라면 이 선에 닿지 않는다. 도달했다는 것은 폭주이거나
	// 프롬프트 인젝션으로 액션이 남발되고 있다는 신호다.
	if (!RateLimiter.TryConsume(FPlatformTime::Seconds()))
	{
		OnResult(HermesJson::MakeActionResult(Payload.Id, false, nullptr, TEXT("rate limited")));
		return;
	}

	IHermesActionHandler* Chosen = nullptr;
	for (const TScriptInterface<IHermesActionHandler>& H : Handlers)
	{
		if (H && H->CanHandle(Payload.Command))
		{
			Chosen = H.GetInterface();
			break;
		}
	}

	if (!Chosen)
	{
		// 화이트리스트 미등록: 즉시 거부
		OnResult(HermesJson::MakeActionResult(Payload.Id, false, nullptr, TEXT("unsupported command")));
		return;
	}

	// 핸들러 응답과 타임아웃 중 하나만 회신하도록 가드
	TSharedRef<bool> bDone = MakeShared<bool>(false);
	TSharedRef<FTimerHandle> Th = MakeShared<FTimerHandle>();
	TWeakObjectPtr<UHermesActionDispatcher> WeakThis(this);
	const FString Id = Payload.Id;

	FHermesActionResultDelegate OnDone;
	OnDone.BindLambda([bDone, Th, WeakThis, Id, OnResult](bool bOk, TSharedPtr<FJsonObject> Result, FString Error)
	{
		if (*bDone)
		{
			return;
		}
		*bDone = true;

		// 즉시 완료되는 핸들러가 대부분이다. 타이머를 회수하지 않으면
		// 액션마다 만료까지 살아남아 자원이 누적된다.
		if (UHermesActionDispatcher* Self = WeakThis.Get())
		{
			if (UWorld* W = Self->GetWorld())
			{
				W->GetTimerManager().ClearTimer(*Th);
			}
		}

		OnResult(HermesJson::MakeActionResult(Id, bOk, Result, Error));
	});

	Chosen->Execute(Payload, OnDone);

	// 액션 응답 타임아웃 폴백 (World 가 있을 때만; 즉시성 핸들러엔 bDone 가드로 무해)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(*Th, [bDone, Id, OnResult]()
		{
			if (*bDone)
			{
				return;
			}
			*bDone = true;
			OnResult(HermesJson::MakeActionResult(Id, false, nullptr, TEXT("timeout")));
		}, Settings->ActionTimeoutSeconds, false);
	}
}
