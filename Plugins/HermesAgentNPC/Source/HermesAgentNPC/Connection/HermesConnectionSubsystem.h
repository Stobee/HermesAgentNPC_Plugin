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
	void RegisterNpc(AHermesNPCCharacter* Npc);

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

	FString PlayerId;
	bool bIdentified = false;
	bool bWasConnected = false;
	int32 ChatCounter = 0;
	TArray<FString> PendingChats; // identified 전 보류
};
