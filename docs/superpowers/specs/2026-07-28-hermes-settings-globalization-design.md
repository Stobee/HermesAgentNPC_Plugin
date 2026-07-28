# Hermes Agent NPC — 설정 전역화 · 입력 강건성 · 프로토콜 v2 (신원 · TLS · 세션 운영)

- 작성일: 2026-07-28
- 상태: 승인 대기
- 대상 브랜치: master
- 범위: **UE5 클라이언트 플러그인 + 프로토콜 문서.** 서버 구현은 별도(재구축 예정)이며, 본 문서의 5·6절이 서버가 지켜야 할 계약이다.

## 0. 이 문서가 해결하는 것과 하지 않는 것

작업이 네 겹으로 커졌으므로 먼저 성격을 구분한다.

| 겹 | 성격 | 절 |
|---|---|---|
| 설정 전역화 | 배포성 | 4.1~4.7 |
| 입력 강건성 | **가용성** — 클라이언트가 이상 입력에 무너지지 않게 함 | 4.8~4.15 |
| 신원 발급 | **보안** — 제3자의 세션 탈취 차단 | 5.1~5.5 |
| 세션 운영 | **기능·신뢰성** — 스트리밍 응답, 죽은 연결 탐지, 응답 타임아웃 | 5.6~5.8 |
| TLS 전송 | **보안** — 경로상 공격자 차단 | 6 |

**입력 강건성은 보안이 아니다.** 메모리 고갈·프레임 행·미정의 동작을 막는 것은 클라이언트를 견고하게 만들 뿐 접근 통제와 무관하다. 실제 보안 효과는 5·6절에서만 나온다.

**5절과 6절은 서로를 필요로 한다.** 신원 발급만 있고 TLS가 없으면 세션 토큰이 평문으로 흘러 경로상 공격자가 그대로 취득한다. TLS만 있고 신원 발급이 없으면 통신은 보호되지만 `player_id`를 아는 누구나 여전히 사칭할 수 있다. 두 절을 함께 넣는 이유이며, 프로토콜을 두 번 올리지 않는 이유이기도 하다.

**그래도 막지 못하는 것이 있다.** 세션 토큰은 로컬 SaveGame에 평문 저장되므로 **기기 소유자 본인**은 언제든 추출할 수 있다. 이는 의도된 한계다 — 클라이언트에 심은 비밀은 그 기기에서 지킬 수 없으며, 숨기려는 시도는 obscurity에 불과하다. 10.1에 전체 위협 모델을 정리한다.

## 1. 문제

### 1.1 서버 주소 하드코딩

`Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:13-15`

```cpp
static const TCHAR* HermesHost = TEXT("192.168.0.111");
static const int32  HermesPort = 8770;
static const TCHAR* SaveSlot   = TEXT("HermesPlayer");
```

`Initialize()`가 이 상수로 `FHermesSocketWorker`를 즉시 생성하므로, 플러그인을 도입한 프로젝트가 서버를 바꾸려면 플러그인 C++ 소스를 수정하고 재컴파일해야 한다. "복사해 넣으면 바로 쓰는 완제품 플러그인"이라는 목표와 충돌한다.

같은 성격의 하드코딩이 3곳 더 있다.

| 위치 | 값 | 의미 |
|---|---|---|
| `HermesSocketWorker.cpp:147-148` | `0.5f` / `30.f` | 재연결 백오프 초기값 / 상한 |
| `HermesActionDispatcher.cpp:64` | `15.f` | 액션 응답 타임아웃(초) |
| `HermesNPCCharacter.cpp:66` | `150.f` | 플레이어 추적 유지 거리(cm) |

### 1.2 신뢰 경계 확장에 따르는 강건성 부족

주소를 설정으로 여는 순간 **클라이언트가 붙는 대상이 고정 사내 IP에서 임의 주소로 넓어진다.** 지금까지는 서버를 암묵적으로 신뢰해도 무방했으나, 앞으로는 오작동하거나 악의적인 피어를 상정해야 한다. 코드 확인 결과 다음 결함이 있다.

**(a) 인바운드 큐 무제한 — 메모리 고갈 및 프레임 행**

`HermesSocketWorker.h:44-45`의 `TQueue` 두 개에 크기 제한이 없다.

- `ReceiveAvailable()`이 `while (Socket->HasPendingData(Pending))` 루프로 계속 수신해 `Inbound`에 무한 적재한다.
- 게임 스레드 `HermesConnectionSubsystem.cpp:109`가 `while (Worker->DequeueInbound(Json))` 로 **한 틱에 큐 전체를 소비**하며 각 프레임마다 JSON 파싱과 액션 디스패치를 수행한다.

**(b) 중단 불가능한 백오프 sleep — 종료 시 게임 스레드 행**

`HermesSocketWorker.cpp:161-162`가 최대 30초를 통짜로 자고, `~FHermesSocketWorker()`(`:16-25`)는 `Thread->Kill(true)`로 그 반환을 대기한다. 서버가 꺼진 상태에서 PIE를 정지하면 게임 스레드가 그만큼 멈춘다.

**이 문제는 본 설계가 직접 악화시킨다.** `MaxReconnectDelay` 상한을 300초로 열면 최악 5분 행이 된다.

**(c) 액션 파라미터 상한 검증 부재**

`HermesActionDispatcher.cpp:18-33`은 `CanHandle()`로 **명령 이름만** 화이트리스트 검사하고 파라미터 검증은 각 핸들러 자율에 맡긴다. 하한(음수·0)은 이미 막혀 있으나 상한과 유한성이 비어 있다.

| 위치 | 문제 |
|---|---|
| `MoveToActionHandler.cpp:17-25` | 좌표 무검증. `1e308` 입력 시 `(float)` 캐스트로 `inf`가 되어 `FVector(inf,inf,inf)`가 `MoveToLocation()`에 전달된다 |
| `ItemTransferActionHandler.cpp:25` | `quantity` 상한 없음. `2e9`는 `Qd < 1`을 통과하고 int32에도 들어가, `HermesInventoryComponent.cpp:31`의 `It->Quantity += Qty`에서 **int32 오버플로 → 음수 수량**이 된다 |
| `ItemTransferActionHandler.cpp:30` | `(int32)Qd` — `Qd > INT32_MAX`면 C++ 미정의 동작 |
| `ItemTransferActionHandler.cpp:24` | `item_id` 무검증 |

**음수 수량을 이용한 아이템 복제 경로는 이미 이중으로 차단되어 있다** (`ItemTransferActionHandler.cpp:25`, `HermesInventoryComponent.cpp:25,43`). 이번에 메우는 것은 반대쪽인 상한과 유한성이다. `follow_player`(bool만 사용)와 `inventory_manage`(고정 문자열 비교 후 조회 키로만 사용)는 검토 결과 문제가 없다.

**(d) 커맨드라인 오버라이드가 만드는 리다이렉트 표면**

본 설계가 추가하는 `-HermesHost=` 인자가 배포 빌드에서도 동작하면 최종 사용자가 클라이언트를 임의 서버로 붙일 수 있다.

**(e) `PendingChats` 무제한 증가**

`HermesConnectionSubsystem.cpp:76-88`:

```cpp
if (bIdentified) SendJson(Json);
else PendingChats.Add(Json);   // 상한 없음
```

`bIdentified`는 서버가 `identified` 프레임을 보내야만 켜진다. **TCP는 받아주면서 `identified`를 보내지 않는 피어**에 붙으면 플레이어 발화가 무한 적재된다. 연결이 끊길 때마다 `bIdentified=false`로 돌아가므로 재연결 실패가 길어져도 같은 일이 생긴다. (a)와 같은 성격의 구멍이다.

**(f) 액션 레이트 리밋 부재 및 타이머 미회수**

틱 예산(4.10)을 64로 두어도 60fps에서 **초당 3,840개** 액션이 처리 가능하고, 각각이 15초 타이머를 만든다 → 최대 약 **57,600개의 동시 타이머**. 틱 예산은 한 틱만 지켜줄 뿐 지속 유입을 막지 못한다.

또한 `HermesActionDispatcher.cpp:55`의 `FTimerHandle Th;`는 **지역 변수로 버려져** 취소가 불가능하다. 핸들러가 즉시 완료돼도 타이머는 15초를 채우고 만료된다.

**(g) 아웃바운드 큐 무제한**

`Outbound`(`HermesSocketWorker.h:44`)에도 상한이 없다. 연결이 끊긴 동안 액션 결과와 pong이 쌓이는데 배출구가 없다.

### 1.3 신원을 클라이언트가 주장한다

`HermesConnectionSubsystem.cpp:52`가 `FGuid::NewGuid()`로 `player_id`를 **직접 생성**해 identify로 보내고, 서버는 그것을 그대로 `chat_id`로 쓴다 (`ue5-socket-protocol.md` §4.2). 토큰도 검증도 없다 (`HermesMessages.cpp:22-32`).

따라서 세이브 파일을 훔칠 필요조차 없다 — **임의의 UUID를 보내면 그 사람으로 인정된다.** 남의 `player_id`를 알아낸 사람은 그 플레이어의 대화 기억과 맥락에 접근할 수 있다.

프로토콜 §5의 `not_authorized`는 허용 목록 방식이며 "if enabled" 선택 사항이라 기본 상태에서는 아무 방어가 없다.

**"토큰을 추가한다"로는 부족하다.** 클라이언트가 자기 신원을 주장하는 구조 자체를 뒤집어, **신원을 서버가 발급**해야 한다.

### 1.4 전송이 평문이다

`HermesSocketWorker.cpp:56`의 `FTcpSocketBuilder(TEXT("HermesClient")).AsBlocking().Build()`는 순수 TCP 소켓이다. 암호화도 서버 신원 확인도 없다.

**도청보다 주입이 더 큰 위험이다.** 경로상 공격자는 대화 내용을 엿듣는 데 그치지 않고 `action_request` 프레임을 **직접 만들어 넣을 수 있다.** 화이트리스트와 파라미터 바운드(4.11)가 피해 범위를 제한하지만, NPC는 여전히 공격자가 고른 액션을 실행한다. 또한 클라이언트가 접속 대상이 진짜 Hermes 서버인지 확인하지 않으므로 가짜 서버로 유인될 수 있다.

### 1.5 TCP를 직접 쓰면서 상위 프로토콜이 주던 것을 만들지 않았다

양방향 서버 푸시(`action_request`)가 필요한 구조에서 커스텀 TCP는 타당한 선택이고, 프레이밍도 올바르게 구현되어 있다. 다만 WebSocket 같은 상위 프로토콜이 기본 제공하는 세션 관리가 비어 있다.

**(a) 반열림(half-open) 연결을 탐지하지 못한다**

소켓에 `SetKeepAlive` 설정이 없고, **클라이언트가 `ping`을 보내지 않는다.** `HermesConnectionSubsystem.cpp:146`은 서버가 보낸 ping에 pong으로 답할 뿐이다.

Wi-Fi 전환, NAT 타임아웃, 케이블 분리처럼 조용히 끊기는 경우 TCP는 알려주지 않는다. `bConnected`는 계속 `true`로 남고 실패는 **다음 송신 시점까지 미뤄진다.** 그런데 송신은 플레이어가 말을 걸 때 일어나므로, 재연결이 *"NPC에게 말을 거는 바로 그 순간"* 에 시작된다 — 가장 나쁜 타이밍이다.

**(b) chat 요청에 타임아웃이 없다**

`HermesDialogueWidget.cpp:41`이 `"생각 중..."`을 띄우지만 응답이 오지 않으면 그대로 남는다. 액션에는 15초 타임아웃이 있으나(`HermesActionDispatcher.cpp:64`) 대화에는 없다. 로컬 GPU 추론은 정상적으로도 수십 초가 걸릴 수 있어, **"느린 것"과 "죽은 것"을 구분할 수단**이 필요하다.

**(c) 응답을 상관(correlate)하지 않는다**

`UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)`가 **`Id`를 사용하지 않는다**(`HermesDialogueWidget.cpp:45-51`). 늦게 도착한 이전 발화의 응답이 현재 화면을 덮어쓴다.

**(d) 스트리밍이 없다**

프로토콜에 부분 응답 개념이 없어 `chat_response`가 통째로 도착한다. LLM NPC에서 수 초의 침묵 후 한 번에 출력되는 것과 즉시 말을 시작하는 것은 체감이 크게 다르다. llama-server는 SSE 스트리밍을 지원하므로 **병목은 추론이 아니라 이 구간의 프로토콜이다.**

프로토콜을 v2로 개정하는 지금이 이 넷을 넣을 수 있는 가장 저렴한 시점이다. 나중에 하면 v3가 필요하다.

## 2. 목표와 비목표

### 목표

- 플러그인 소스에서 `192.168.0.111`을 완전히 제거한다.
- 도입 개발자가 **에디터 Project Settings**에서, 배포 담당자가 **재컴파일 없이 ini·커맨드라인**으로 서버를 지정할 수 있다.
- Host에 **호스트명(도메인)** 을 쓸 수 있다.
- 1.1의 상수 4종을 같은 설정 체계로 흡수한다.
- 1.2의 (a)~(g)를 해소해, 신뢰할 수 없는 피어에 붙어도 클라이언트가 메모리 고갈·프레임 행·미정의 동작에 빠지지 않는다.
- 1.3을 해소해, **`player_id`를 아는 것만으로는 남의 세션에 접근할 수 없게** 한다.
- 1.4를 해소해, **경로상 공격자가 대화를 엿보거나 액션을 주입하거나 가짜 서버로 유인할 수 없게** 한다.
- 공인 인증서를 받을 수 없는 **LAN 자체 서명 환경에서도** 위 보장이 성립하게 한다.
- 1.5를 해소해, **NPC가 응답을 즉시 말하기 시작하고**, 죽은 연결이 사용 시점이 아니라 발생 시점에 탐지되며, 응답 없는 대화가 무한히 대기하지 않게 한다.

### 비목표 (명시적 제외)

- **기기 소유자로부터의 보호.** 세션 토큰은 로컬 SaveGame에 평문 저장되며, 이를 숨기려는 시도(난독화·암호화)는 하지 않는다. 클라이언트에 심은 비밀은 그 기기 소유자에게 결국 노출되므로 obscurity에 불과하다.
- **클라이언트 인증서(mTLS).** 서버가 클라이언트를 인증서로 식별하는 방식은 도입하지 않는다. 인증서를 배포된 게임에 심는 것은 위와 같은 이유로 실효가 없고, 5절의 세션 토큰이 같은 역할을 더 단순하게 한다.
- **서버측 레이트 리밋·프롬프트 인젝션 방어.** 이 저장소 범위 밖이다. 10.3 참조.
- **런타임 서버 전환 API.** 설정은 `Initialize()`에서 1회 읽고 확정된다.
- **최종 사용자 설정 노출.**
- **설정 저장 즉시 재연결.** 에디터 값 변경은 **다음 PIE/게임 시작부터** 적용된다.
- **`FHermesFrameCodec::MaxBodySize`(1 MiB) 설정화.** 4.3 참조.
- **환경별 DataAsset 프로필.**
- **아이템 카탈로그 검증.** `item_id`의 형식 검증(빈 문자열·길이)까지만 하고 게임 데이터 대조는 확장 지점으로 남긴다.
- **v1 서버 호환 모드.** 5.4 참조.
- **TLS 실패 시 평문 폴백.** 6.5 참조.

## 3. 접근법 결정

### 3.1 설정 체계

`UDeveloperSettings` 파생 클래스 하나(`UHermesSettings`)를 두고, 그 위에 커맨드라인 오버라이드 레이어를 얹는다.

검토했으나 채택하지 않은 대안:

- **`UObject(config=Game)` + `GConfig` 수동 읽기** — 의존성이 없어 가볍지만 에디터 설정 UI가 생기지 않는다. 도입 개발자가 ini 키 이름을 문서에서 찾아 손으로 써야 하므로 목표에 어긋난다.
- **`UDataAsset` 프로필 + 세팅에서 활성 프로필 선택** — 설정 계층이 2단이 되어 값의 출처 추적이 어려워진다. 환경 전환은 커맨드라인으로 이미 해결된다.

### 3.2 TLS 구현 방식

**UE의 `SSL` 모듈(OpenSSL) 위에 직접 구현한다.** 기존 프레이밍 코덱·워커 구조를 유지하고 전송 바이트만 TLS 안으로 넣는다.

> **표기 정정:** 초기 검토에서 `FSslSocket`을 언급했으나 UE의 공개 API에 그런 클래스는 없다. 실제로는 `SSL` 모듈이 제공하는 `SSL_CTX` 생성과 루트 인증서 주입을 사용하고, 소켓 계층은 OpenSSL BIO로 다룬다.

검토했으나 채택하지 않은 대안:

- **`IWebSocket`(wss)로 전환** — TLS와 메시지 프레이밍을 엔진에서 받을 수 있어 매력적이다. 그러나 (1) `FHermesFrameCodec`·`FFrameAccumulator`·`FHermesSocketWorker`와 그에 딸린 자동화 테스트가 전부 폐기되고, (2) UE WebSockets 모듈은 플랫폼별 백엔드가 달라 **사설 CA 추가와 공개키 핀 고정의 제어가 빈약하다.** 서버가 LAN IP(`192.168.0.111`)라 공인 인증서를 받을 수 없고 자체 서명 + 핀 고정이 유일한 현실적 경로이므로, 이 제약이 결정적이다.
- **검증을 끄고 암호화만 사용** — 인증서 검증 없는 TLS는 MITM을 전혀 막지 못한다. 도청만 막고 주입은 그대로 허용하므로 1.4의 주된 위험을 놓친다.

## 4. 설계 — 설정과 강건성

### 4.1 값의 우선순위

1. **플러그인 기본값** — `UHermesSettings` 프로퍼티 초기화자. 중립값 `127.0.0.1:8770`.
2. **프로젝트 ini** — `Config/DefaultGame.ini`. 에디터 UI가 이 파일에 쓴다.
3. **커맨드라인** — `-HermesHost=` / `-HermesPort=`. Host/Port에만 적용되며 Shipping 빌드에서는 비활성화된다 (4.9).

커맨드라인 계층을 Host/Port로 한정하는 이유는 그 목적이 "같은 빌드를 다른 서버에 붙이는 것"이기 때문이다. 백오프나 추적 거리를 실행할 때마다 바꿔야 할 시나리오는 없다. **TLS 설정에는 커맨드라인 오버라이드를 두지 않는다** — 검증 정책을 실행 인자로 낮출 수 있게 하는 것은 그 자체가 취약점이다.

### 4.2 설정 클래스

신규: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.h` / `.cpp`

```cpp
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

    /** TLS 사용 여부. Shipping 빌드에서는 false여도 강제로 켜진다 (6.5). */
    UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
    bool bUseTLS = true;

    /**
     * SNI 및 인증서 호스트명 검증에 사용할 이름. 비우면 Host를 그대로 쓴다.
     * IP 주소로 접속하면서 인증서에는 도메인명이 들어 있을 때 지정한다.
     */
    UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
    FString TlsServerName;

    /**
     * 서버 공개키(SPKI) SHA-256 해시의 base64 목록. 비어있지 않으면
     * 이 목록과의 일치가 신뢰의 근거가 된다 (6.4). 자체 서명 인증서를
     * 쓰는 LAN 환경의 권장 설정.
     */
    UPROPERTY(EditAnywhere, config, Category="Connection|TLS")
    TArray<FString> TlsPinnedPublicKeyHashes;

    /** 사설 CA 인증서(PEM) 파일 경로. 프로젝트 디렉터리 기준 상대 경로. */
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
     */
    static void ApplyCommandLineOverrides(const TCHAR* CmdLine,
                                          FString& InOutHost, int32& InOutPort);
};
```

`ClampMin`/`ClampMax`는 에디터 UI 단에서 비정상 값 입력을 막는다.

`GetResolvedEndpoint()` 규칙:

- `-HermesHost=` 가 있고 비어있지 않으면 Host를 덮는다. 없거나 빈 문자열이면 ini 값을 유지한다.
- `-HermesPort=` 가 있고 정수로 파싱되며 1~65535이면 Port를 덮는다. 파싱 실패나 범위 밖이면 ini 값을 유지한다.
- Host/Port는 독립 판정한다. 한쪽만 지정해도 다른 쪽은 영향받지 않는다.

**`PeerTimeoutSeconds`는 `KeepAlivePingIntervalSeconds`보다 충분히 커야 한다.** 그렇지 않으면 정상 연결이 죽은 것으로 오판된다. 시작 시 `PeerTimeoutSeconds < KeepAlivePingIntervalSeconds * 2`이면 경고 로그를 남긴다. 값을 강제로 교정하지는 않는다 — 설정자의 의도를 덮어쓰기보다 문제를 드러내는 편이 낫다.

### 4.3 `MaxBodySize`를 제외하는 근거

- 서버와 합의된 **프로토콜 불변식**이다 (`ue5-socket-protocol.md` §1). 클라이언트만 올려도 서버가 1 MiB에서 끊으면 효과가 없고 원인 파악이 어려운 장애가 된다.
- `FHermesFrameCodec` / `FFrameAccumulator`는 엔진 게임플레이 타입에 의존하지 않는 순수 로직으로 설계되어 `HermesFrameCodec.spec.cpp`가 단독 검증한다. `UHermesSettings` 참조를 넣으면 그 격리가 깨진다.

컴파일 타임 안전 상한으로 유지한다.

### 4.4 DNS 해석

`HermesSocketWorker.cpp:47`의 IPv4 전용 파싱을 주소 해석으로 교체한다. **TLS 사용 여부와 무관하게 이 경로 하나로 주소를 정한다** — 해석 결과는 TLS 모드에서 OpenSSL 접속 주소로 넘어가고, 인증서 검증에 쓰는 이름은 별도로 관리한다 (6.3).

```cpp
// 변경 전
FIPv4Address Addr;
if (!FIPv4Address::Parse(Host, Addr)) return false;

// 변경 후
FAddressInfoResult Result = SS->GetAddressInfo(*Host, nullptr,
    EAddressInfoFlags::Default, NAME_None);
if (Result.ReturnCode != SE_NO_ERROR || Result.Results.Num() == 0) return false;

// IPv4 우선, 없으면 첫 결과(IPv6 포함) 사용
const FAddressInfoResultData* Chosen = &Result.Results[0];
for (const FAddressInfoResultData& R : Result.Results)
{
    if (R.Address->GetProtocolType() == FNetworkProtocolTypes::IPv4) { Chosen = &R; break; }
}
TSharedRef<FInternetAddr> InetAddr = Chosen->Address->Clone();
InetAddr->SetPort(Port);
```

**IPv4 우선 정책의 이유:** 듀얼스택 환경에서 OS가 IPv6를 먼저 반환하는 경우가 있는데 서버가 IPv4만 수신하면 접속에 실패한다. IPv4를 먼저 고르면 기존 사내 환경의 동작이 바뀌지 않으면서 IPv6 전용 엔드포인트도 폴백으로 지원된다.

**블로킹 특성:** `GetAddressInfo()`는 동기 호출이며 도달 불가 호스트명에서 OS DNS 타임아웃(수 초)까지 멈춘다. 이 코드는 `FHermesSocketWorker::Run()` 루프, 즉 전용 백그라운드 스레드에서만 실행되므로 게임 스레드는 영향받지 않는다. 실패는 기존 백오프 루프가 흡수한다. 별도 비동기화는 하지 않는다.

### 4.5 소비 지점 변경

| 파일 | 현재 | 변경 후 |
|---|---|---|
| `Connection/HermesConnectionSubsystem.cpp:13-15` | `static const` 3개 | 삭제. `GetDefault<UHermesSettings>()`로 읽어 Worker에 주입 |
| `Transport/HermesSocketWorker.cpp:147-148` | 지역 상수 `0.5f`/`30.f` | 생성자 주입 |
| `Actions/HermesActionDispatcher.cpp:64` | 리터럴 `15.f` | `GetDefault<UHermesSettings>()->ActionTimeoutSeconds` |
| `NPC/HermesNPCCharacter.cpp:66` | 리터럴 `150.f` | `GetDefault<UHermesSettings>()->FollowDistance` |

**`FHermesSocketWorker`가 설정을 직접 읽지 않고 주입받는 이유:** 워커는 전용 스레드에서 실행되는데 `UHermesSettings`는 `UObject`다. 워커 스레드에서 UObject를 만지는 것을 피하기 위해, 값은 `Initialize()`(게임 스레드)에서 읽어 평범한 값 타입으로 복사해 넘긴다. "다음 PIE부터 적용" 정책과도 일치한다. TLS 설정도 같은 이유로 값 복사로 전달한다.

```cpp
struct FHermesTlsConfig
{
    bool            bUseTLS = true;
    FString         ServerName;              // 비면 Host 사용
    TArray<FString> PinnedPublicKeyHashes;
    FString         PrivateCaPath;           // 절대 경로로 변환해 전달
    float           HandshakeTimeoutSeconds = 10.f;
};

struct FHermesWorkerConfig
{
    FString Host;
    int32   Port                  = 8770;
    float   InitialReconnectDelay = 0.5f;
    float   MaxReconnectDelay     = 30.f;
    int32   MaxInboundQueueSize   = 1024;
    int32   MaxOutboundQueueSize  = 256;
    FHermesTlsConfig Tls;
};

explicit FHermesSocketWorker(const FHermesWorkerConfig& InConfig);
```

인자가 여럿으로 늘어나므로 위치 인자 나열 대신 설정 구조체를 받는다. 호출부에서 어떤 값이 무엇인지 읽히고, 항목이 추가돼도 시그니처가 흔들리지 않는다. `PrivateCaPath`는 게임 스레드에서 `FPaths::ProjectDir()` 기준 절대 경로로 변환해 넘긴다 — 워커 스레드가 경로 API에 의존하지 않게 한다.

### 4.6 빌드 설정

`HermesAgentNPC.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { /* 기존 */, "DeveloperSettings" });
PrivateDependencyModuleNames.Add("SSL");
AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
```

`SSL` 모듈은 플랫폼에 따라 없을 수 있으므로 TLS 코드는 전부 `#if WITH_SSL` 로 감싼다. `WITH_SSL`이 0인 플랫폼에서 `bUseTLS=true`이면 **연결을 시도하지 않고 명확한 에러 로그를 남긴다** — 조용히 평문으로 내려가지 않는다 (6.5).

### 4.7 프로젝트 ini

`Config/DefaultGame.ini` 신규 생성:

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=192.168.0.111
Port=8770
bUseTLS=True
TlsServerName=hermes.local
+TlsPinnedPublicKeyHashes=<서버 인증서 배포 시 채움>
```

`Config/`는 현재 git 미추적이나 `.gitignore` 대상은 아니다. `DefaultEngine.ini`, `DefaultInput.ini`와 함께 git에 추가한다. 이로써 사내 주소는 **플러그인이 아니라 이 프로젝트의 설정**으로 이동한다.

핀 해시는 서버 인증서가 확정된 뒤 채운다. 그때까지는 `TlsPrivateCaPath`로 사설 CA를 지정하거나, 개발 환경에서만 `bUseTLS=False`로 두고 작업한다.

### 4.8 백오프 sleep 중단 응답성 — 1.2(b)

```cpp
/** 총 Seconds 만큼 자되, 100ms 마다 깨어나 중단 요청을 확인한다. */
void FHermesSocketWorker::InterruptibleSleep(float Seconds)
{
    constexpr float Slice = 0.1f;
    float Remaining = Seconds;
    while (Remaining > 0.f && !bStopRequested)
    {
        const float Step = FMath::Min(Slice, Remaining);
        FPlatformProcess::Sleep(Step);
        Remaining -= Step;
    }
}
```

`Run()`의 백오프 대기를 이 함수로 교체하고 직후 `if (bStopRequested) break;` 를 둔다. `MaxReconnectDelay`가 300초여도 종료 응답 지연이 **100ms 이내**로 고정된다.

`ConnectSocket()` 안의 `GetAddressInfo()` DNS 블로킹은 이 방식으로 줄일 수 없다(OS 호출이라 중간에 깰 수 없음). 다만 그 상한은 OS 기본 DNS 타임아웃이고 설정으로 늘어나지 않으므로 본 설계가 악화시키지 않는다. 10.4에 잔여 과제로 기록한다. TLS 핸드셰이크는 `TlsHandshakeTimeoutSeconds`로 상한이 있고 논블로킹 루프 안에서 진행되므로 중단 확인이 가능하다 (6.6).

### 4.9 Shipping 빌드 오버라이드 차단 — 1.2(d)

```cpp
void UHermesSettings::ApplyCommandLineOverrides(const TCHAR* CmdLine,
                                                FString& InOutHost, int32& InOutPort)
{
#if UE_BUILD_SHIPPING
    // 배포 빌드에서는 최종 사용자가 클라이언트를 임의 서버로 리다이렉트하지 못하게 한다.
    return;
#else
    // ... 파싱 및 오버라이드 ...
#endif
}
```

Test 구성은 사내 프로파일링·QA 용도이므로 오버라이드를 유지한다.

**이것이 방어의 전부가 아님을 명시한다.** 서버 주소는 클라이언트가 알아야 접속할 수 있으므로 패킷 캡처로 언제든 드러나고, 바이너리를 소유한 사용자는 ini를 직접 고칠 수도 있다. 주소 은닉은 obscurity이지 보안이 아니다. **접근 통제의 본체는 5절의 신원 발급과 6절의 서버 인증이다.** 이 조치의 역할은 "기본 경로를 막는 것"까지다.

### 4.10 인바운드 큐 상한과 틱 예산 — 1.2(a)

**큐 상한 (워커 스레드 측).** `TQueue`는 크기 조회를 제공하지 않으므로 `FThreadSafeCounter InboundCount`를 함께 둔다.

- `ReceiveAvailable()`이 `Enqueue()` 할 때마다 `Increment()`, `DequeueInbound()` 성공 시 `Decrement()`
- `InboundCount`가 `Config.MaxInboundQueueSize`를 넘으면 `ReceiveAvailable()`이 `false`를 반환 → 기존 프레이밍 위반과 **동일 경로**로 연결을 닫고 백오프 재연결

기존 에러 처리 경로를 재사용하므로 새 상태가 늘지 않는다. 정상 서버라면 게임 스레드가 매 틱 비우므로 1024에 도달할 일이 없고, 도달했다는 것은 피어가 소비 속도를 무시하고 밀어넣고 있다는 뜻이다.

**틱 예산 (게임 스레드 측).**

```cpp
// 변경 전: while (Worker->DequeueInbound(Json))
int32 Budget = Settings->MaxInboundFramesPerTick;
FString Json;
while (Budget-- > 0 && Worker->DequeueInbound(Json))
{
    TSharedPtr<FJsonObject> Obj;
    if (HermesJson::Parse(Json, Obj)) HandleFrame(Obj);
}
```

남은 프레임은 다음 틱에서 처리된다. 유입이 예산을 계속 초과하면 큐가 차오르고 결국 큐 상한이 연결을 끊는다. 두 장치가 함께 동작해 **어느 경우에도 한 틱이 무한정 길어지지 않는다.**

### 4.11 액션 파라미터 하드 바운드 — 1.2(c)

검증 헬퍼를 `Actions/HermesActionParams.h` / `.cpp`에 모아 각 핸들러가 호출한다. 검증 로직을 핸들러 밖에 두는 이유는 규칙을 한 곳에서 읽을 수 있게 하고, 엔진 타입에 의존하지 않는 부분을 단독 테스트하기 위함이다.

```cpp
namespace HermesParams
{
    /** 유한하고 |V| <= Limit 인지 검사한다. NaN/Inf는 거부. */
    bool IsValidCoordinate(double V, float Limit);

    /** 유한한 정수이고 1 <= V <= Max 인지 검사한다. int32 캐스트 전에 호출한다. */
    bool IsValidQuantity(double V, int32 Max, int32& OutQty);

    /** 비어있지 않고 길이가 MaxLen 이하인지 검사한다. */
    bool IsValidItemId(const FString& Id, int32 MaxLen);
}
```

**`MoveToActionHandler`** — 3축 각각에 `IsValidCoordinate(V, MaxWorldCoordinate)`를 적용하고 실패 시 `ok=false, error="coordinate out of range"`. `FVector` 생성 **전에** 검사하므로 non-finite 벡터가 엔진에 들어가지 않는다.

**`ItemTransferActionHandler`** — `IsValidQuantity(Qd, MaxItemQuantity, Qty)`로 **`(int32)` 캐스트 이전에** 범위를 확정해 미정의 동작 경로를 제거한다. `item_id`에는 `IsValidItemId(ItemId, MaxItemIdLength)`. 실패 시 각각 `"quantity out of range"`, `"invalid item_id"`.

**`UHermesInventoryComponent::Add()`** — 핸들러 검증과 별개로 누적 오버플로를 자체 차단한다. 이 컴포넌트는 액션 경로 외에서도 호출될 수 있으므로 방어를 겹쳐 둔다.

```cpp
if (UHermesItem* It = Find(ItemId))
{
    // int32 오버플로 방지: 상한에서 포화시킨다
    It->Quantity = (It->Quantity > MAX_int32 - Qty) ? MAX_int32 : It->Quantity + Qty;
    return;
}
```

`follow_player`와 `inventory_manage` 핸들러는 상한 문제가 없어 변경하지 않는다.

### 4.12 보류 발화 상한 — 1.2(e)

`PendingChats`를 `MaxPendingChats`로 제한한다. 가득 차면 **가장 오래된 것부터 버린다** — 대화 맥락상 최신 발화가 살아남는 편이 자연스럽고, 어차피 서버에 닿지 못한 발화들이다. 버릴 때 경고 로그를 남긴다.

경계 처리를 단독 테스트할 수 있도록 순수 헬퍼로 분리한다.

```cpp
namespace HermesUtil
{
    /** Item을 Array 뒤에 넣되 길이가 MaxNum을 넘으면 앞에서부터 버린다. 버린 개수를 반환. */
    int32 PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum);
}
```

### 4.13 액션 레이트 리밋과 타이머 회수 — 1.2(f)

**레이트 리밋.** 토큰 버킷을 `UHermesActionDispatcher`에 둔다. 용량과 초당 충전량 모두 `MaxActionsPerSecond`다. 소진 상태에서 들어온 요청은 핸들러를 거치지 않고 즉시 `ok=false, error="rate limited"`로 회신한다. 서버가 정상이라면 초당 20건에 도달할 일이 없고, 도달했다는 것은 폭주 또는 프롬프트 인젝션으로 액션이 남발되고 있다는 신호다.

시간을 주입받는 순수 클래스로 만들어 단독 테스트한다.

```cpp
class FHermesRateLimiter
{
public:
    void  Configure(int32 PerSecond);
    /** NowSeconds 기준으로 토큰을 채우고 1개를 소비한다. 소비 실패 시 false. */
    bool  TryConsume(double NowSeconds);
private:
    double Tokens = 0.0;
    double LastRefillTime = 0.0;
    double Capacity = 0.0;
    double RefillPerSecond = 0.0;
};
```

**타이머 회수.** `FTimerHandle`을 공유 상태로 올려 완료 시 취소한다.

```cpp
TSharedRef<bool> bDone = MakeShared<bool>(false);
TSharedRef<FTimerHandle> Th = MakeShared<FTimerHandle>();
TWeakObjectPtr<UHermesActionDispatcher> WeakThis(this);

OnDone.BindLambda([bDone, Th, WeakThis, Id, OnResult](bool bOk, TSharedPtr<FJsonObject> Result, FString Error)
{
    if (*bDone) return;
    *bDone = true;
    if (UHermesActionDispatcher* Self = WeakThis.Get())
    {
        if (UWorld* W = Self->GetWorld()) W->GetTimerManager().ClearTimer(*Th);
    }
    OnResult(HermesJson::MakeActionResult(Id, bOk, Result, Error));
});
```

즉시 완료되는 핸들러(대부분)가 타이머를 남기지 않게 되어, 레이트 리밋과 함께 1.2(f)의 자원 누적 경로가 닫힌다.

### 4.14 아웃바운드 큐 상한 — 1.2(g)

`FThreadSafeCounter OutboundCount`로 동일하게 관리한다. `EnqueueOutbound()` 시점에 `MaxOutboundQueueSize`에 도달해 있으면 **새 프레임을 넣지 않고** 경고 로그를 남긴다.

**보류 발화(4.12)와 달리 오래된 쪽이 아니라 새 쪽을 버리는 이유:** `Outbound`는 `EQueueMode::Spsc` 큐이고 게임 스레드는 producer다. SPSC 큐에서 `Dequeue()`는 consumer(워커 스레드)만 호출할 수 있으므로, producer 쪽에서 "가장 오래된 것을 버리는" 동작은 성립하지 않는다. `PendingChats`는 게임 스레드 전용 `TArray`라 그 제약이 없다.

인바운드처럼 연결을 끊지 않는 이유: 아웃바운드 적체는 피어의 악의가 아니라 **연결 단절의 결과**이므로 끊어봐야 상황이 나아지지 않는다. 버려진 `action_result`는 서버 쪽에서 타임아웃으로 관측되며, 이는 프로토콜 §4.5가 이미 정의한 정상 경로다.

### 4.15 요약 — 강건성 장치가 함께 만드는 보장

| 유입 경로 | 장치 | 최악의 결과 |
|---|---|---|
| 수신 프레임 폭주 | 큐 상한(4.10) + 틱 예산(4.10) | 연결 끊고 재연결 |
| 액션 요청 폭주 | 레이트 리밋(4.13) + 타이머 회수(4.13) | 초과분 거부 회신 |
| identified 지연 중 발화 누적 | 보류 상한(4.12) | 오래된 발화 폐기 |
| 연결 단절 중 송신 누적 | 아웃바운드 상한(4.14) | 새 프레임 폐기 |
| 비정상 파라미터 | 하드 바운드(4.11) | `ok=false` 회신 |
| 종료 요청 | 조각 sleep(4.8) | 100ms 내 종료 |
| TLS 핸드셰이크 지연 | 핸드셰이크 타임아웃(6.6) | 연결 끊고 재연결 |
| 조용히 죽은 연결 | keepalive + 수신 침묵 판정(5.7) | `PeerTimeout` 내 재연결 |
| 응답 없는 대화 | 발화 추적 + 델타 갱신(5.8) | `OnChatFailed` 통지 |

## 5. 설계 — 프로토콜 v2 (신원 발급과 세션 운영)

1.3과 1.5를 해소한다. **`ue5-socket-protocol.md`를 v2로 개정하며, 서버는 이 계약을 구현해야 한다.**

### 5.1 원칙

- **신원은 서버가 발급한다.** 클라이언트는 자기 `player_id`를 만들지 않는다.
- **재접속은 자격 증명으로 증명한다.** `player_id`만으로는 세션에 접근할 수 없다.
- **토큰은 서버가 생성한 고엔트로피 값**이며 `player_id`에서 유도되지 않는다. 둘 중 하나가 노출돼도 다른 하나를 알 수 없어야 한다.

### 5.2 최초 접속 — 발급

```
Client → { "type": "identify", "protocol_version": 2, "player_name": "Aria" }
Server → { "type": "identified", "ok": true,
           "player_id": "<서버 생성>", "session_token": "<서버 생성>",
           "chat_id": "<서버 생성>" }
```

`player_id`가 없는 identify는 신규 발급 요청으로 해석된다. 클라이언트는 받은 `player_id`와 `session_token`을 SaveGame에 저장한다.

### 5.3 재접속 — 검증

```
Client → { "type": "identify", "protocol_version": 2,
           "player_id": "...", "session_token": "...", "player_name": "Aria" }
Server → { "type": "identified", "ok": true, "chat_id": "..." }
```

서버는 `(player_id, session_token)` 쌍을 검증한다. 불일치하면 `error { code: "not_authorized" }` 후 연결을 닫는다 (프로토콜 §5의 기존 동작).

**`player_id`만 알고 `session_token`을 모르는 제3자는 여기서 차단된다.**

**토큰을 회전(rotate)하지 않는다.** 회전은 유출된 토큰의 유효 기간을 줄이지만, 저장 실패나 경합 시 플레이어가 자기 세션에서 영구히 잠기는 위험을 만든다. 6절의 TLS로 전송 구간 유출이 차단되면 남는 유출 경로는 기기 소유자의 로컬 추출뿐인데, 회전은 그 경로에 아무 효과가 없다(소유자는 새 토큰도 함께 얻는다). 이득이 위험을 상쇄하지 못하므로 도입하지 않는다.

### 5.4 버전 협상

클라이언트는 항상 `protocol_version: 2`를 보낸다. `identified` 응답에 `session_token`이 없으면 **v1 서버로 판단하고 명확한 에러 로그와 함께 연결을 닫는다.**

v1 호환 모드를 두지 않는 이유: 호환 모드는 곧 "인증 없이도 동작하는 경로"이므로 5절의 목적을 무력화한다. 서버를 재구축하는 상황이라 조용한 불일치보다 **크게 실패하는 편**이 낫다.

### 5.5 클라이언트 변경

**`UHermesSaveGame`** — 토큰 필드 추가.

```cpp
UPROPERTY() FString PlayerId;
UPROPERTY() FString SessionToken;   // 신규
```

**`UHermesConnectionSubsystem`** — `LoadOrCreatePlayerId()`를 `LoadCredentials()`로 교체한다. `FGuid::NewGuid()` 생성 로직을 **삭제**한다 (`HermesConnectionSubsystem.cpp:51-53`). 자격 증명이 없으면 빈 값으로 두고 서버 발급을 기다린다.

`HandleFrame()`의 `identified` 분기에서 응답에 `player_id`/`session_token`이 있으면 SaveGame에 저장한다. 저장 실패 시 경고 로그를 남기되 연결은 유지한다 — 다음 실행에서 새 신원을 발급받게 되며, 이는 데이터 손실이지 보안 결함은 아니다.

**`HermesJson`** — `MakeIdentify()`에 토큰과 버전을 추가하고, `identified` 파싱 헬퍼를 둔다.

```cpp
FString MakeIdentify(const FString& PlayerId, const FString& SessionToken,
                     const FString& PlayerName);

/** identified 프레임에서 자격 증명을 꺼낸다. session_token이 없으면 false(=v1 서버). */
bool ParseIdentified(const TSharedPtr<FJsonObject>& Obj,
                     FString& OutPlayerId, FString& OutToken, FString& OutChatId);
```

`ParseIdentified()`를 순수 함수로 두어 5.4의 버전 판정을 단독 테스트한다.

### 5.6 스트리밍 응답 — 1.5(d)

신규 프레임 `chat_delta`(Server → Client)를 도입한다.

```json
{ "type": "chat_delta", "id": "c-0001", "seq": 0, "text": "알겠어요, " }
```

종결은 기존 `chat_response`가 담당하며 **최종 전체 텍스트를 그대로 싣는다.**

```json
{ "type": "chat_response", "id": "c-0001",
  "text": "알겠어요, 언덕 위로 이동할게요.", "actions": [ ... ] }
```

**델타는 표시용 힌트이고 `chat_response.text`가 정본이다.** 클라이언트는 델타를 이어붙여 즉시 보여주다가, `chat_response`가 오면 그 값으로 **교체**한다. 델타를 놓치거나 중복 처리해도 최종 화면이 자기 교정되며, 델타를 무시하는 구현도 그대로 동작한다.

**서버는 델타를 묶어 보내야 한다.** 토큰 하나당 프레임 하나는 초당 수십~수백 프레임이 되어 4.10의 틱 예산과 큐 상한을 정상 동작 중에 압박한다. **50ms 또는 토큰 몇 개 단위로 묶어 보내는 것**을 프로토콜 문서에 요구사항으로 명시한다. 클라이언트 상한을 이 때문에 올리지는 않는다 — 상한은 비정상 피어를 막기 위한 것이고, 정상 서버가 그 선에 닿는다면 서버 쪽 배치 정책이 잘못된 것이다.

**클라이언트 변경.** `UHermesConnectionSubsystem`에 델리게이트를 추가한다.

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatDelta, const FString& /*Text*/, const FString& /*Id*/);
FOnChatDelta OnChatDelta;
```

`UHermesDialogueWidget`은 델타를 누적 표시하고 `chat_response`에서 전체 텍스트로 교체한다. 액션 요청은 스트리밍과 무관하게 기존 경로로 처리된다.

### 5.7 연결 유지와 죽은 연결 탐지 — 1.5(a)

**클라이언트가 ping을 보낸다.** `KeepAlivePingIntervalSeconds` 동안 아무것도 보내지 않았으면 `ping`을 전송한다.

**수신 침묵으로 사망을 판정한다.** `PeerTimeoutSeconds` 동안 **어떤 종류의 프레임도** 받지 못하면 연결을 죽은 것으로 간주하고 재연결한다. pong뿐 아니라 모든 수신이 생존 신호이므로, 대화가 활발할 때는 별도 ping 없이도 판정이 성립한다.

판정 로직은 게임 스레드(`UHermesConnectionSubsystem::Tick`)에 둔다. 프레임 종류를 아는 쪽이 여기이고, 워커를 프로토콜로부터 분리한 상태를 유지하기 위함이다. 워커에는 재연결을 요청하는 진입점만 추가한다.

```cpp
/** 현재 연결을 끊고 재연결 루프로 돌아가게 한다. 게임 스레드에서 호출. */
void FHermesSocketWorker::RequestReconnect();
```

결정 로직은 시간을 인자로 받는 순수 함수로 분리해 단독 테스트한다.

```cpp
namespace HermesLiveness
{
    enum class EDecision : uint8 { Nothing, SendPing, DeclareDead };

    EDecision Evaluate(double Now, double LastRecvTime, double LastSendTime,
                       float PingInterval, float PeerTimeout);
}
```

연결이 성립하지 않은 동안에는 평가하지 않는다 — 재연결 대기 중에는 수신이 없는 것이 정상이다.

**소켓 수준 keepalive도 함께 켠다.** 평문 경로는 `FSocket`의 keepalive 설정을, TLS 경로는 OpenSSL이 소유한 소켓에 `SO_KEEPALIVE`를 설정한다. OS 기본 keepalive 주기는 보통 2시간이라 단독으로는 쓸모가 없지만, 애플리케이션 ping이 놓치는 하위 계층 경로를 보완한다.

### 5.8 대화 응답 타임아웃과 상관 — 1.5(b), (c)

**진행 중인 발화를 추적한다.** `SendChat()`이 만든 `id`와 전송 시각을 보관하고, `ChatResponseTimeoutSeconds` 안에 아무 응답도 없으면 실패로 처리한다.

**델타는 타이머를 갱신한다.** `chat_delta`가 도착하면 해당 `id`의 마지막 진행 시각을 갱신한다. 이로써 **긴 생성이 타임아웃되지 않으면서도, 생성이 멈추면 탐지된다.** 스트리밍(5.6)이 이 판정을 정확하게 만들어 주는 지점이다.

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatFailed, const FString& /*Id*/, const FString& /*Reason*/);
FOnChatFailed OnChatFailed;
```

**상관 규칙.** 위젯은 **가장 최근에 보낸 발화의 `id`** 와 일치하는 델타·응답만 화면에 반영하고, 그 외 `id`는 무시한다. 늦게 도착한 이전 응답이 현재 화면을 덮어쓰는 문제(1.5-c)가 사라진다. 무시한 프레임은 로그로 남긴다.

연결이 끊기면 진행 중인 발화를 모두 실패 처리하고 추적 목록을 비운다. 재연결 후 서버가 이전 발화를 이어서 응답하더라도 그 `id`는 이미 추적 대상이 아니므로 위 상관 규칙에 의해 무시된다.

추적기도 시간 주입형 순수 클래스로 분리한다.

```cpp
class FHermesPendingChats
{
public:
    void Add(const FString& Id, double Now);
    void Touch(const FString& Id, double Now);        // 델타 수신
    void Remove(const FString& Id);                    // 최종 응답 수신
    void CollectTimedOut(double Now, float Timeout, TArray<FString>& Out);
    void Clear();
};
```

## 6. 설계 — TLS 전송

1.4를 해소한다. **프로토콜 v2는 TLS를 요구한다.**

### 6.1 구성

`FHermesSocketWorker` 안에 전송 계층을 얇게 추상화하고, 평문과 TLS 두 구현을 둔다. 프레이밍(`FHermesFrameCodec`, `FFrameAccumulator`)과 큐·백오프·상한 로직은 **전혀 바뀌지 않는다** — 바이트를 어디로 읽고 쓰는지만 달라진다.

```cpp
/** 연결 지향 바이트 스트림. 논블로킹 시맨틱을 따른다. */
class IHermesTransport
{
public:
    virtual ~IHermesTransport() = default;
    /** 연결 및(TLS라면) 핸드셰이크까지 완료한다. */
    virtual bool Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) = 0;
    virtual void Close() = 0;
    /** 반환: 읽은 바이트 수. 0=지금은 없음. 음수=치명적 오류(재연결). */
    virtual int32 Recv(uint8* Buf, int32 BufSize) = 0;
    /** 반환: 보낸 바이트 수. 0=지금은 불가. 음수=치명적 오류. */
    virtual int32 Send(const uint8* Buf, int32 Num) = 0;
};
```

- `FHermesPlainTransport` — 기존 `FSocket` 경로를 그대로 옮긴다.
- `FHermesTlsTransport` — OpenSSL 경로 (6.3).

이 경계를 두는 부수 효과로, 나중에 큐 상한(4.10, 4.14)을 단위 테스트하고 싶어지면 가짜 전송을 끼울 수 있게 된다. 이번 범위에서는 하지 않는다 (7절).

### 6.2 SSL 컨텍스트 준비

`SSL` 모듈이 제공하는 SSL 초기화와 `SSL_CTX` 생성을 사용한다. 모듈 시작/종료 시점(게임 스레드)에 SSL 초기화·해제를 참조 계수로 관리하고, 컨텍스트 생성은 최초 연결 시 1회 수행해 재연결마다 재사용한다.

- TLS 1.2 미만은 비활성화한다.
- UE가 제공하는 루트 인증서 저장소를 컨텍스트에 주입한다.
- `TlsPrivateCaPath`가 지정되어 있으면 해당 PEM을 신뢰 저장소에 추가한다.

> **구현 시 확인 필요:** `ISslManager` / `ISslCertificateManager`의 정확한 메서드 이름과 시그니처는 설치된 UE 5.8 소스(`Engine/Source/Runtime/Online/SSL`)를 열어 확인한다. 본 문서는 필요한 기능을 규정할 뿐 API 이름을 확정하지 않는다.

### 6.3 연결 수립 절차

1. 4.4의 해석 결과로 TCP 연결을 맺는다. **주소 선택 정책(IPv4 우선)은 평문과 동일하다.**

   **TLS 경로는 `FSocket`을 쓰지 않고 OpenSSL이 소켓을 직접 소유한다.** `FSocket`의 네이티브 핸들을 얻는 공개 API가 없어(`FSocketBSD::GetNativeSocket()`은 `Sockets` 모듈 내부에 있다) 기존 소켓을 TLS로 감쌀 수 없기 때문이다. 대신 4.4가 고른 주소를 숫자 문자열로 변환해 OpenSSL 접속 대상으로 넘긴다. **주소 해석은 여전히 `ISocketSubsystem`이 담당하므로 IPv4 우선 정책과 DNS 동작이 두 경로에서 동일하게 유지된다.**
2. 검증에 쓸 이름을 정한다: `TlsServerName`이 비어 있지 않으면 그 값, 비어 있으면 `Host`.
3. SNI(`server_name` 확장)에 그 이름을 싣는다.
4. 호스트명 검증 대상으로 같은 이름을 설정한다.
5. 논블로킹 핸드셰이크를 진행한다 (6.6).
6. 6.4의 정책으로 서버 인증서를 검증한다. 실패하면 연결을 닫는다.

**IP로 접속하면서 인증서에 도메인명이 든 경우** `TlsServerName`으로 그 도메인을 지정하면 SNI와 검증이 모두 맞는다. 접속 주소와 검증 이름을 분리해 두는 이유가 이것이다.

### 6.4 서버 인증서 검증 정책

| `TlsPinnedPublicKeyHashes` | `TlsPrivateCaPath` | 동작 |
|---|---|---|
| 비어 있지 않음 | (무관) | **핀 검증.** 서버 인증서의 SPKI SHA-256 base64가 목록 중 하나와 일치해야 한다. 체인이 공개 CA로 이어지지 않아도 통과하지만, 불일치하면 무조건 거부 |
| 비어 있음 | 지정됨 | 해당 PEM을 신뢰 저장소에 추가한 뒤 **표준 체인 + 호스트명 검증** |
| 비어 있음 | 비어 있음 | UE 기본 루트 CA로 **표준 체인 + 호스트명 검증** |

**LAN 자체 서명 환경에서는 핀 검증을 권장한다.** `192.168.0.111` 같은 사설 IP에는 공인 인증서를 발급받을 수 없고, 사설 CA를 세우는 것보다 서버 공개키 해시 한 줄을 ini에 넣는 편이 운영이 단순하다.

**핀은 인증서가 아니라 공개키(SPKI)에 건다.** 인증서를 갱신해도 키쌍을 유지하면 핀이 그대로 유효하다. 인증서 지문에 걸면 갱신할 때마다 모든 클라이언트를 다시 배포해야 한다.

핀 값을 만드는 방법을 README에 적는다:

```
openssl x509 -in server.crt -pubkey -noout \
  | openssl pkey -pubin -outform der \
  | openssl dgst -sha256 -binary \
  | openssl enc -base64
```

### 6.5 다운그레이드 금지

- `bUseTLS=true`에서 핸드셰이크나 인증서 검증이 실패하면 **평문으로 재시도하지 않는다.** 연결을 닫고 기존 백오프 재연결에 맡긴다.
- `WITH_SSL`이 0인 플랫폼에서 `bUseTLS=true`이면 연결을 시도하지 않고 에러 로그를 남긴다.
- **Shipping 빌드에서는 `bUseTLS`가 `false`여도 `true`로 강제하고 에러 로그를 남긴다.** 배포된 게임이 평문으로 통신하는 상황을 설정 실수로 만들 수 없게 한다.
- `bUseTLS=false`인 비-Shipping 빌드에서는 **연결할 때마다** 경고 로그를 남긴다. 개발 전용 구성임이 로그에 계속 드러나게 한다.

TLS 설정에 커맨드라인 오버라이드를 두지 않는 것(4.1)도 같은 맥락이다. 검증 정책을 실행 인자로 낮출 수 있으면 그 자체가 취약점이다.

### 6.6 워커 스레드 통합

기존 `Run()` 루프는 논블로킹 소켓 + 폴링 + 5ms 양보 구조다. TLS 전송도 같은 형태를 따른다.

- 소켓을 논블로킹으로 두고, 핸드셰이크와 읽기/쓰기에서 "지금은 더 진행할 수 없음" 신호가 오면 **오류가 아니라 재시도 대상**으로 처리한다.
- 핸드셰이크는 루프 안에서 진행하며 매 반복마다 `bStopRequested`와 경과 시간을 확인한다. `TlsHandshakeTimeoutSeconds`를 넘으면 실패로 처리하고 연결을 닫는다.
- 쓰기가 부분적으로만 진행되면 남은 바이트를 보관했다가 이어서 보낸다. 프레임 경계가 깨지지 않도록 `SendAllPending()`의 기존 부분 송신 처리 방식을 그대로 따른다.

이로써 **종료 응답성(4.8)이 TLS 경로에서도 유지된다** — 핸드셰이크 중에도 100ms 이내에 중단 요청을 확인한다.

### 6.7 프로토콜 문서 변경

`ue5-socket-protocol.md` 헤더의 전송 정의를 바꾼다.

- **Transport:** TLS 1.2 이상 위의 TCP 스트림, 길이 프리픽스 JSON 프레임. 프레이밍 규격(§1)은 **변경 없음** — TLS는 그 아래 계층이다.
- 서버는 인증서를 제시해야 하며, 자체 서명을 쓰는 경우 클라이언트가 핀 고정할 SPKI 해시를 배포 문서에 함께 제공해야 한다.
- 평문 접속은 **개발 전용**이며 배포 구성에서 허용되지 않는다.
- 포트는 `8770`을 유지한다. v2 서버는 이 포트에서 TLS만 수락한다.

### 6.8 남는 위험 (명시)

- **기기 소유자.** `.sav`의 세션 토큰과 ini의 핀 설정 모두 로컬에 평문으로 있다. 기기 소유자는 자기 세션을 추출·복제할 수 있다. 의도된 한계다(2절 비목표).
- **핀 미설정 + 사설 CA 미지정 상태의 사설 IP 접속.** 이 조합은 공개 CA로 검증이 불가능해 연결이 실패한다. 실패가 맞는 동작이지만, 설정 실수 시 원인이 로그로 분명해야 한다 — 검증 실패 사유를 구체적으로 남긴다.
- **키 유출 시 핀 회전.** 서버 키쌍이 유출되면 새 키로 교체하고 클라이언트 ini의 핀을 갱신해 재배포해야 한다. 배포된 클라이언트를 원격으로 갱신하는 수단은 이 설계에 없다.

## 7. 테스트

### 신규 자동화 테스트

**`Settings/HermesSettings.spec.cpp`** — `ApplyCommandLineOverrides()` 대상. 실제 프로세스 커맨드라인이 아니라 테스트가 만든 문자열을 넘긴다.

- 인자 없음 → ini/기본값 유지
- `-HermesHost=` 만 → Host만 덮이고 Port 유지
- `-HermesPort=` 만 → Port만 덮이고 Host 유지
- `-HermesPort=abc` → 기존 Port 유지
- `-HermesPort=0`, `-HermesPort=70000` → 기존 Port 유지
- `-HermesHost=` (빈 값) → 기존 Host 유지

**`Actions/HermesActionParams.spec.cpp`** — 파라미터 검증 헬퍼.

- `IsValidCoordinate`: 정상값 통과 / `NaN`·`±Inf` 거부 / 경계(`Limit`, `Limit+1`) 판정 / 음수 방향 대칭
- `IsValidQuantity`: `1`·`Max` 통과 / `0`·`-1` 거부 / `Max+1` 거부 / `1e18` 거부 / 소수(`1.5`) 거부 / `NaN` 거부
- `IsValidItemId`: 정상 통과 / 빈 문자열 거부 / `MaxLen` 통과, `MaxLen+1` 거부

**`Actions/HermesRateLimiter.spec.cpp`** — 시간 주입형이라 단독 검증이 가능하다.

- 용량 내 연속 요청은 모두 통과
- 용량 소진 후 즉시 요청은 거부
- 1초 경과 후 다시 통과 (충전 확인)
- 시간이 역행해도 토큰이 음수가 되거나 폭증하지 않음

**`Protocol/HermesMessages.spec.cpp` 확장** — 5.4 버전 판정.

- `session_token`을 포함한 `identified` → `ParseIdentified()`가 true, 값이 정확히 추출됨
- `session_token`이 없는 v1 형식 `identified` → false 반환
- `MakeIdentify()`가 자격 증명 없을 때 `player_id`/`session_token` 필드를 넣지 않고 `protocol_version: 2`는 항상 넣음

**`Connection/HermesUtil.spec.cpp`** — `PushBounded()`.

- 상한 미만에서는 버리지 않음 / 상한 도달 시 가장 오래된 것부터 버림 / 반환된 폐기 개수가 정확함

**`Connection/HermesLiveness.spec.cpp`** — 5.7 판정 로직. 시간 주입형이라 단독 검증이 가능하다.

- 최근 수신·송신이 있으면 `Nothing`
- 송신 침묵이 `PingInterval`을 넘으면 `SendPing`
- 수신 침묵이 `PeerTimeout`을 넘으면 `DeclareDead`
- 수신 침묵이 타임아웃을 넘었고 송신 침묵도 넘었을 때 **`DeclareDead`가 우선**한다 (죽은 연결에 ping을 보내지 않는다)
- 경계값(정확히 `PingInterval`, 정확히 `PeerTimeout`)의 판정이 문서와 일치함

**`Connection/HermesPendingChats.spec.cpp`** — 5.8 추적기.

- 추가 후 타임아웃 전에는 수집되지 않음
- 타임아웃 경과 후 수집됨
- `Touch()`(델타 수신)가 타이머를 갱신해 **긴 생성이 타임아웃되지 않음**
- `Remove()` 후에는 수집되지 않음
- `Clear()` 후 목록이 비고, 여러 건 중 만료된 것만 선별 수집됨

**`Transport/HermesTlsPolicy.spec.cpp`** — TLS에서 네트워크 없이 검증 가능한 순수 결정 로직만 대상으로 한다.

- 검증 이름 결정: `TlsServerName`이 비었으면 `Host`, 비어있지 않으면 그 값
- 검증 모드 선택: 핀 있음 → 핀 모드 / 핀 없고 사설 CA 있음 → 사설 CA 모드 / 둘 다 없음 → 기본 CA 모드
- Shipping 강제 규칙: `bUseTLS=false` 입력이 Shipping 구성에서 `true`로 교정됨

**`Inventory/HermesInventory.spec.cpp` 확장**

- `Add(id, MAX_int32)` 후 `Add(id, 1)` → `MAX_int32`에서 포화하고 **음수가 되지 않음**

**자동화 테스트를 만들지 않는 것:**

- **큐 상한(4.10, 4.14)** — `FHermesSocketWorker`가 실제 소켓과 전용 스레드에 묶여 있다. 6.1의 `IHermesTransport` 도입으로 가짜 전송을 끼울 길은 열리지만, 워커의 스레드 수명까지 테스트에 태우는 것은 이 작업의 목적과 무관하게 범위를 키운다.
- **TLS 핸드셰이크·인증서 검증** — 실제 OpenSSL과 서버가 필요하다. 아래 수동 검증으로 확인한다.

### 회귀

기존 자동화 테스트 5종이 통과해야 한다. `HermesMessages.spec.cpp`는 identify 형식이 바뀌므로 **기대값 갱신이 필요하다** — 이 변경은 회귀가 아니라 의도된 프로토콜 변경이다.

```
UnrealEditor-Cmd.exe HermesAgentNPC.uproject -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

### 수동 검증 (통합 테스트 체크리스트에 추가)

설정:

- Project Settings > Plugins > Hermes Agent NPC 화면에 항목이 보이고, 변경 시 `Config/DefaultGame.ini`에 기록된다
- `Host=localhost` → 호스트명이 해석된다
- `-HermesHost=<다른IP>` → ini를 무시하고 해당 IP로 접속을 시도한다

강건성:

- **서버를 끈 상태로 PIE 실행 후 즉시 정지** → 에디터가 멈추지 않는다. `MaxReconnectDelay=300`으로 올리고 백오프가 충분히 커진 뒤에도 재확인
- 도달 불가 호스트명 → 게임 스레드가 멈추지 않고 백오프 재연결이 계속된다
- 프레임 대량 전송 → 메모리가 무한 증가하지 않고, 상한 초과 시 연결이 끊긴 뒤 재연결된다
- `action_request` 초당 100건 전송 → 20건만 처리되고 나머지는 `"rate limited"`로 회신되며 프레임이 유지된다
- TCP는 수락하되 `identified`를 보내지 않는 스텁 서버에 접속 후 발화 반복 → 메모리가 무한 증가하지 않는다
- `move_to`에 `x: 1e308` → `"coordinate out of range"` 회신, 크래시 없음
- `item_transfer`에 `quantity: 2e9` → `ok=false`, 인벤토리 수량이 음수가 되지 않음

신원:

- 세이브 파일이 없는 상태로 최초 접속 → 서버가 `player_id`/`session_token`을 발급하고 `.sav`에 저장된다
- 재시작 후 접속 → 저장된 자격 증명으로 재접속하며 **이전 대화 맥락이 유지된다**
- `.sav`의 `SessionToken`을 임의 값으로 변조 후 접속 → `not_authorized`로 거부되고 연결이 닫힌다
- 다른 플레이어의 `player_id`만 넣고 토큰은 자기 것으로 접속 → 거부된다
- v1 서버에 접속 → 명확한 에러 로그와 함께 연결이 닫히고 조용히 동작하지 않는다

세션 운영:

- 대화 중 응답이 **한 번에 나오지 않고 이어서 출력된다** (스트리밍 확인)
- 델타를 보내다 중단하는 스텁 서버 → `ChatResponseTimeoutSeconds` 후 실패로 표시되고 `"생각 중..."`이 남지 않는다
- 긴 응답(60초 이상 생성)이 델타를 계속 보내는 동안 → 타임아웃되지 않는다
- **연결된 상태에서 서버 프로세스를 강제 종료**(정상 FIN 없이) → `PeerTimeoutSeconds` 안에 죽은 연결로 판정하고 재연결을 시작한다. 플레이어가 말을 걸 때까지 기다리지 않는다
- 유휴 상태를 `KeepAlivePingIntervalSeconds` 이상 유지 → 클라이언트가 ping을 보내고 pong을 받는다
- 이전 발화의 늦은 응답이 도착 → 현재 화면을 덮어쓰지 않고 무시 로그만 남는다

TLS:

- 올바른 인증서와 일치하는 핀 → 접속 성공, 대화·액션이 정상 동작한다
- **핀을 한 글자 바꿈** → 연결이 거부되고 사유가 로그에 남는다
- 핀 미설정 + 사설 CA 지정 + 그 CA로 서명된 인증서 → 접속 성공
- 핀 미설정 + 사설 CA 미지정 + 자체 서명 인증서 → 연결 거부 (기본 CA로 검증 불가)
- `TlsServerName`을 인증서와 다른 이름으로 설정 → 호스트명 불일치로 거부
- **평문 서버에 `bUseTLS=true`로 접속** → 핸드셰이크 실패, **평문으로 폴백하지 않고** 백오프 재연결만 반복
- 핸드셰이크 도중 PIE 정지 → 즉시 종료된다 (6.6)
- Shipping 구성에서 `bUseTLS=False` 설정 → 강제로 TLS가 사용되고 에러 로그가 남는다
- 패킷 캡처로 확인 → `session_token`과 대화 내용이 평문으로 보이지 않는다

## 8. 문서 갱신

- `ue5-socket-protocol.md` → **v2로 개정.** 헤더 전송 정의를 TLS 1.2+ 위의 TCP로 변경(§1 프레이밍 규격 자체는 불변). §3 핸드셰이크에 발급/검증 두 경로, §4.1 identify에 `protocol_version`·`session_token`, §4.2 identified에 `player_id`·`session_token`, §5 `not_authorized`를 선택 사항에서 **필수**로 변경. §6에 클라이언트가 적용하는 파라미터 범위 검증과 레이트 리밋을 명시(서버 구현자가 `ok=false` 회신을 이해할 수 있어야 한다). §7 체크리스트의 `192.168.0.111:8770` 고정 표현을 "설정된 엔드포인트"로 교체. §8 참조 클라이언트도 TLS를 쓰도록 갱신 필요함을 명시.
  - **신규 §4.9 `chat_delta`** — 필드 정의, `chat_response.text`가 정본이라는 규칙, **델타 배치 요구사항(50ms 또는 토큰 몇 개 단위)** 을 서버 구현 요구사항으로 명시.
  - **§4.7 ping/pong 개정** — 양쪽 모두 주기적 ping을 보낼 수 있고, 클라이언트는 `PeerTimeout` 침묵을 연결 사망으로 판정한다는 점을 규정. 서버도 같은 규칙을 두도록 권고.
- `README.md` — 서버 설정 절 추가(Project Settings 경로, ini 예시, 커맨드라인 인자, Shipping에서 비활성화됨). **TLS 설정 절 추가** — 핀 해시 생성 명령(6.4), 사설 CA 사용법, 개발 중 평문 사용 시 주의.
- `HermesAgentNPC_Documentation.html` — 하드코딩 주소 서술을 설정 기반으로 교체하고 TLS 요구사항을 반영.

세 문서 모두 `192.168.0.111`을 플러그인 사양처럼 기술한 부분이 있다. 하드코딩 제거가 목적이므로 문서에서도 걷어낸다.

## 9. 완료 조건

- [ ] 플러그인 소스 전체에서 `192.168.0.111` 문자열이 검색되지 않는다
- [ ] 4.5 표의 4개 소비 지점이 모두 설정을 참조한다
- [ ] `Config/DefaultGame.ini`가 git에 추가되고 사내 주소를 담고 있다
- [ ] 서버가 꺼진 상태에서 PIE 정지가 즉시 완료된다 (백오프 최대치·TLS 핸드셰이크 도중 모두)
- [ ] Shipping 구성에서 `-HermesHost=`가 무시된다
- [ ] 인바운드 큐 상한·틱 예산·아웃바운드 상한·보류 발화 상한이 모두 동작한다
- [ ] 액션 레이트 리밋이 동작하고, 완료된 액션의 타이머가 취소된다
- [ ] `move_to` 비정상 좌표와 `item_transfer` 과대 수량이 `ok=false`로 거부된다
- [ ] `FGuid::NewGuid()` 기반 `player_id` 생성 코드가 삭제되었다
- [ ] 변조된 `session_token`으로는 접속할 수 없다
- [ ] v1 서버 접속 시 조용히 동작하지 않고 명확히 실패한다
- [ ] 핀이 일치할 때만 TLS 연결이 성립하고, 불일치 시 사유가 로그에 남는다
- [ ] TLS 실패가 평문 폴백으로 이어지지 않는다
- [ ] Shipping 구성에서 `bUseTLS=False`가 무시되고 TLS가 강제된다
- [ ] 패킷 캡처에서 세션 토큰과 대화 내용이 평문으로 보이지 않는다
- [ ] NPC 응답이 스트리밍으로 출력되고, `chat_response`가 최종 텍스트로 교체한다
- [ ] 서버 강제 종료 시 `PeerTimeoutSeconds` 안에 죽은 연결을 판정하고 재연결한다
- [ ] 응답 없는 발화가 타임아웃으로 실패 처리되고, 델타가 오는 동안에는 타임아웃되지 않는다
- [ ] 이전 발화의 늦은 응답이 현재 화면을 덮어쓰지 않는다
- [ ] 신규 spec 테스트 7종(`HermesSettings`, `HermesActionParams`, `HermesRateLimiter`, `HermesUtil`, `HermesTlsPolicy`, `HermesLiveness`, `HermesPendingChats`)과 확장된 기존 테스트 2종(`HermesMessages`, `HermesInventory`)이 통과한다
- [ ] 기존 자동화 테스트 5종이 통과한다 (identify 형식 변경분 기대값 갱신 포함)
- [ ] 7절 수동 검증 항목이 확인된다
- [ ] 프로토콜 문서가 v2로 개정되고, README·HTML 문서가 갱신된다

## 10. 후속 과제

### 10.1 위협 모델 — 본 설계 후 상태

| 위협 | 상태 | 담당 |
|---|---|---|
| `player_id`를 안 제3자의 사칭 | **차단** | 5 |
| 경로상 공격자의 도청 | **차단** | 6 |
| 경로상 공격자의 `action_request` 주입 | **차단** | 6 |
| 가짜 서버로의 유인 | **차단** | 6.4 |
| 설정 실수로 인한 평문 통신 | **차단** | 6.5 |
| 악성 피어의 클라이언트 자원 고갈 | **차단** | 4.10~4.14 |
| 비정상 파라미터로 인한 크래시·미정의 동작 | **차단** | 4.11 |
| 기기 소유자의 토큰 추출 | **의도적으로 미해결** | — |
| 서버 키 유출 후 원격 핀 갱신 | 미해결 | 10.2 |
| 서버 자원 고갈 (연결·메시지 폭주) | 미해결 | 10.3 |
| 프롬프트 인젝션 | 부분 완화 (서버 문법 제약 도입 시 크게 축소) | 10.3, 10.6 |

### 10.2 자격 증명·핀 수명 관리

본 설계는 토큰 회전(5.3)과 원격 핀 갱신을 모두 두지 않는다. 서버 키쌍이 유출되면 클라이언트 재배포가 필요하다. 운영 규모가 커지면 다음을 검토한다.

- 서버가 발급하는 단기 토큰 + 갱신 토큰 구조
- 핀 목록에 **백업 핀**(다음 키쌍의 SPKI 해시)을 미리 넣어두는 관행 — 재배포 없이 키 교체가 가능해진다

### 10.3 서버측 과제 (이 저장소 밖)

- **레이트 리밋** — 연결 수·메시지 빈도 제한이 없으면 llama-server(GPU 자원)까지 과부하가 전파된다. 본 설계의 클라이언트측 상한과 레이트 리밋은 **클라이언트를 보호할 뿐 서버를 보호하지 않는다.** TLS와 신원 발급이 도입되면 인증된 세션 단위로 제한을 걸 수 있어 구현이 오히려 쉬워진다.
- **프롬프트 인젝션** — 플레이어 발화에 심긴 지시문으로 LLM이 의도치 않은 액션을 호출할 수 있다. 화이트리스트는 "무엇을 실행할 수 있는가"만 막고 "언제 실행하는가"는 LLM 판단에 남는다. 클라이언트측 완화는 4.11의 파라미터 하드 바운드와 4.13의 레이트 리밋이 전부다. **서버측에서는 10.6(a)의 문법 제약 디코딩이 가장 효과적인 수단이다** — 미등록 명령과 범위 밖 파라미터를 생성 불가능하게 만든다.

### 10.4 DNS 해석 블로킹 잔여 지연

4.8의 조각 sleep으로도 `GetAddressInfo()` 자체는 중간에 깰 수 없어, 종료 응답이 OS DNS 타임아웃만큼 지연될 수 있다. 본 설계가 악화시키지 않으므로 유예하되, 문제가 되면 해석 결과 캐싱이나 비동기 해석으로 처리한다.

### 10.5 서버 재구축 시 모듈 경계

서버를 새로 지을 때 다음 다섯 축을 분리하면 이후 교체·확장이 쉬워진다.

- **전송/세션** — TLS 종단, 소켓 수락, 프레이밍, 자격 증명 검증, `player_id` ↔ 연결 바인딩, 재접속 시 세션 복원
- **신원 저장소** — `player_id` ↔ `session_token` 매핑. 5절이 요구하는 신규 구성 요소이며, 재시작 후에도 유지되어야 플레이어가 세션에서 잠기지 않는다
- **대화 저장소** — `chat_id`별 히스토리. 분리하면 서버 재시작에도 맥락이 유지되고 인스턴스 확장이 가능해진다
- **LLM 백엔드** — llama-server 호출을 인터페이스로 감싸면 llama.cpp ↔ vLLM ↔ 클라우드 API 교체가 설정 변경으로 끝난다
- **액션 카탈로그** — 화이트리스트 명령 정의와 LLM 툴 스키마를 한 곳에서 선언하면, 프로토콜 문서 §6·서버 툴 정의·UE 핸들러 세 곳을 손으로 맞추다 어긋나는 문제(1.2-c의 원인)를 구조적으로 줄일 수 있다

TLS 종단을 리버스 프록시(nginx 등)에 맡기고 애플리케이션은 평문 루프백만 다루는 구성도 가능하다. 그 경우 인증서·핀 관리가 프록시 설정으로 분리되어 서버 코드가 단순해진다.

클라이언트는 `ue5-socket-protocol.md`가 자기완결적 계약 역할을 하므로 서버 내부 구조에 영향받지 않는다.

### 10.6 llama-server 연동 가이드 (서버 구현 참고)

에이전트 서버 ↔ llama-server 구간은 이 저장소 밖이지만, 위 프로토콜 요구사항을 만족하려면 이 구간의 설계가 따라와야 한다. 재구축 시 참고할 지침을 남긴다.

**전송 방식에는 선택의 여지가 거의 없다.** `llama-server`는 HTTP만 노출한다 — OpenAI 호환 `/v1/chat/completions`와 네이티브 `/completion`. 소켓 프로토콜은 없다. 따라서 과제는 프로토콜 교체가 아니라 HTTP를 제대로 쓰는 것이다.

**(a) 문법 제약 디코딩 — 액션 화이트리스트의 구조적 강제**

가장 중요한 항목이다. 지금은 LLM이 자유 생성한 출력에서 JSON을 추출하고 사후에 걸러낸다. llama.cpp는 GBNF 문법 또는 JSON 스키마로 **디코딩 단계에서 출력 형태를 강제**할 수 있다.

`command`를 화이트리스트 4종의 enum으로, `quantity`를 `minimum`/`maximum`이 있는 정수로 스키마에 박아두면, **범위 밖 값이나 미등록 명령은 생성 자체가 불가능해진다.** 걸러지는 것이 아니라 토큰 샘플링에서 배제된다. 프롬프트 인젝션이 다른 툴 호출을 유도해도 스키마에 없는 이름은 나올 수 없으므로, 10.3의 프롬프트 인젝션 위험이 실질적으로 축소된다.

**클라이언트의 파라미터 하드 바운드(4.11)는 그대로 둔다.** 클라이언트가 서버를 신뢰하지 않는 것이 이 설계의 전제이므로 두 겹으로 간다.

**(b) 스트리밍 (5.6의 전제)**

`llama-server`는 SSE 스트리밍을 지원한다. 에이전트는 이를 받아 `chat_delta` 프레임으로 중계하되, **토큰 단위가 아니라 50ms 또는 토큰 몇 개 단위로 묶어** 보낸다 (5.6). 묶지 않으면 클라이언트의 틱 예산과 큐 상한을 정상 동작 중에 압박한다.

**(c) KV 캐시 재사용과 슬롯 관리**

- 프롬프트 접두사(시스템 프롬프트 + 대화 히스토리) 캐시 재사용을 켜면 응답 지연이 크게 줄어든다.
- 병렬 슬롯 수가 동시 처리 한계다. NPC나 플레이어가 여럿이면 이 값이 곧 처리량 상한이 된다.
- 슬롯 상태를 디스크에 저장·복원할 수 있으므로, 재접속 시 KV 캐시째 복원하면 맥락 재구성 비용이 사라진다. **10.5의 "대화 저장소" 설계와 직결되는 지점이다.**

**(d) 연결과 부하 관리**

- llama-server로의 HTTP 연결은 keep-alive로 재사용한다. 요청마다 TCP(및 TLS) 핸드셰이크를 반복하지 않는다.
- `/health`를 폴링해 모델 로딩 중과 준비 완료를 구분하고, 준비 전에는 클라이언트에 그 상태를 알린다.
- **슬롯이 포화되면 큐에 넣되 큐 깊이에 상한을 둔다.** GPU는 하나뿐이므로 이 상한이 10.3에서 말한 서버측 과부하 방어의 실제 구현 지점이다. 5절의 인증이 도입되면 세션 단위로 제한을 걸 수 있어 구현이 오히려 쉬워진다.

> llama.cpp의 정확한 파라미터·엔드포인트 이름은 버전에 따라 달라진다. 구현 시 사용하는 빌드의 `server` 문서로 확인한다. 본 절은 필요한 기능을 규정할 뿐 API 이름을 확정하지 않는다.
