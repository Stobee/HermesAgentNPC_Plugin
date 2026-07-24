#include "Actions/FollowPlayerActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Dom/JsonObject.h"

void UFollowPlayerActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid())
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable"));
		return;
	}
	bool bEnabled = false;
	if (!Payload.Params.IsValid() || !Payload.Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid enabled"));
		return;
	}
	Npc->SetFollowPlayer(bEnabled);
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("following"), bEnabled);
	OnDone.ExecuteIfBound(true, Res, FString());
}
