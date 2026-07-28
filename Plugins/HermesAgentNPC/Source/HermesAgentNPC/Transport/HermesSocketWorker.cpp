#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesFrameCodec.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"
#include "IPAddress.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Math/UnrealMathUtility.h"

FHermesSocketWorker::FHermesSocketWorker(const FHermesWorkerConfig& InConfig)
	: Config(InConfig)
{
}

FHermesSocketWorker::~FHermesSocketWorker()
{
	RequestStop();
	if (Thread)
	{
		Thread->Kill(true); // 스레드 종료 대기
		delete Thread;
		Thread = nullptr;
	}
	CloseSocket();
}

void FHermesSocketWorker::Start()
{
	Thread = FRunnableThread::Create(this, TEXT("HermesSocketWorker"));
}

void FHermesSocketWorker::EnqueueOutbound(const FString& Json)
{
	// 아웃바운드 적체는 피어의 악의가 아니라 연결 단절의 결과다. 끊어봐야
	// 나아지지 않으므로 연결은 유지하고 새 프레임만 버린다.
	// Outbound 는 SPSC 큐라 producer(게임 스레드)가 Dequeue 할 수 없어
	// "가장 오래된 것 버리기"는 성립하지 않는다.
	if (OutboundCount.GetValue() >= Config.MaxOutboundQueueSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] outbound queue full (%d), dropping frame"),
			Config.MaxOutboundQueueSize);
		return;
	}
	Outbound.Enqueue(Json);
	OutboundCount.Increment();
}

bool FHermesSocketWorker::DequeueInbound(FString& OutJson)
{
	if (Inbound.Dequeue(OutJson))
	{
		InboundCount.Decrement();
		return true;
	}
	return false;
}
void FHermesSocketWorker::RequestStop() { bStopRequested = true; }
void FHermesSocketWorker::Stop() { bStopRequested = true; }

bool FHermesSocketWorker::ConnectSocket()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return false;
	}

	// 호스트명과 IP를 모두 처리한다. 컨테이너명·k8s 서비스명 같은 유동 주소 대응.
	// 동기 호출이라 도달 불가 호스트명에서 OS DNS 타임아웃까지 멈추지만,
	// 이 함수는 워커 전용 스레드에서만 실행되므로 게임 스레드는 영향받지 않는다.
	FAddressInfoResult Result = SS->GetAddressInfo(*Config.Host, nullptr,
		EAddressInfoFlags::Default, NAME_None);
	if (Result.ReturnCode != SE_NO_ERROR || Result.Results.Num() == 0)
	{
		return false;
	}

	// IPv4 우선. 듀얼스택에서 OS가 IPv6를 먼저 주더라도 서버가 IPv4만 수신하면
	// 접속이 실패하므로, 기존 환경의 동작을 바꾸지 않기 위해 IPv4를 먼저 고른다.
	const FAddressInfoResultData* Chosen = &Result.Results[0];
	for (const FAddressInfoResultData& R : Result.Results)
	{
		if (R.Address->GetProtocolType() == FNetworkProtocolTypes::IPv4)
		{
			Chosen = &R;
			break;
		}
	}

	TSharedRef<FInternetAddr> InetAddr = Chosen->Address->Clone();
	InetAddr->SetPort(Config.Port);

	// 소켓은 선택된 주소의 프로토콜을 따라야 한다.
	// FTcpSocketBuilder 는 FIPv4Endpoint 에서 프로토콜을 유도하므로 구조적으로
	// IPv4 전용이다. IPv6 폴백을 지원하려면 CreateSocket 을 직접 불러야 한다.
	Socket = SS->CreateSocket(NAME_Stream, TEXT("HermesClient"),
		Chosen->Address->GetProtocolType());
	if (!Socket)
	{
		return false;
	}

	// 빌더의 AsBlocking() 과 동일하게 맞춘다. 연결 후 논블로킹으로 전환한다.
	Socket->SetNonBlocking(false);

	if (!Socket->Connect(*InetAddr))
	{
		CloseSocket();
		return false;
	}

	Socket->SetNonBlocking(true);      // 연결 후 논블로킹 수신으로 전환
	Accumulator = FFrameAccumulator(); // 새 연결마다 파서 리셋
	return true;
}

void FHermesSocketWorker::CloseSocket()
{
	bConnected = false;
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SS->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

bool FHermesSocketWorker::SendAllPending()
{
	FString Json;
	while (Outbound.Dequeue(Json))
	{
		OutboundCount.Decrement();
		TArray<uint8> Bytes;
		if (!FHermesFrameCodec::Encode(Json, Bytes))
		{
			continue; // 인코드 불가한 프레임은 스킵
		}
		int32 Total = 0;
		while (Total < Bytes.Num())
		{
			int32 Sent = 0;
			if (!Socket->Send(Bytes.GetData() + Total, Bytes.Num() - Total, Sent) || Sent < 0)
			{
				return false; // 송신 실패 → 재연결
			}
			if (Sent == 0)
			{
				FPlatformProcess::Sleep(0.001f); // 송신 버퍼 가득 참: 잠시 양보
				continue;
			}
			Total += Sent;
		}
	}
	return true;
}

bool FHermesSocketWorker::ReceiveAvailable()
{
	uint8 Buf[4096];
	uint32 Pending = 0;
	while (Socket->HasPendingData(Pending))
	{
		int32 Read = 0;
		if (!Socket->Recv(Buf, sizeof(Buf), Read))
		{
			return false; // 수신 에러 → 재연결
		}
		if (Read <= 0)
		{
			break;
		}
		Accumulator.Feed(Buf, Read);
		if (Accumulator.HasError())
		{
			return false; // 프레이밍 위반 → 연결 종료
		}
		FString Json;
		while (Accumulator.TryPop(Json))
		{
			// 정상 서버라면 게임 스레드가 매 틱 비우므로 이 선에 닿지 않는다.
			// 도달했다는 것은 피어가 소비 속도를 무시하고 밀어넣고 있다는 뜻이다.
			// 프레이밍 위반과 같은 경로로 연결을 끊고 백오프 재연결에 맡긴다.
			if (InboundCount.GetValue() >= Config.MaxInboundQueueSize)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Hermes] inbound queue overflow (%d), closing connection"),
					Config.MaxInboundQueueSize);
				return false;
			}
			Inbound.Enqueue(Json);
			InboundCount.Increment();
		}
	}
	return true;
}

void FHermesSocketWorker::InterruptibleSleep(float Seconds)
{
	// 통짜 Sleep 은 소멸자의 Thread->Kill(true) 대기를 그만큼 늘린다.
	// MaxReconnectDelay 가 설정으로 최대 300초까지 열려 있어 조각내지 않으면
	// PIE 정지가 수 분간 멈출 수 있다.
	constexpr float Slice = 0.1f;
	float Remaining = Seconds;
	while (Remaining > 0.f && !bStopRequested)
	{
		const float Step = FMath::Min(Slice, Remaining);
		FPlatformProcess::Sleep(Step);
		Remaining -= Step;
	}
}

uint32 FHermesSocketWorker::Run()
{
	float Backoff = Config.InitialReconnectDelay;
	const float MaxBackoff = Config.MaxReconnectDelay;

	while (!bStopRequested)
	{
		if (!bConnected)
		{
			if (ConnectSocket())
			{
				bConnected = true;
				Backoff = Config.InitialReconnectDelay; // 성공 시 리셋
			}
			else
			{
				const float Jitter = FMath::FRandRange(0.f, Backoff * 0.25f);
				InterruptibleSleep(Backoff + Jitter);
				if (bStopRequested)
				{
					break;
				}
				Backoff = FMath::Min(Backoff * 2.f, MaxBackoff);
				continue;
			}
		}

		if (!SendAllPending() || !ReceiveAvailable())
		{
			CloseSocket(); // 다음 루프에서 재연결
			continue;
		}
		FPlatformProcess::Sleep(0.005f); // busy-wait 방지 (5ms)
	}

	CloseSocket();
	return 0;
}
