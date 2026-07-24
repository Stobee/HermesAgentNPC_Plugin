#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "Dom/JsonObject.h"
#include "HermesTestActionHandler.generated.h"

/** 자동화 테스트 전용: "echo" command 만 처리하고 즉시 성공으로 회신한다. */
UCLASS()
class UHermesTestActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	virtual bool CanHandle(const FString& Command) const override
	{
		return Command == TEXT("echo");
	}
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("echoed"), true);
		OnDone.ExecuteIfBound(true, R, FString());
	}
};
