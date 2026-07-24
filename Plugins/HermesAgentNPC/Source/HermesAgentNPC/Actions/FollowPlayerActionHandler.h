#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "FollowPlayerActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UFollowPlayerActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("follow_player"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;

private:
	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
