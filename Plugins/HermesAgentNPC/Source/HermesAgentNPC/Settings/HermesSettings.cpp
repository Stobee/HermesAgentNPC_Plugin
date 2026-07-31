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

void UHermesSettings::ApplyTlsOverride(const TCHAR* CmdLine, bool& InOutUseTLS)
{
#if UE_BUILD_SHIPPING
	// Host/Port 와 같은 이유로 배포 빌드에서는 받지 않는다. 어차피
	// HermesTls::ResolveUseTls 가 Shipping 에서 TLS 를 강제하므로 이중 방어다.
	(void)CmdLine; (void)InOutUseTLS;
	return;
#else
	if (!CmdLine)
	{
		return;
	}

	FString Raw;
	if (!FParse::Value(CmdLine, TEXT("HermesUseTLS="), Raw))
	{
		return;
	}

	const FString Value = Raw.TrimStartAndEnd().ToLower();
	if (Value == TEXT("1") || Value == TEXT("true") || Value == TEXT("yes") || Value == TEXT("on"))
	{
		InOutUseTLS = true;
	}
	else if (Value == TEXT("0") || Value == TEXT("false") || Value == TEXT("no") || Value == TEXT("off"))
	{
		InOutUseTLS = false;
	}
	// 해석할 수 없는 값은 ini 를 유지한다. 오타 하나로 평문 통신이 되면 안 된다.
#endif
}

void UHermesSettings::GetResolvedEndpoint(FString& OutHost, int32& OutPort) const
{
	OutHost = Host;
	OutPort = Port;
	ApplyCommandLineOverrides(FCommandLine::Get(), OutHost, OutPort);
}

bool UHermesSettings::GetResolvedUseTLS() const
{
	bool bResolved = bUseTLS;
	ApplyTlsOverride(FCommandLine::Get(), bResolved);
	return bResolved;
}
