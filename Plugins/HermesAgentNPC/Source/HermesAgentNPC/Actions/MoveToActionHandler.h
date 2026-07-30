#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
// UFUNCTION 인자로 쓰므로 FAIRequestID 와 EPathFollowingResult 의 완전한 정의가 필요하다.
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "MoveToActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UMoveToActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("move_to"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;

private:
	/** 진행 중인 이동의 action_request id. 완료 통지에 그대로 쓴다. */
	FString PendingMoveId;

	UFUNCTION()
	void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);

	/** 이 NPC 가 속한 게임 인스턴스의 연결 구독 시스템. 없으면 nullptr. */
	class UHermesConnectionSubsystem* GetConnection() const;

	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
