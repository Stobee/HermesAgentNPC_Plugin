#include "Actions/ItemTransferActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Dom/JsonObject.h"
#include "Actions/HermesActionParams.h"
#include "Settings/HermesSettings.h"

void UItemTransferActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid())
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable"));
		return;
	}
	UHermesInventoryComponent* Inv = Npc->GetInventory();
	if (!Inv)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("no inventory"));
		return;
	}

	FString Direction, ItemId;
	double Qd = 0;
	if (!Payload.Params.IsValid() ||
		!Payload.Params->TryGetStringField(TEXT("direction"), Direction) ||
		!Payload.Params->TryGetStringField(TEXT("item_id"), ItemId) ||
		!Payload.Params->TryGetNumberField(TEXT("quantity"), Qd))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid params"));
		return;
	}

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	if (!HermesParams::IsValidItemId(ItemId, Settings->MaxItemIdLength))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid item_id"));
		return;
	}

	// (int32) 캐스트 이전에 범위를 확정한다. Qd > INT32_MAX 면 캐스트 자체가
	// C++ 미정의 동작이고, 통과하더라도 인벤토리 누적에서 오버플로가 난다.
	int32 Qty = 0;
	if (!HermesParams::IsValidQuantity(Qd, Settings->MaxItemQuantity, Qty))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("quantity out of range"));
		return;
	}

	// give: NPC → player (NPC 인벤토리에서 차감), receive: player → NPC (NPC 인벤토리에 추가)
	if (Direction == TEXT("give"))
	{
		if (!Inv->Remove(ItemId, Qty))
		{
			OnDone.ExecuteIfBound(false, nullptr, TEXT("insufficient quantity"));
			return;
		}
	}
	else if (Direction == TEXT("receive"))
	{
		Inv->Add(ItemId, Qty);
	}
	else
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid direction"));
		return;
	}

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetNumberField(TEXT("transferred"), Qty);
	OnDone.ExecuteIfBound(true, Res, FString());
}
