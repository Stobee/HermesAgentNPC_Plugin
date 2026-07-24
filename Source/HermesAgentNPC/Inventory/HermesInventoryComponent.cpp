#include "Inventory/HermesInventoryComponent.h"
#include "Inventory/HermesItem.h"
#include "Dom/JsonObject.h"

UHermesItem* UHermesInventoryComponent::Find(const FString& ItemId) const
{
	for (UHermesItem* It : Items)
	{
		if (It && It->ItemId == ItemId)
		{
			return It;
		}
	}
	return nullptr;
}

int32 UHermesInventoryComponent::GetQuantity(const FString& ItemId) const
{
	const UHermesItem* It = Find(ItemId);
	return It ? It->Quantity : 0;
}

void UHermesInventoryComponent::Add(const FString& ItemId, int32 Qty)
{
	if (Qty <= 0)
	{
		return;
	}
	if (UHermesItem* It = Find(ItemId))
	{
		It->Quantity += Qty;
		return;
	}
	UHermesItem* New = NewObject<UHermesItem>(this);
	New->ItemId = ItemId;
	New->Quantity = Qty;
	Items.Add(New);
}

bool UHermesInventoryComponent::Remove(const FString& ItemId, int32 Qty)
{
	UHermesItem* It = Find(ItemId);
	if (!It || Qty <= 0 || It->Quantity < Qty)
	{
		return false;
	}
	It->Quantity -= Qty;
	if (It->Quantity == 0)
	{
		Items.Remove(It);
	}
	return true;
}

TArray<TSharedPtr<FJsonValue>> UHermesInventoryComponent::ListAsJson() const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const UHermesItem* It : Items)
	{
		if (!It)
		{
			continue;
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("item_id"), It->ItemId);
		O->SetNumberField(TEXT("quantity"), It->Quantity);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}
