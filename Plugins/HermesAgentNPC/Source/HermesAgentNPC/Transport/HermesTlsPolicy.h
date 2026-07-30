#pragma once
#include "CoreMinimal.h"

/**
 * TLS 설정으로부터 검증 정책을 결정하는 순수 로직.
 * 네트워크·OpenSSL 없이 단독 테스트가 가능한 부분만 여기 모은다.
 */
namespace HermesTls
{
	enum class EVerifyMode : uint8
	{
		/** 서버 공개키(SPKI) 해시 핀 검증. 자체 서명 인증서를 허용한다. */
		PinnedKey,
		/** 지정된 사설 CA 를 신뢰 저장소에 추가하고 표준 체인+호스트명 검증. */
		PrivateCa,
		/** 시스템 기본 루트 CA 로 표준 체인+호스트명 검증. */
		SystemCa
	};

	/** SNI 와 호스트명 검증에 쓸 이름. TlsServerName 이 비면 Host 를 쓴다. */
	FString ResolveServerName(const FString& Host, const FString& TlsServerName);

	/**
	 * 핀이 하나라도 있으면 핀 검증이 우선한다. 핀은 자체 서명을 허용하는
	 * 대신 정확히 그 키만 신뢰하므로 사설 CA 보다 좁고 강한 조건이다.
	 */
	EVerifyMode ResolveVerifyMode(const TArray<FString>& Pins, const FString& PrivateCaPath);

	/**
	 * Shipping 빌드에서는 설정이 false 여도 TLS 를 강제한다.
	 * 배포된 게임이 설정 실수로 평문 통신하는 상황을 만들 수 없게 한다.
	 * 빌드 구성을 인자로 받아 테스트가 두 경우를 모두 검증할 수 있게 한다.
	 */
	bool ResolveUseTls(bool bConfigured, bool bShippingBuild);
}
