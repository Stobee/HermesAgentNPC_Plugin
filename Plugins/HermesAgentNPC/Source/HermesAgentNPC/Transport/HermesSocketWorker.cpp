#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesFrameCodec.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
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

void FHermesSocketWorker::EnqueueOutbound(const FString& Json) { Outbound.Enqueue(Json); }
bool FHermesSocketWorker::DequeueInbound(FString& OutJson) { return Inbound.Dequeue(OutJson); }
void FHermesSocketWorker::RequestStop() { bStopRequested = true; }
void FHermesSocketWorker::Stop() { bStopRequested = true; }

bool FHermesSocketWorker::ConnectSocket()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return false;
	}

	FIPv4Address Addr;
	if (!FIPv4Address::Parse(Config.Host, Addr))
	{
		return false;
	}

	TSharedRef<FInternetAddr> InetAddr = SS->CreateInternetAddr();
	InetAddr->SetIp(Addr.Value);
	InetAddr->SetPort(Config.Port);

	Socket = FTcpSocketBuilder(TEXT("HermesClient")).AsBlocking().Build();
	if (!Socket)
	{
		return false;
	}

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
			Inbound.Enqueue(Json);
		}
	}
	return true;
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
				FPlatformProcess::Sleep(Backoff + Jitter);
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
