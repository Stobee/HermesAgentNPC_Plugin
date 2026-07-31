#pragma once
#include "CoreMinimal.h"

/**
 * TLS 관련 설정의 값 타입 사본.
 * 워커 스레드가 UObject(UHermesSettings)를 만지지 않도록 게임 스레드에서 복사해 넘긴다.
 */
struct FHermesTlsConfig
{
	bool            bUseTLS = true;
	/** 비어 있으면 Host를 검증 이름으로 쓴다. */
	FString         ServerName;
	TArray<FString> PinnedPublicKeyHashes;
	/** 게임 스레드에서 절대 경로로 변환해 넘긴다. 워커는 경로 API를 쓰지 않는다. */
	FString         PrivateCaPath;
	float           HandshakeTimeoutSeconds = 10.f;
};

/** FHermesSocketWorker 에 넘기는 설정 묶음. 전부 값 타입이라 스레드 간 공유가 안전하다. */
struct FHermesWorkerConfig
{
	FString Host;
	int32   Port                  = 8770;
	float   InitialReconnectDelay = 0.5f;
	float   MaxReconnectDelay     = 30.f;
	/** 이만큼 살아 있었던 연결만 백오프 사다리를 되돌린다. HermesBackoff 참고. */
	float   HealthyConnectionSeconds = 5.f;
	int32   MaxInboundQueueSize   = 1024;
	int32   MaxOutboundQueueSize  = 256;

	FHermesTlsConfig Tls;
};
