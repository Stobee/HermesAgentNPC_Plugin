#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "InventoryActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UInventoryActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("inventory_manage"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;

private:
	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
