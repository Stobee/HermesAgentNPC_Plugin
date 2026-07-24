#include "Actions/ItemTransferActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Dom/JsonObject.h"

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
		!Payload.Params->TryGetNumberField(TEXT("quantity"), Qd) || Qd < 1)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid params"));
		return;
	}
	const int32 Qty = (int32)Qd;

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
