#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "Protocol/HermesFrameCodec.h"
#include "Transport/HermesWorkerConfig.h"
#include "Transport/IHermesTransport.h"

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

	/** 연결이 성립할 때마다 증가한다. 0 은 "아직 한 번도 붙지 않았다". */
	uint32 GetConnectionGeneration() const { return (uint32)ConnectionGeneration.GetValue(); }
	void RequestStop();
	/** 현재 연결을 끊고 재연결 루프로 돌아가게 한다. 게임 스레드에서 호출. */
	void RequestReconnect();

	/**
	 * 재연결 루프를 멈춘다. 재시도로 낫지 않는 종료성 에러(session_taken_over,
	 * unsupported_version)에 쓴다. 재개는 ResumeReconnect() 뿐이다.
	 */
	void SuspendReconnect();

	/** 정지를 해제한다. 게임이 의도적으로 다시 붙을 때만 호출한다. */
	void ResumeReconnect();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	bool ConnectSocket();
	void CloseSocket();
	bool SendAllPending();   // 아웃바운드 큐 flush
	bool ReceiveAvailable(); // 논블로킹 recv → accumulator → inbound 큐

	/**
	 * 총 Seconds 만큼 자되 100ms 마다 깨어나 중단·정지 요청을 확인한다.
	 * 재연결 백오프 대기에만 쓴다.
	 */
	void InterruptibleSleep(float Seconds);

	FHermesWorkerConfig Config;

	TUniquePtr<IHermesTransport> Transport;
	FRunnableThread* Thread = nullptr;
	FFrameAccumulator Accumulator;

	TQueue<FString, EQueueMode::Spsc> Outbound;
	TQueue<FString, EQueueMode::Spsc> Inbound;

	// TQueue 는 크기 조회를 제공하지 않으므로 카운터를 따로 둔다.
	FThreadSafeCounter InboundCount;
	FThreadSafeCounter OutboundCount;
	FThreadSafeCounter ConnectionGeneration;

	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bReconnectRequested = false;
	FThreadSafeBool bConnected = false;

	// 워커 스레드와 게임 스레드가 함께 읽는다.
	FThreadSafeBool bReconnectSuspended = false;
};
