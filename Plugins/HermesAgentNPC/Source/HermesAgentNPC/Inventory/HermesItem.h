#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HermesItem.generated.h"

/** 인벤토리 아이템 한 종. 최소 모델: 문자열 id + 수량. */
UCLASS()
class UHermesItem : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() FString ItemId;
	UPROPERTY() int32 Quantity = 0;
};
