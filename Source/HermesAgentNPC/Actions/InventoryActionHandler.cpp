#include "Actions/InventoryActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Dom/JsonObject.h"

void UInventoryActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
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

	FString Op;
	if (!Payload.Params.IsValid() || !Payload.Params->TryGetStringField(TEXT("operation"), Op))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid operation"));
		return;
	}

	if (Op == TEXT("list"))
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetArrayField(TEXT("items"), Inv->ListAsJson());
		OnDone.ExecuteIfBound(true, Res, FString());
		return;
	}
	if (Op == TEXT("drop"))
	{
		FString Target;
		if (Payload.Params->TryGetStringField(TEXT("target"), Target) &&
			Inv->Remove(Target, Inv->GetQuantity(Target)))
		{
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("dropped"), Target);
			OnDone.ExecuteIfBound(true, Res, FString());
			return;
		}
		OnDone.ExecuteIfBound(false, nullptr, TEXT("drop target not found"));
		return;
	}
	if (Op == TEXT("sort"))
	{
		// 최소 구현: 성공 처리 (정렬 로직은 확장 지점)
		OnDone.ExecuteIfBound(true, MakeShared<FJsonObject>(), FString());
		return;
	}
	OnDone.ExecuteIfBound(false, nullptr, TEXT("unsupported operation"));
}
