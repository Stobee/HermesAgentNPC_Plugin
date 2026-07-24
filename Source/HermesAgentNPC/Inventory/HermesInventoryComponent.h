#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "HermesInventoryComponent.generated.h"

class UHermesItem;

/** UObject 아이템을 TArray 로 보유하는 최소 인벤토리. */
UCLASS(ClassGroup=(Hermes))
class UHermesInventoryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	int32 GetQuantity(const FString& ItemId) const;
	void Add(const FString& ItemId, int32 Qty);
	bool Remove(const FString& ItemId, int32 Qty); // 부족 시 false
	TArray<TSharedPtr<FJsonValue>> ListAsJson() const;

private:
	UHermesItem* Find(const FString& ItemId) const;

	UPROPERTY()
	TArray<UHermesItem*> Items;
};
