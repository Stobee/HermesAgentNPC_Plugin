#include "Transport/HermesTlsTransport.h"
#include "Transport/HermesTlsPolicy.h"
#include "HermesLog.h"
#include "IPAddress.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#if WITH_SSL
#include "Ssl.h"   // ISslManager, ISslCertificateManager, FSslModule
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#undef UI
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace
{
	/** 플랫폼 소켓을 논블로킹으로 전환한다. */
	void SetSocketNonBlocking(int32 Fd)
	{
#if PLATFORM_WINDOWS
		u_long Mode = 1;
		ioctlsocket((SOCKET)Fd, FIONBIO, &Mode);
#else
		const int Flags = fcntl(Fd, F_GETFL, 0);
		fcntl(Fd, F_SETFL, Flags | O_NONBLOCK);
#endif
	}

	void CloseNativeSocket(int32 Fd)
	{
#if PLATFORM_WINDOWS
		closesocket((SOCKET)Fd);
#else
		close(Fd);
#endif
	}
}

FHermesTlsTransport::~FHermesTlsTransport()
{
	Close();
}

bool FHermesTlsTransport::Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr)
{
#if !WITH_SSL
	// 조용히 평문으로 내려가지 않는다. 이것이 다운그레이드 금지의 첫 번째 층이다.
	UE_LOG(LogHermes, Error,
		TEXT("TLS requested but SSL module is unavailable on this platform. "
		     "Refusing to connect in plaintext."));
	return false;
#else
	const FString ServerName = HermesTls::ResolveServerName(Config.Host, Config.Tls.ServerName);

	if (!CreateContext(Config.Tls, ServerName))
	{
		Close();
		return false;
	}

	// 1) raw 소켓 생성 및 연결 (블로킹 상태로 connect 한 뒤 논블로킹 전환)
	{
		int32 Family = AF_INET;
		if (Addr.GetProtocolType() == FNetworkProtocolTypes::IPv6)
		{
			Family = AF_INET6;
		}
		NativeSocket = (int32)socket(Family, SOCK_STREAM, IPPROTO_TCP);
		if (NativeSocket < 0)
		{
			Close();
			return false;
		}

		TArray<uint8> RawIp = Addr.GetRawIp();
		const int32 PortNo = Addr.GetPort();

		if (Family == AF_INET && RawIp.Num() == 4)
		{
			sockaddr_in SA;
			FMemory::Memzero(&SA, sizeof(SA));
			SA.sin_family = AF_INET;
			SA.sin_port   = htons((uint16)PortNo);
			FMemory::Memcpy(&SA.sin_addr, RawIp.GetData(), 4);
			if (connect((SOCKET)NativeSocket, (sockaddr*)&SA, sizeof(SA)) != 0)
			{
				Close();
				return false;
			}
		}
		else if (Family == AF_INET6 && RawIp.Num() == 16)
		{
			sockaddr_in6 SA6;
			FMemory::Memzero(&SA6, sizeof(SA6));
			SA6.sin6_family = AF_INET6;
			SA6.sin6_port   = htons((uint16)PortNo);
			FMemory::Memcpy(&SA6.sin6_addr, RawIp.GetData(), 16);
			if (connect((SOCKET)NativeSocket, (sockaddr*)&SA6, sizeof(SA6)) != 0)
			{
				Close();
				return false;
			}
		}
		else
		{
			Close();
			return false;
		}

		// 소켓 수준 keepalive. 평문 경로에서는 FSocket 에 설정자가 없어 생략했지만,
		// 여기서는 디스크립터를 직접 소유하므로 그대로 켤 수 있다.
		{
			int OptVal = 1;
			setsockopt((SOCKET)NativeSocket, SOL_SOCKET, SO_KEEPALIVE,
				(const char*)&OptVal, sizeof(OptVal));
		}

		SetSocketNonBlocking(NativeSocket);
	}

	// 2) SSL 객체 생성 및 소켓 결합
	Ssl = SSL_new(Ctx);
	if (!Ssl)
	{
		Close();
		return false;
	}
	SSL_set_fd(Ssl, NativeSocket);

	// SNI. 서버가 여러 인증서를 서비스할 때 올바른 것을 고르게 한다.
	SSL_set_tlsext_host_name(Ssl, TCHAR_TO_ANSI(*ServerName));

	// 호스트명 검증 대상
	SSL_set1_host(Ssl, TCHAR_TO_ANSI(*ServerName));

	SSL_set_connect_state(Ssl);

	// 3) 논블로킹 핸드셰이크
	if (!DoHandshake(Config.Tls.HandshakeTimeoutSeconds))
	{
		Close();
		return false;
	}

	// 4) 핀 검증 (핀 모드일 때만)
	if (HermesTls::ResolveVerifyMode(Config.Tls.PinnedPublicKeyHashes, Config.Tls.PrivateCaPath)
		== HermesTls::EVerifyMode::PinnedKey)
	{
		if (!VerifyPinnedKey(ServerName))
		{
			UE_LOG(LogHermes, Error,
				TEXT("TLS public key pin mismatch for '%s'. Refusing connection."),
				*ServerName);
			Close();
			return false;
		}
	}
	else
	{
		// CA 모드: 체인+호스트명 검증 결과를 확인한다.
		const long VerifyResult = SSL_get_verify_result(Ssl);
		if (VerifyResult != X509_V_OK)
		{
			UE_LOG(LogHermes, Error,
				TEXT("TLS certificate verification failed for '%s' (code %ld). "
				     "If this is a self-signed LAN server, set TlsPinnedPublicKeyHashes."),
				*ServerName, VerifyResult);
			Close();
			return false;
		}
	}

	return true;
#endif
}

#if WITH_SSL
bool FHermesTlsTransport::CreateContext(const FHermesTlsConfig& Tls, const FString& ServerName)
{
	ISslManager& Mgr = FSslModule::Get().GetSslManager();

	// InitializeSsl() 이 거짓을 돌려주는 것은 실패가 아니다.
	//
	// 엔진 구현(FSslManager::InitializeSsl)은 본체 전체가
	// `#if IS_MONOLITHIC || UE_MERGED_MODULES` 안에 있고, 그 밖에서는 false 로
	// 초기화된 값을 그대로 돌려준다. 모듈러 빌드에서는 OpenSSL 이 여러
	// 라이브러리에 정적 링크되어 SSL 모듈이 전역 초기화를 맡지 않기 때문이며,
	// "초기화하지 않았다"는 뜻이지 "쓸 수 없다"는 뜻이 아니다.
	//
	// 이것을 치명적 오류로 다루면 **에디터와 PIE 에서 TLS 가 영영 붙지 못한다**.
	// 모놀리식인 Shipping 에서만 동작하므로 개발 중에는 드러나지도 않는다.
	// 실제로 그 상태였고, 실서버 TLS 연동에서 처음 드러났다.
	//
	// 돌려받은 값은 ShutdownSsl() 짝을 맞추는 데만 쓴다. 초기화하지 않았다면
	// 내려놓을 것도 없다.
	bSslInitialized = Mgr.InitializeSsl();

	const HermesTls::EVerifyMode Mode =
		HermesTls::ResolveVerifyMode(Tls.PinnedPublicKeyHashes, Tls.PrivateCaPath);

	// SSL_CTX 를 엔진의 ISslManager 로 만들지 않고 OpenSSL 로 직접 만든다.
	//
	// FSslManager::CreateSslContext() 는 본체 전체가
	// `#if IS_MONOLITHIC || UE_MERGED_MODULES` 안에 있어 모듈러 빌드에서는
	// 무조건 nullptr 을 돌려준다. DestroySslContext 도 마찬가지다. 즉 그 API 는
	// 모놀리식 타깃 전용이고, **에디터와 PIE 에서는 TLS 를 만들 수 없다.**
	// 그것을 쓰는 한 개발 중에는 TLS 를 한 번도 시험할 수 없다.
	//
	// OpenSSL 은 이 모듈에 직접 링크되어 있으므로(Build.cs 의
	// AddEngineThirdPartyPrivateStaticDependencies) 직접 만들면 빌드 형태와
	// 무관하게 동작한다. 인증서 관리자(ISslCertificateManager)는 게이팅되어
	// 있지 않으므로 핀 검증 경로는 그대로 쓴다.
	Ctx = SSL_CTX_new(TLS_client_method());
	if (!Ctx)
	{
		UE_LOG(LogHermes, Error, TEXT("SSL_CTX_new failed"));
		return false;
	}

	// TLS 1.2 미만 비활성화. 엔진 구현의 SSL_OP_NO_* 비트 나열과 같은 효과이며
	// 이쪽이 의도가 분명하다.
	if (SSL_CTX_set_min_proto_version(Ctx, TLS1_2_VERSION) != 1)
	{
		UE_LOG(LogHermes, Error, TEXT("failed to require TLS 1.2 or newer"));
		return false;
	}
	SSL_CTX_set_options(Ctx, SSL_OP_NO_COMPRESSION);   // CRIME 류 회피

	ISslCertificateManager& Certs = FSslModule::Get().GetCertificateManager();

	// 핀 모드가 아니면 신뢰 저장소를 붙인다. 엔진의 bAddCertificates 와 같다.
	if (Mode != HermesTls::EVerifyMode::PinnedKey)
	{
		Certs.AddCertificatesToSslContext(Ctx);
	}

	switch (Mode)
	{
	case HermesTls::EVerifyMode::PinnedKey:
	{
		const FString Joined = FString::Join(Tls.PinnedPublicKeyHashes, TEXT(";"));
		Certs.SetPinnedPublicKeys(ServerName, Joined);
		SSL_CTX_set_verify(Ctx, SSL_VERIFY_NONE, nullptr);
		break;
	}

	case HermesTls::EVerifyMode::PrivateCa:
	{
		SSL_CTX_set_verify(Ctx, SSL_VERIFY_PEER, nullptr);
		const FString AbsPath = Tls.PrivateCaPath;
		if (SSL_CTX_load_verify_locations(Ctx, TCHAR_TO_ANSI(*AbsPath), nullptr) != 1)
		{
			UE_LOG(LogHermes, Error, TEXT("failed to load private CA: %s"), *AbsPath);
			return false;
		}
		break;
	}

	case HermesTls::EVerifyMode::SystemCa:
	default:
		SSL_CTX_set_verify(Ctx, SSL_VERIFY_PEER, nullptr);
		break;
	}

	return true;
}

bool FHermesTlsTransport::DoHandshake(float TimeoutSeconds)
{
	const double Deadline = FPlatformTime::Seconds() + (double)TimeoutSeconds;

	while (true)
	{
		const int32 Ret = SSL_do_handshake(Ssl);
		if (Ret == 1)
		{
			return true;
		}

		const int32 Err = SSL_get_error(Ssl, Ret);
		if (Err != SSL_ERROR_WANT_READ && Err != SSL_ERROR_WANT_WRITE)
		{
			UE_LOG(LogHermes, Error, TEXT("TLS handshake failed (ssl error %d)"), Err);
			return false;
		}

		if (FPlatformTime::Seconds() >= Deadline)
		{
			UE_LOG(LogHermes, Error, TEXT("TLS handshake timed out after %.1fs"), TimeoutSeconds);
			return false;
		}

		FPlatformProcess::Sleep(0.01f);
	}
}

bool FHermesTlsTransport::VerifyPinnedKey(const FString& ServerName) const
{
	ISslCertificateManager& Certs = FSslModule::Get().GetCertificateManager();

	if (!Certs.IsDomainPinned(ServerName))
	{
		UE_LOG(LogHermes, Error,
			TEXT("pins were configured but not registered for '%s'"), *ServerName);
		return false;
	}

	// 다이제스트를 직접 만들어 넘긴다.
	//
	// 엔진의 X509_STORE_CTX 오버로드는 X509_STORE_CTX_get_chain() — 즉
	// X509_verify_cert() 가 채워 놓는 **검증된 체인** — 을 읽는다. 핀 모드는
	// 체인 검증을 돌리지 않으므로(자체 서명을 허용하는 것이 핀의 목적이다)
	// 그 체인은 언제나 비어 있고, 결과는 항상 "핀 불일치"가 된다.
	// X509_STORE_CTX_init 이 채우는 것은 untrusted 체인이라 소용이 없다.
	//
	// 다이제스트 기준은 엔진과 동일하다 — SubjectPublicKeyInfo(DER) 의 SHA-256.
	TArray<TArray<uint8, TFixedAllocator<ISslCertificateManager::PUBLIC_KEY_DIGEST_SIZE>>> Digests;

	auto AddDigest = [&Digests](X509* Cert)
	{
		if (!Cert)
		{
			return;
		}
		const int Length = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(Cert), nullptr);
		if (Length <= 0)
		{
			return;   // 공개키가 없는 인증서
		}
		TArray<uint8> PubKey;
		PubKey.AddUninitialized(Length);
		uint8* Ptr = PubKey.GetData();
		i2d_X509_PUBKEY(X509_get_X509_PUBKEY(Cert), &Ptr);

		TArray<uint8, TFixedAllocator<ISslCertificateManager::PUBLIC_KEY_DIGEST_SIZE>> Digest;
		Digest.AddUninitialized(ISslCertificateManager::PUBLIC_KEY_DIGEST_SIZE);
		SHA256(PubKey.GetData(), PubKey.Num(), Digest.GetData());
		Digests.Add(MoveTemp(Digest));
	};

	X509* Leaf = SSL_get_peer_certificate(Ssl);
	AddDigest(Leaf);
	if (Leaf)
	{
		X509_free(Leaf);
	}

	// 클라이언트 쪽에서는 이 스택에 리프도 포함되지만, 중간 인증서로 핀을 잡는
	// 구성도 있으므로 전부 넣는다. 중복은 무해하다 — 하나라도 맞으면 통과다.
	if (STACK_OF(X509)* Chain = SSL_get_peer_cert_chain(Ssl))
	{
		for (int32 Index = 0; Index < sk_X509_num(Chain); ++Index)
		{
			AddDigest(sk_X509_value(Chain, Index));
		}
	}

	const bool bOk = Digests.Num() > 0 && Certs.VerifySslCertificates(Digests, ServerName);

	if (!bOk)
	{
		UE_LOG(LogHermes, Error,
			TEXT("server public key does not match any configured pin for '%s'"),
			*ServerName);
	}
	return bOk;
}
#endif // WITH_SSL

void FHermesTlsTransport::Close()
{
#if WITH_SSL
	if (Ssl)
	{
		SSL_shutdown(Ssl);
		SSL_free(Ssl);
		Ssl = nullptr;
	}
	if (Ctx)
	{
		// SSL_CTX_new 로 직접 만들었으므로 직접 해제한다. 엔진의
		// DestroySslContext 는 모듈러 빌드에서 아무 일도 하지 않아 누수가 된다.
		SSL_CTX_free(Ctx);
		Ctx = nullptr;
	}
	if (bSslInitialized)
	{
		FSslModule::Get().GetSslManager().ShutdownSsl();
		bSslInitialized = false;
	}
#endif
	if (NativeSocket >= 0)
	{
		CloseNativeSocket(NativeSocket);
		NativeSocket = -1;
	}
}

bool FHermesTlsTransport::HasPendingData(uint32& OutBytes)
{
#if WITH_SSL
	if (!Ssl)
	{
		return false;
	}
	const int32 Pending = SSL_pending(Ssl);
	if (Pending > 0)
	{
		OutBytes = (uint32)Pending;
		return true;
	}

	uint8 Peek = 0;
	const int32 R = (int32)recv((SOCKET)NativeSocket, (char*)&Peek, 1, MSG_PEEK);
	if (R > 0)
	{
		OutBytes = 1;
		return true;
	}
	return false;
#else
	OutBytes = 0;
	return false;
#endif
}

int32 FHermesTlsTransport::Recv(uint8* Buf, int32 BufSize)
{
#if WITH_SSL
	if (!Ssl)
	{
		return -1;
	}
	const int32 Read = SSL_read(Ssl, Buf, BufSize);
	if (Read > 0)
	{
		return Read;
	}

	const int32 Err = SSL_get_error(Ssl, Read);
	if (Err == SSL_ERROR_WANT_READ || Err == SSL_ERROR_WANT_WRITE)
	{
		return 0;
	}
	return -1;
#else
	return -1;
#endif
}

int32 FHermesTlsTransport::Send(const uint8* Buf, int32 Num)
{
#if WITH_SSL
	if (!Ssl)
	{
		return -1;
	}
	const int32 Sent = SSL_write(Ssl, Buf, Num);
	if (Sent > 0)
	{
		return Sent;
	}

	const int32 Err = SSL_get_error(Ssl, Sent);
	if (Err == SSL_ERROR_WANT_READ || Err == SSL_ERROR_WANT_WRITE)
	{
		return 0;
	}
	return -1;
#else
	return -1;
#endif
}
