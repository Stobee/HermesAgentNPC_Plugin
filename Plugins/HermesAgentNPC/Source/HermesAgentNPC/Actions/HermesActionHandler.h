#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Actions/HermesActionTypes.h"
#include "HermesActionHandler.generated.h"

UINTERFACE(MinimalAPI)
class UHermesActionHandler : public UInterface
{
	GENERATED_BODY()
};

/** 하나의 command 를 실행하는 핸들러. 새 액션은 이 인터페이스 구현체를 추가하기만 하면 된다. */
class IHermesActionHandler
{
	GENERATED_BODY()
public:
	virtual bool CanHandle(const FString& Command) const = 0;
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) = 0;
};
