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
	if (!Mgr.InitializeSsl())
	{
		UE_LOG(LogHermes, Error, TEXT("failed to initialize SSL"));
		return false;
	}
	bSslInitialized = true;

	const HermesTls::EVerifyMode Mode =
		HermesTls::ResolveVerifyMode(Tls.PinnedPublicKeyHashes, Tls.PrivateCaPath);

	FSslContextCreateOptions Options;
	Options.MinimumProtocol = ESslTlsProtocol::TLSv1_2;   // TLS 1.2 미만 비활성화
	Options.bAllowCompression = false;                    // CRIME 류 회피
	Options.bAddCertificates = (Mode != HermesTls::EVerifyMode::PinnedKey);

	Ctx = Mgr.CreateSslContext(Options);
	if (!Ctx)
	{
		return false;
	}

	ISslCertificateManager& Certs = FSslModule::Get().GetCertificateManager();

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

	X509_STORE_CTX* StoreCtx = X509_STORE_CTX_new();
	if (!StoreCtx)
	{
		return false;
	}

	STACK_OF(X509)* Chain = SSL_get_peer_cert_chain(Ssl);
	X509* Leaf = SSL_get_peer_certificate(Ssl);
	if (!Leaf)
	{
		X509_STORE_CTX_free(StoreCtx);
		return false;
	}

	const bool bInit = X509_STORE_CTX_init(StoreCtx, nullptr, Leaf, Chain) == 1;
	const bool bOk = bInit && Certs.VerifySslCertificates(StoreCtx, ServerName);

	X509_free(Leaf);
	X509_STORE_CTX_free(StoreCtx);

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
		FSslModule::Get().GetSslManager().DestroySslContext(Ctx);
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
