#include "Transport/HermesTlsPolicy.h"

namespace HermesTls
{
	FString ResolveServerName(const FString& Host, const FString& TlsServerName)
	{
		const FString Trimmed = TlsServerName.TrimStartAndEnd();
		return Trimmed.IsEmpty() ? Host : Trimmed;
	}

	EVerifyMode ResolveVerifyMode(const TArray<FString>& Pins, const FString& PrivateCaPath)
	{
		// 공백뿐인 항목은 핀으로 치지 않는다. ini 편집 실수로 핀 목록이
		// 비어 있는데 핀 모드로 들어가면 검증이 무력해진다.
		for (const FString& Pin : Pins)
		{
			if (!Pin.TrimStartAndEnd().IsEmpty())
			{
				return EVerifyMode::PinnedKey;
			}
		}

		if (!PrivateCaPath.TrimStartAndEnd().IsEmpty())
		{
			return EVerifyMode::PrivateCa;
		}
		return EVerifyMode::SystemCa;
	}

	bool ResolveUseTls(bool bConfigured, bool bShippingBuild)
	{
		return bShippingBuild ? true : bConfigured;
	}
}
