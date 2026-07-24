#include "Actions/HermesActionDispatcher.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UHermesActionDispatcher::RegisterHandler(TScriptInterface<IHermesActionHandler> Handler)
{
	if (Handler)
	{
		Handlers.Add(Handler);
	}
}

void UHermesActionDispatcher::Dispatch(const FHermesActionPayload& Payload,
	TFunction<void(const FString&)> OnResult)
{
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
	const FString Id = Payload.Id;

	FHermesActionResultDelegate OnDone;
	OnDone.BindLambda([bDone, Id, OnResult](bool bOk, TSharedPtr<FJsonObject> Result, FString Error)
	{
		if (*bDone)
		{
			return;
		}
		*bDone = true;
		OnResult(HermesJson::MakeActionResult(Id, bOk, Result, Error));
	});

	Chosen->Execute(Payload, OnDone);

	// 15초 타임아웃 폴백 (World 가 있을 때만; 즉시성 핸들러엔 bDone 가드로 무해)
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Th;
		World->GetTimerManager().SetTimer(Th, [bDone, Id, OnResult]()
		{
			if (*bDone)
			{
				return;
			}
			*bDone = true;
			OnResult(HermesJson::MakeActionResult(Id, false, nullptr, TEXT("timeout")));
		}, 15.f, false);
	}
}
