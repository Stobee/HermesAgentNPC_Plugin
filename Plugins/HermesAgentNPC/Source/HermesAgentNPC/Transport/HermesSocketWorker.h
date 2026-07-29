#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "Protocol/HermesFrameCodec.h"
#include "Transport/HermesWorkerConfig.h"

class FSocket;
class FRunnableThread;

/**
 * 전용 워커 스레드에서 TCP 소켓을 소유하고, 게임 스레드와는 두 개의 SPSC TQueue로만 통신한다.
 * 연결 끊김을 감지하면 지수 백오프로 자동 재연결한다.
 */
class FHermesSocketWorker : public FRunnable
{
public:
	explicit FHermesSocketWorker(const FHermesWorkerConfig& InConfig);
	virtual ~FHermesSocketWorker() override;

	void Start();                            // 스레드 생성
	void EnqueueOutbound(const FString& Json); // 게임 → 워커
	bool DequeueInbound(FString& OutJson);     // 워커 → 게임
	bool IsConnected() const { return bConnected; }
	void RequestStop();
	/** 현재 연결을 끊고 재연결 루프로 돌아가게 한다. 게임 스레드에서 호출. */
	void RequestReconnect();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	bool ConnectSocket();
	void CloseSocket();
	bool SendAllPending();   // 아웃바운드 큐 flush
	bool ReceiveAvailable(); // 논블로킹 recv → accumulator → inbound 큐

	/** 총 Seconds 만큼 자되 100ms 마다 깨어나 중단 요청을 확인한다. */
	void InterruptibleSleep(float Seconds);

	FHermesWorkerConfig Config;

	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	FFrameAccumulator Accumulator;

	TQueue<FString, EQueueMode::Spsc> Outbound;
	TQueue<FString, EQueueMode::Spsc> Inbound;

	// TQueue 는 크기 조회를 제공하지 않으므로 카운터를 따로 둔다.
	FThreadSafeCounter InboundCount;
	FThreadSafeCounter OutboundCount;

	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bReconnectRequested = false;
	FThreadSafeBool bConnected = false;
};
