#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "ItemTransferActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UItemTransferActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("item_transfer"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;

private:
	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
