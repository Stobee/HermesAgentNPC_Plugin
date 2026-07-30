#pragma once
#include "CoreMinimal.h"
#include "Transport/IHermesTransport.h"

#if WITH_SSL
typedef struct ssl_st      SSL;
typedef struct ssl_ctx_st  SSL_CTX;
#endif

class FSocket;

/**
 * OpenSSL 기반 TLS 전송.
 *
 * FSocket 을 TLS 로 감싸지 않고 OpenSSL 이 소켓을 직접 소유한다.
 * FSocket 의 네이티브 핸들을 얻는 공개 API 가 없기 때문이다
 * (FSocketBSD::GetNativeSocket() 은 Sockets 모듈 내부에 있다).
 * 주소 해석은 여전히 ISocketSubsystem 이 담당하므로 IPv4 우선 정책과
 * DNS 동작이 평문 경로와 동일하게 유지된다.
 */
class FHermesTlsTransport : public IHermesTransport
{
public:
	virtual ~FHermesTlsTransport() override;

	virtual bool  Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) override;
	virtual void  Close() override;
	virtual bool  HasPendingData(uint32& OutBytes) override;
	virtual int32 Recv(uint8* Buf, int32 BufSize) override;
	virtual int32 Send(const uint8* Buf, int32 Num) override;

private:
#if WITH_SSL
	/** SSL_CTX 를 만들고 검증 정책을 적용한다. */
	bool CreateContext(const FHermesTlsConfig& Tls, const FString& ServerName);

	/** 논블로킹 핸드셰이크. 타임아웃을 매 반복 확인한다. */
	bool DoHandshake(float TimeoutSeconds);

	/**
	 * 서버 공개키가 등록된 핀과 일치하는지 확인한다.
	 * 다이제스트 계산과 비교는 엔진의 ISslCertificateManager 가 수행한다.
	 */
	bool VerifyPinnedKey(const FString& ServerName) const;

	SSL_CTX* Ctx = nullptr;
	SSL*     Ssl = nullptr;
	/** InitializeSsl() 성공 여부. Close() 에서 짝을 맞춰 ShutdownSsl() 한다. */
	bool     bSslInitialized = false;
#endif

	/** OpenSSL 이 소유하는 raw 소켓 디스크립터. -1 이면 없음. */
	int32 NativeSocket = -1;
};
