#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HermesSettings.generated.h"

/**
 * Hermes Agent NPC 플러그인의 모든 설정을 담는 단일 출처.
 * 에디터 Project Settings > Plugins > Hermes Agent NPC 에 표시되고
 * 값은 프로젝트의 Config/DefaultGame.ini 에 저장된다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="Hermes Agent NPC"))
class HERMESAGENTNPC_API UHermesSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ---- Connection ----

	/** Hermes 백엔드 호스트명 또는 IP 주소. */
	UPROPERTY(EditAnywhere, config, Category="Connection")
	FString Host = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, config, Category="Connection", meta=(ClampMin="1", ClampMax="65535"))
	int32 Port = 8770;

	// ---- Connection|TLS ----

	/** TLS 사용 여부. Shipping 빌드에서는 false여도 강제로 켜진다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
	bool bUseTLS = true;

	/** SNI 및 인증서 호스트명 검증에 쓸 이름. 비우면 Host를 그대로 쓴다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
	FString TlsServerName;

	/** 서버 공개키(SPKI) SHA-256 해시의 base64 목록. 비어있지 않으면 핀 검증을 한다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
	TArray<FString> TlsPinnedPublicKeyHashes;

	/** 사설 CA 인증서(PEM) 경로. 프로젝트 디렉터리 기준 상대 경로. */
	UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
	FString TlsPrivateCaPath;

	UPROPERTY(EditAnywhere, config, Category="Connection|TLS", meta=(ClampMin="1.0", ClampMax="60.0"))
	float TlsHandshakeTimeoutSeconds = 10.f;

	// ---- Connection|Tuning ----

	/** 재연결 첫 대기 시간(초). 실패할 때마다 2배씩 증가한다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="0.05", ClampMax="10.0"))
	float InitialReconnectDelay = 0.5f;

	/** 재연결 대기 시간 상한(초). */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="300.0"))
	float MaxReconnectDelay = 30.f;

	/**
	 * 이만큼(초) 살아 있었던 연결만 백오프 사다리를 초기값으로 되돌린다.
	 *
	 * TCP 접속 성공만으로 되돌리면, 접속은 되는데 서버가 곧바로 끊는 상황에서
	 * 매 사이클이 성공으로 보여 재연결이 왕복 지연 속도로 돌며 서버를 두드린다.
	 * 값을 키우면 더 보수적으로(느리게) 재연결한다.
	 */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="0.0", ClampMax="60.0"))
	float HealthyConnectionSeconds = 5.f;

	/**
	 * 두 번째 연속 정지부터의 재개 대기(초). 첫 정지는 언제나 즉시 재개된다.
	 *
	 * 같은 신원으로 두 인스턴스가 뜨면 서로를 걷어내는 싸움이 된다. 되받아칠수록
	 * 이 값이 배로 늘어 싸움이 수렴한다. 값을 키우면 더 빨리 수렴하지만 정당한
	 * 재접속도 그만큼 기다린다.
	 */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="0.0", ClampMax="120.0"))
	float ReconnectCooldownSeconds = 5.f;

	/** 재개 대기의 상한(초). */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="3600.0"))
	float MaxReconnectCooldownSeconds = 300.f;

	/** 액션 요청 응답 대기 한계(초). 초과 시 timeout 결과를 회신한다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="120.0"))
	float ActionTimeoutSeconds = 15.f;

	/** 게임 스레드가 소비하기 전까지 쌓아둘 수 있는 최대 수신 프레임 수. 초과 시 연결을 끊는다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="16", ClampMax="65536"))
	int32 MaxInboundQueueSize = 1024;

	/** 한 틱에 처리할 최대 수신 프레임 수. 나머지는 다음 틱으로 미룬다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="4096"))
	int32 MaxInboundFramesPerTick = 64;

	/** 송신 대기 프레임 상한. 초과 시 새 프레임을 버린다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="16", ClampMax="8192"))
	int32 MaxOutboundQueueSize = 256;

	/** identified 이전에 보류할 수 있는 최대 발화 수. 초과 시 가장 오래된 것부터 버린다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="512"))
	int32 MaxPendingChats = 32;

	/** 초당 처리할 최대 액션 요청 수. 초과분은 즉시 거부 회신한다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="1000"))
	int32 MaxActionsPerSecond = 20;

	// ---- Connection|Liveness ----

	/** 유휴 상태에서 서버로 ping을 보내는 간격(초). */
	UPROPERTY(EditAnywhere, config, Category="Connection|Liveness", meta=(ClampMin="5.0", ClampMax="300.0"))
	float KeepAlivePingIntervalSeconds = 20.f;

	/** 이 시간 동안 어떤 프레임도 받지 못하면 연결을 죽은 것으로 간주한다(초). */
	UPROPERTY(EditAnywhere, config, Category="Connection|Liveness", meta=(ClampMin="10.0", ClampMax="600.0"))
	float PeerTimeoutSeconds = 60.f;

	/** chat 발화 후 첫 응답(델타 포함)까지 기다리는 한계(초). 델타가 오면 갱신된다. */
	UPROPERTY(EditAnywhere, config, Category="Connection|Liveness", meta=(ClampMin="5.0", ClampMax="300.0"))
	float ChatResponseTimeoutSeconds = 60.f;

	// ---- Gameplay ----

	/** 플레이어 자격 증명을 보관하는 SaveGame 슬롯 이름. */
	UPROPERTY(EditAnywhere, config, Category="Gameplay")
	FString SaveSlotName = TEXT("HermesPlayer");

	/** follow_player 액션에서 NPC가 유지하는 거리(cm). */
	UPROPERTY(EditAnywhere, config, Category="Gameplay", meta=(ClampMin="50.0"))
	float FollowDistance = 150.f;

	// ---- Gameplay|Limits ----

	/** move_to 좌표 각 축의 허용 절댓값(cm). 기본 1e7 = 100 km. */
	UPROPERTY(EditAnywhere, config, Category="Gameplay|Limits", meta=(ClampMin="1000.0"))
	float MaxWorldCoordinate = 1.0e7f;

	/** item_transfer 1회 요청의 최대 수량. */
	UPROPERTY(EditAnywhere, config, Category="Gameplay|Limits", meta=(ClampMin="1", ClampMax="2000000000"))
	int32 MaxItemQuantity = 999999;

	/** item_id 문자열의 최대 길이. */
	UPROPERTY(EditAnywhere, config, Category="Gameplay|Limits", meta=(ClampMin="1", ClampMax="1024"))
	int32 MaxItemIdLength = 64;

	/** ini 값에 커맨드라인 오버라이드를 적용한 최종 엔드포인트를 돌려준다. */
	void GetResolvedEndpoint(FString& OutHost, int32& OutPort) const;

	/**
	 * 오버라이드 적용의 순수 로직. GetResolvedEndpoint()는 이 함수에
	 * FCommandLine::Get()을 넘길 뿐이다. 테스트가 임의 문자열로 직접 호출한다.
	 * 조건에 맞을 때만 InOut 인자를 덮어쓴다.
	 */
	/**
	 * TLS 사용 여부의 최종 값. `-HermesUseTLS=0/1` 로 덮을 수 있다.
	 *
	 * 스텁 서버는 평문이고 실서버는 TLS 라, 이것이 없으면 검증할 때마다 ini 를
	 * 고쳐야 하고 그 순간 반대쪽 검증이 전부 막힌다.
	 */
	bool GetResolvedUseTLS() const;

	/** 위 오버라이드의 순수 로직. Shipping 에서는 아무것도 하지 않는다. */
	static void ApplyTlsOverride(const TCHAR* CmdLine, bool& InOutUseTLS);

	static void ApplyCommandLineOverrides(const TCHAR* CmdLine,
	                                      FString& InOutHost, int32& InOutPort);
};
