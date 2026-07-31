#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
// TUniquePtr<FHermesSocketWorker> 멤버 때문에 완전한 타입이 필요하다.
// UHT가 생성하는 생성자가 멤버 소멸자를 인스턴스화하므로 전방 선언으로는 부족하다.
#include "Transport/HermesSocketWorker.h"
#include "Connection/HermesPendingChats.h"
#include "Connection/HermesSuspendState.h"
#include "HermesConnectionSubsystem.generated.h"

class UHermesActionDispatcher;
class AHermesNPCCharacter;
enum class EHermesErrorReaction : uint8;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatResponse, const FString&, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatDelta, const FString& /*Text*/, const FString& /*Id*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatFailed, const FString& /*Id*/, const FString& /*Reason*/);

/**
 * 재연결 루프가 정지되었다. 게임이 재접속 UI 를 띄울 시점이다.
 * 기존 델리게이트들과 달리 dynamic 이다 — 블루프린트가 구독해야 하기 때문이다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReconnectSuspended, const FString&, Reason);

UCLASS()
class HERMESAGENTNPC_API UHermesConnectionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void SendChat(const FString& Text);

	/** 장기 실행 액션의 완료/실패를 서버에 알린다. 액션 핸들러가 호출한다. */
	void SendActionEvent(const FString& Id, bool bCompleted,
	                     const TSharedPtr<class FJsonObject>& Result, const FString& Error);

	/**
	 * 종료성 에러로 멈춘 재연결 루프를 다시 돌린다.
	 *
	 * 재개했으면 true. 정지 상태가 아니거나 쿨다운 중이면 false 다.
	 * 쿨다운은 플러그인이 강제한다 — 게임이 이 함수를 Tick 에 걸어도
	 * eviction war 로 번지지 않는다. 남은 대기는
	 * GetReconnectCooldownRemaining() 으로 알 수 있다.
	 */
	UFUNCTION(BlueprintCallable, Category="Hermes")
	bool Reconnect();

	/** 종료성 에러로 정지되어 Reconnect() 를 기다리는 상태인가. */
	UFUNCTION(BlueprintPure, Category="Hermes")
	bool IsReconnectSuspended() const;

	/**
	 * 지금 Reconnect() 가 거부될 때 남은 대기(초). 0 이면 즉시 가능.
	 * 정지 상태가 아니면 0 이다 — 기다릴 것이 없다.
	 */
	UFUNCTION(BlueprintPure, Category="Hermes")
	float GetReconnectCooldownRemaining() const;

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

	/** 부분 응답. 표시용 힌트이며 정본은 OnChatResponse 가 전달하는 최종 텍스트다. */
	FOnChatDelta OnChatDelta;

	/** 발화가 응답 없이 만료되었거나 연결 단절로 폐기되었음을 알린다. */
	FOnChatFailed OnChatFailed;

	/** 재연결 루프가 정지되는 순간. Reason 은 사용자에게 보여줄 수 있는 사유. */
	UPROPERTY(BlueprintAssignable, Category="Hermes")
	FOnReconnectSuspended OnReconnectSuspended;

	const FString& GetLastSentChatId() const { return LastSentChatId; }

private:
	bool Tick(float DeltaTime);              // 게임스레드 인바운드 소비
	void HandleFrame(const TSharedPtr<class FJsonObject>& Obj);
	void SendJson(const FString& Json);      // 워커 아웃바운드로
	/** SaveGame 에서 자격 증명을 읽는다. 없으면 PlayerId/SessionToken 이 빈 채로 남는다. */
	void LoadCredentials();
	/** 서버가 발급한 자격 증명을 SaveGame 에 저장한다. 실패해도 연결은 유지한다. */
	void SaveCredentials();
	void SendIdentify();
	void FlushPendingChats();

	/** 연결이 사라졌을 때의 정리. 진행 중이던 발화를 실패시키고 상태를 알린다. */
	void HandleConnectionLost(double NowSeconds);
	/** 새 연결에 신원을 밝히고 생존 타이머를 초기화한다. */
	void HandleConnectionEstablished(double NowSeconds);

	/** error 프레임의 반응을 실행한다. 판정은 HermesErrorPolicy 가 한다. */
	void ApplyErrorReaction(EHermesErrorReaction Reaction, const FString& Code,
	                        const FString& Message, const FString& Id);

	TUniquePtr<FHermesSocketWorker> Worker;
	FTSTicker::FDelegateHandle TickHandle;

	UPROPERTY()
	UHermesActionDispatcher* Dispatcher = nullptr;

	/** 액션이 향하는 단 하나의 NPC. 레벨 전환 등으로 파괴되면 자동으로 무효화된다. */
	TWeakObjectPtr<AHermesNPCCharacter> ActiveNpc;

	FString PlayerId;
	FString SessionToken;
	bool bIdentified = false;
	bool bWasConnected = false;
	uint32 SeenConnectionGeneration = 0;
	FHermesSuspendState SuspendState;
	/** 현재 연결이 성립한 시각. 끝날 때 얼마나 살았는지 재는 데 쓴다. */
	double ConnectionOpenedAt = 0.0;
	int32 ChatCounter = 0;
	int32 PingCounter = 0;

	// 종류를 가리지 않는 마지막 수신/송신 시각. 연결 성립 시점에 초기화된다.
	double LastRecvTime = 0.0;
	double LastSendTime = 0.0;
	TArray<FString> PendingChats; // identified 전 보류

	FHermesPendingChats InFlightChats;

	/** 가장 최근에 보낸 발화 id. 위젯의 상관 규칙이 이 값을 기준으로 삼는다. */
	FString LastSentChatId;
};
