#include "Settings/HermesSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UHermesSettings::ApplyCommandLineOverrides(const TCHAR* CmdLine,
                                                FString& InOutHost, int32& InOutPort)
{
#if UE_BUILD_SHIPPING
	// 배포 빌드에서는 최종 사용자가 클라이언트를 임의 서버로 리다이렉트하지 못하게 한다.
	// 주소 은닉이 목적이 아니라(주소는 패킷 캡처로 드러난다) 기본 경로를 막는 것이 목적이다.
	(void)CmdLine; (void)InOutHost; (void)InOutPort;
	return;
#else
	if (!CmdLine)
	{
		return;
	}

	FString HostOverride;
	if (FParse::Value(CmdLine, TEXT("HermesHost="), HostOverride) && !HostOverride.IsEmpty())
	{
		InOutHost = HostOverride;
	}

	int32 PortOverride = 0;
	if (FParse::Value(CmdLine, TEXT("HermesPort="), PortOverride))
	{
		// 범위 밖이면 ini 값을 유지한다. 잘못된 인자로 연결이 조용히 깨지지 않게 한다.
		if (PortOverride >= 1 && PortOverride <= 65535)
		{
			InOutPort = PortOverride;
		}
	}
#endif
}

void UHermesSettings::GetResolvedEndpoint(FString& OutHost, int32& OutPort) const
{
	OutHost = Host;
	OutPort = Port;
	ApplyCommandLineOverrides(FCommandLine::Get(), OutHost, OutPort);
}
