#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesFrameCodec.h"
#include "HermesLog.h"
#include "Transport/HermesTlsTransport.h"
#include "Transport/HermesPlainTransport.h"
#include "Transport/HermesBackoff.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"
#include "IPAddress.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
// 연결 수명을 재는 FPlatformTime::Seconds() 때문에 필요하다. 에디터 빌드는
// unity/PCH 로 전이적으로 얻지만 플러그인 패키징 빌드에는 없다.
#include "HAL/PlatformTime.h"
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
		UE_LOG(LogHermes, Warning, TEXT("outbound queue full (%d), dropping frame"),
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
void FHermesSocketWorker::RequestReconnect() { bReconnectRequested = true; }
void FHermesSocketWorker::SuspendReconnect() { bReconnectSuspended = true; }

void FHermesSocketWorker::ResumeReconnect()
{
	// 정지 중에 쌓인 재연결 요청은 의미가 없다. 남겨두면 재개 직후 새로 맺은
	// 연결을 곧바로 끊어 한 번을 헛돈다.
	bReconnectRequested = false;
	bReconnectSuspended = false;
}
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

	// TLS 실패는 평문 폴백으로 이어지지 않는다. 연결을 닫고 백오프 재연결에 맡긴다.
	if (Config.Tls.bUseTLS)
	{
		Transport = MakeUnique<FHermesTlsTransport>();
	}
	else
	{
		UE_LOG(LogHermes, Warning,
			TEXT("connecting WITHOUT TLS to %s:%d — development only"),
			*Config.Host, Config.Port);
		Transport = MakeUnique<FHermesPlainTransport>();
	}

	if (!Transport->Connect(Config, *InetAddr))
	{
		Transport.Reset();
		return false;
	}

	Accumulator = FFrameAccumulator(); // 새 연결마다 파서 리셋
	return true;
}

void FHermesSocketWorker::CloseSocket()
{
	bConnected = false;
	if (Transport)
	{
		Transport->Close();
		Transport.Reset();
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
			const int32 Sent = Transport->Send(Bytes.GetData() + Total, Bytes.Num() - Total);
			if (Sent < 0)
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
	while (Transport->HasPendingData(Pending))
	{
		const int32 Read = Transport->Recv(Buf, sizeof(Buf));
		if (Read < 0)
		{
			return false; // 수신 에러 → 재연결
		}
		if (Read == 0)
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
				UE_LOG(LogHermes, Warning,
					TEXT("inbound queue overflow (%d), closing connection"),
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
	// 정지가 걸리면 남은 백오프를 더 기다릴 이유가 없다. 즉시 빠져나가 루프
	// 상단의 정지 처리로 넘긴다.
	while (Remaining > 0.f && !bStopRequested && !bReconnectSuspended)
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
	double ConnectedAt = 0.0;

	// 재시도 전 대기. 접속 실패와 "붙자마자 끊긴 연결" 을 같은 사다리로 다룬다.
	auto WaitBeforeRetry = [this, &Backoff, MaxBackoff]()
	{
		const float Jitter = FMath::FRandRange(0.f, Backoff * 0.25f);
		InterruptibleSleep(Backoff + Jitter);
	};

	// 연결이 끝났다. 얼마나 살아 있었는지로 사다리를 되돌릴지 정한다.
	// 붙었다는 사실만으로 되돌리면 서버가 매번 즉시 끊을 때 재연결이
	// 왕복 지연 속도로 돈다 (HermesBackoff 주석 참고).
	auto EndConnection = [this, &Backoff, &ConnectedAt, MaxBackoff, &WaitBeforeRetry]()
	{
		const double Lifetime = FPlatformTime::Seconds() - ConnectedAt;
		const bool bHealthy = HermesBackoff::WasHealthy(Lifetime, Config.HealthyConnectionSeconds);

		Backoff = HermesBackoff::NextAfterDisconnect(
			Backoff, Lifetime, Config.HealthyConnectionSeconds,
			Config.InitialReconnectDelay, MaxBackoff);

		CloseSocket();

		if (!bHealthy)
		{
			UE_LOG(LogHermes, Verbose,
				TEXT("connection lasted %.2fs (< %.1fs); backing off %.1fs before retry"),
				Lifetime, Config.HealthyConnectionSeconds, Backoff);
			WaitBeforeRetry();
		}
	};

	while (!bStopRequested)
	{
		// 종료성 에러로 정지되었다. 연결을 끊고 재개 요청까지 아무것도 하지 않는다.
		//
		// 스레드를 실제로 끝내지 않고 유휴로 두는 이유: 끝내면 ResumeReconnect()
		// 가 스레드를 다시 만들어야 하고, 그 시점에 Thread 포인터를 게임 스레드와
		// 종료 중인 워커가 함께 만지는 경쟁이 생긴다. 100ms 조각으로 자는 스레드
		// 하나의 비용은 그 위험보다 싸다. 소멸자 경로(RequestStop → Kill)는
		// bStopRequested 검사로 그대로 동작한다.
		if (bReconnectSuspended)
		{
			if (bConnected)
			{
				CloseSocket();
			}
			FPlatformProcess::Sleep(0.1f);
			continue;
		}

		if (!bConnected)
		{
			if (ConnectSocket())
			{
				// bConnected 보다 먼저 올린다. 게임 스레드가 bConnected 를 참으로
				// 본 시점에는 세대가 이미 새 값이어야 새 연결을 놓치지 않는다.
				ConnectionGeneration.Increment();

				// 연결 성립을 identify 와 독립적으로 남긴다. 둘의 개수를 비교하는
				// 것이 "재연결마다 신원을 밝히는가" 회귀의 감시선이다.
				UE_LOG(LogHermes, Verbose, TEXT("transport connected to %s:%d (generation %d)"),
					*Config.Host, Config.Port, ConnectionGeneration.GetValue());

				bConnected = true;
				ConnectedAt = FPlatformTime::Seconds();
				// 여기서 Backoff 를 되돌리지 않는다. 붙었다는 것만으로는
				// 사다리를 초기화할 근거가 못 된다 — EndConnection 이 연결이
				// 얼마나 살아 있었는지를 보고 정한다.
			}
			else
			{
				WaitBeforeRetry();
				if (bStopRequested)
				{
					break;
				}
				Backoff = HermesBackoff::NextAfterFailedConnect(
					Backoff, Config.InitialReconnectDelay, MaxBackoff);
				continue;
			}
		}

		if (bReconnectRequested)
		{
			bReconnectRequested = false;
			EndConnection();     // 다음 루프에서 재연결
			continue;
		}

		if (!SendAllPending() || !ReceiveAvailable())
		{
			EndConnection(); // 다음 루프에서 재연결
			continue;
		}
		FPlatformProcess::Sleep(0.005f); // busy-wait 방지 (5ms)
	}

	CloseSocket();
	return 0;
}
