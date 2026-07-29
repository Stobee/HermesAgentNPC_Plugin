#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
// TUniquePtr<FHermesSocketWorker> 멤버 때문에 완전한 타입이 필요하다.
// UHT가 생성하는 생성자가 멤버 소멸자를 인스턴스화하므로 전방 선언으로는 부족하다.
#include "Transport/HermesSocketWorker.h"
#include "HermesConnectionSubsystem.generated.h"

class UHermesActionDispatcher;
class AHermesNPCCharacter;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatResponse, const FString&, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, bool);

UCLASS()
class HERMESAGENTNPC_API UHermesConnectionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void SendChat(const FString& Text);

	/**
	 * 이 연결이 대상으로 삼을 NPC 를 지정한다. 플러그인은 NPC 한 명만 다루므로
	 * 이미 지정된 NPC 가 있으면 경고를 남기고 교체한다. 프로토콜에 NPC 식별자가
	 * 없어 두 NPC 를 동시에 둘 방법이 없다(ue5-socket-protocol.md "Scope" 참조).
	 */
	void RegisterNpc(AHermesNPCCharacter* Npc);

	/** Npc 가 현재 활성 NPC 일 때만 해제한다. 다른 NPC 의 종료는 무시한다. */
	void UnregisterNpc(AHermesNPCCharacter* Npc);

	AHermesNPCCharacter* GetActiveNpc() const { return ActiveNpc.Get(); }

	FOnChatResponse OnChatResponse;
	FOnConnectionStateChanged OnConnectionStateChanged;

private:
	bool Tick(float DeltaTime);              // 게임스레드 인바운드 소비
	void HandleFrame(const TSharedPtr<class FJsonObject>& Obj);
	void SendJson(const FString& Json);      // 워커 아웃바운드로
	FString LoadOrCreatePlayerId();
	void SendIdentify();
	void FlushPendingChats();

	TUniquePtr<FHermesSocketWorker> Worker;
	FTSTicker::FDelegateHandle TickHandle;

	UPROPERTY()
	UHermesActionDispatcher* Dispatcher = nullptr;

	/** 액션이 향하는 단 하나의 NPC. 레벨 전환 등으로 파괴되면 자동으로 무효화된다. */
	TWeakObjectPtr<AHermesNPCCharacter> ActiveNpc;

	FString PlayerId;
	bool bIdentified = false;
	bool bWasConnected = false;
	int32 ChatCounter = 0;
	TArray<FString> PendingChats; // identified 전 보류
};
