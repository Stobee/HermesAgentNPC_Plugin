#include "Transport/HermesPlainTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"
#include "IPAddress.h"

FHermesPlainTransport::~FHermesPlainTransport()
{
	Close();
}

bool FHermesPlainTransport::Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr)
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return false;
	}

	// 소켓은 선택된 주소의 프로토콜을 따라야 한다.
	// FTcpSocketBuilder 는 FIPv4Endpoint 에서 프로토콜을 유도하므로 구조적으로
	// IPv4 전용이다. IPv6 폴백을 지원하려면 CreateSocket 을 직접 불러야 한다.
	Socket = SS->CreateSocket(NAME_Stream, TEXT("HermesClient"), Addr.GetProtocolType());
	if (!Socket)
	{
		return false;
	}

	// 빌더의 AsBlocking() 과 동일하게 맞춘다. 연결 후 논블로킹으로 전환한다.
	Socket->SetNonBlocking(false);

	if (!Socket->Connect(Addr))
	{
		Close();
		return false;
	}

	// 소켓 수준 keepalive 는 켜지 않는다 — UE 5.8 FSocket 에 설정자가 없다.
	// Task 12 Step 10 참조. 생존 탐지는 애플리케이션 ping 이 담당한다.
	Socket->SetNonBlocking(true);      // 연결 후 논블로킹 수신으로 전환
	return true;
}

void FHermesPlainTransport::Close()
{
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

bool FHermesPlainTransport::HasPendingData(uint32& OutBytes)
{
	return Socket && Socket->HasPendingData(OutBytes);
}

int32 FHermesPlainTransport::Recv(uint8* Buf, int32 BufSize)
{
	if (!Socket)
	{
		return -1;
	}
	int32 Read = 0;
	if (!Socket->Recv(Buf, BufSize, Read))
	{
		return -1; // 수신 에러 → 재연결
	}
	return FMath::Max(0, Read);
}

int32 FHermesPlainTransport::Send(const uint8* Buf, int32 Num)
{
	if (!Socket)
	{
		return -1;
	}
	int32 Sent = 0;
	if (!Socket->Send(Buf, Num, Sent) || Sent < 0)
	{
		return -1; // 송신 실패 → 재연결
	}
	return Sent;
}
