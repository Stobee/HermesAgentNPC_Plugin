# Hermes Agent NPC — 설정 전역화 · 입력 강건성 · 프로토콜 v2 신원 발급

- 작성일: 2026-07-28
- 상태: 승인 대기
- 대상 브랜치: master
- 범위: **UE5 클라이언트 플러그인 + 프로토콜 문서.** 서버 구현은 별도(재구축 예정)이며, 본 문서의 5절이 서버가 지켜야 할 계약이다.

## 0. 이 문서가 해결하는 것과 하지 않는 것

작업이 세 겹으로 커졌으므로 먼저 성격을 구분한다.

| 겹 | 성격 | 절 |
|---|---|---|
| 설정 전역화 | 배포성 | 4.1~4.7 |
| 입력 강건성 | **가용성** — 클라이언트가 이상 입력에 무너지지 않게 함 | 4.8~4.15 |
| 신원 발급 | **보안** — 제3자의 세션 탈취 차단 | 5 |

**입력 강건성은 보안이 아니다.** 메모리 고갈·프레임 행·미정의 동작을 막는 것은 클라이언트를 견고하게 만들 뿐, 접근 통제와 무관하다. 실제 보안 효과는 5절에서만 나온다.

**5절도 TLS 없이는 절반이다.** 세션 토큰이 평문으로 오가므로 경로상 공격자는 발급 시점이나 재접속마다 그대로 취득할 수 있다. 5절이 막는 것은 **"player_id를 알아낸 제3자의 사칭"** 이고, 막지 못하는 것은 **"경로상 공격자"** 와 **"기기 소유자 본인"** 이다. 이 한계를 알고 채택한다. 남은 격차는 9.2(TLS)가 담당한다.

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

## 2. 목표와 비목표

### 목표

- 플러그인 소스에서 `192.168.0.111`을 완전히 제거한다.
- 도입 개발자가 **에디터 Project Settings**에서, 배포 담당자가 **재컴파일 없이 ini·커맨드라인**으로 서버를 지정할 수 있다.
- Host에 **호스트명(도메인)** 을 쓸 수 있다.
- 1.1의 상수 4종을 같은 설정 체계로 흡수한다.
- 1.2의 (a)~(g)를 해소해, 신뢰할 수 없는 피어에 붙어도 클라이언트가 메모리 고갈·프레임 행·미정의 동작에 빠지지 않는다.
- 1.3을 해소해, **`player_id`를 아는 것만으로는 남의 세션에 접근할 수 없게** 한다.

### 비목표 (명시적 제외)

- **TLS.** 별도 스펙. 9.2 참조. 5절 인증이 경로상 공격자를 막지 못하는 원인이다.
- **기기 소유자로부터의 보호.** 세션 토큰은 로컬 SaveGame에 평문 저장되며, 이를 숨기려는 시도(난독화·암호화)는 하지 않는다. 클라이언트에 심은 비밀은 그 기기 소유자에게 결국 노출되므로 obscurity에 불과하다.
- **서버측 레이트 리밋·프롬프트 인젝션 방어.** 이 저장소 범위 밖이다. 9.3 참조.
- **런타임 서버 전환 API.** 설정은 `Initialize()`에서 1회 읽고 확정된다.
- **최종 사용자 설정 노출.**
- **설정 저장 즉시 재연결.** 에디터 값 변경은 **다음 PIE/게임 시작부터** 적용된다.
- **`FHermesFrameCodec::MaxBodySize`(1 MiB) 설정화.** 4.3 참조.
- **환경별 DataAsset 프로필.**
- **아이템 카탈로그 검증.** `item_id`의 형식 검증(빈 문자열·길이)까지만 하고 게임 데이터 대조는 확장 지점으로 남긴다.
- **v1 서버 호환 모드.** 5.4 참조.

## 3. 접근법 결정

`UDeveloperSettings` 파생 클래스 하나(`UHermesSettings`)를 두고, 그 위에 커맨드라인 오버라이드 레이어를 얹는다.

검토했으나 채택하지 않은 대안:

- **`UObject(config=Game)` + `GConfig` 수동 읽기** — 의존성이 없어 가볍지만 에디터 설정 UI가 생기지 않는다. 도입 개발자가 ini 키 이름을 문서에서 찾아 손으로 써야 하므로 목표에 어긋난다.
- **`UDataAsset` 프로필 + 세팅에서 활성 프로필 선택** — 설정 계층이 2단이 되어 값의 출처 추적이 어려워진다. 환경 전환은 커맨드라인으로 이미 해결된다.

## 4. 설계 — 설정과 강건성

### 4.1 값의 우선순위

1. **플러그인 기본값** — `UHermesSettings` 프로퍼티 초기화자. 중립값 `127.0.0.1:8770`.
2. **프로젝트 ini** — `Config/DefaultGame.ini`. 에디터 UI가 이 파일에 쓴다.
3. **커맨드라인** — `-HermesHost=` / `-HermesPort=`. Host/Port에만 적용되며 Shipping 빌드에서는 비활성화된다 (4.9).

커맨드라인 계층을 Host/Port로 한정하는 이유는 그 목적이 "같은 빌드를 다른 서버에 붙이는 것"이기 때문이다. 백오프나 추적 거리를 실행할 때마다 바꿔야 할 시나리오는 없다.

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

    /** 송신 대기 프레임 상한. 초과 시 가장 오래된 것부터 버린다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="16", ClampMax="8192"))
    int32 MaxOutboundQueueSize = 256;

    /** identified 이전에 보류할 수 있는 최대 발화 수. 초과 시 가장 오래된 것부터 버린다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="512"))
    int32 MaxPendingChats = 32;

    /** 초당 처리할 최대 액션 요청 수. 초과분은 즉시 거부 회신한다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="1000"))
    int32 MaxActionsPerSecond = 20;

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

### 4.3 `MaxBodySize`를 제외하는 근거

- 서버와 합의된 **프로토콜 불변식**이다 (`ue5-socket-protocol.md` §1). 클라이언트만 올려도 서버가 1 MiB에서 끊으면 효과가 없고 원인 파악이 어려운 장애가 된다.
- `FHermesFrameCodec` / `FFrameAccumulator`는 엔진 게임플레이 타입에 의존하지 않는 순수 로직으로 설계되어 `HermesFrameCodec.spec.cpp`가 단독 검증한다. `UHermesSettings` 참조를 넣으면 그 격리가 깨진다.

컴파일 타임 안전 상한으로 유지한다.

### 4.4 DNS 해석

`HermesSocketWorker.cpp:47`의 IPv4 전용 파싱을 주소 해석으로 교체한다.

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

소켓 생성(`FTcpSocketBuilder`)은 선택된 주소의 프로토콜 타입을 따르도록 한다.

### 4.5 소비 지점 변경

| 파일 | 현재 | 변경 후 |
|---|---|---|
| `Connection/HermesConnectionSubsystem.cpp:13-15` | `static const` 3개 | 삭제. `GetDefault<UHermesSettings>()`로 읽어 Worker에 주입 |
| `Transport/HermesSocketWorker.cpp:147-148` | 지역 상수 `0.5f`/`30.f` | 생성자 주입 |
| `Actions/HermesActionDispatcher.cpp:64` | 리터럴 `15.f` | `GetDefault<UHermesSettings>()->ActionTimeoutSeconds` |
| `NPC/HermesNPCCharacter.cpp:66` | 리터럴 `150.f` | `GetDefault<UHermesSettings>()->FollowDistance` |

**`FHermesSocketWorker`가 설정을 직접 읽지 않고 주입받는 이유:** 워커는 전용 스레드에서 실행되는데 `UHermesSettings`는 `UObject`다. 워커 스레드에서 UObject를 만지는 것을 피하기 위해, 값은 `Initialize()`(게임 스레드)에서 읽어 평범한 값 타입으로 복사해 넘긴다. "다음 PIE부터 적용" 정책과도 일치한다.

`UHermesActionDispatcher`와 `AHermesNPCCharacter`는 게임 스레드 전용이므로 `GetDefault<>` 직접 읽기가 안전하다.

```cpp
struct FHermesWorkerConfig
{
    FString Host;
    int32   Port                  = 8770;
    float   InitialReconnectDelay = 0.5f;
    float   MaxReconnectDelay     = 30.f;
    int32   MaxInboundQueueSize   = 1024;
    int32   MaxOutboundQueueSize  = 256;
};

explicit FHermesSocketWorker(const FHermesWorkerConfig& InConfig);
```

인자가 6개로 늘어나므로 위치 인자 나열 대신 설정 구조체를 받는다. 호출부에서 어떤 값이 무엇인지 읽히고, 항목이 추가돼도 시그니처가 흔들리지 않는다.

### 4.6 빌드 설정

`HermesAgentNPC.Build.cs`의 `PublicDependencyModuleNames`에 `"DeveloperSettings"` 추가. 런타임 모듈이므로 패키징에 문제없다.

### 4.7 프로젝트 ini

`Config/DefaultGame.ini` 신규 생성:

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=192.168.0.111
Port=8770
```

`Config/`는 현재 git 미추적이나 `.gitignore` 대상은 아니다. `DefaultEngine.ini`, `DefaultInput.ini`와 함께 git에 추가한다. 이로써 사내 주소는 **플러그인이 아니라 이 프로젝트의 설정**으로 이동한다.

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

`ConnectSocket()` 안의 `GetAddressInfo()` DNS 블로킹은 이 방식으로 줄일 수 없다(OS 호출이라 중간에 깰 수 없음). 다만 그 상한은 OS 기본 DNS 타임아웃이고 설정으로 늘어나지 않으므로 본 설계가 악화시키지 않는다. 9.4에 잔여 과제로 기록한다.

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

**이것이 방어의 전부가 아님을 명시한다.** 서버 주소는 클라이언트가 알아야 접속할 수 있으므로 패킷 캡처로 언제든 드러나고, 바이너리를 소유한 사용자는 ini를 직접 고칠 수도 있다. 주소 은닉은 obscurity이지 보안이 아니다. **접근 통제의 본체는 5절의 신원 발급이다.** 이 조치의 역할은 "기본 경로를 막는 것"까지다.

### 4.10 인바운드 큐 상한과 틱 예산 — 1.2(a)

**큐 상한 (워커 스레드 측).** `TQueue`는 크기 조회를 제공하지 않으므로 `FThreadSafeCounter InboundCount`를 함께 둔다.

- `ReceiveAvailable()`이 `Enqueue()` 할 때마다 `Increment()`, `DequeueInbound()` 성공 시 `Decrement()`
- `InboundCount`가 `Config.MaxInboundQueueSize`를 넘으면 `ReceiveAvailable()`이 `false`를 반환 → 기존 프레이밍 위반과 **동일 경로**로 `CloseSocket()` 후 백오프 재연결

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

## 5. 설계 — 프로토콜 v2 신원 발급

1.3을 해소한다. **`ue5-socket-protocol.md`를 v2로 개정하며, 서버는 이 계약을 구현해야 한다.**

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

**`player_id`만 알고 `session_token`을 모르는 제3자는 여기서 차단된다.** 이것이 5절의 유일한 보안 효과다.

재접속 시 토큰을 회전(rotate)하지 않는다. 회전은 토큰 유출의 노출 창을 줄이지만, 저장 실패·경합 시 플레이어가 자기 세션에서 영구히 잠기는 위험을 만든다. 평문 전송(9.2 미해결) 상태에서는 회전의 이득이 그 위험을 상쇄하지 못한다. TLS 도입 시 재검토한다.

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

### 5.6 남는 위험 (명시)

- **평문 전송.** 토큰이 발급 시점과 매 재접속마다 평문으로 흐른다. 경로상 공격자는 이를 취득해 사칭할 수 있다. → 9.2
- **로컬 평문 저장.** `.sav`는 암호화되지 않아 기기 소유자는 토큰을 읽고 복사할 수 있다. 이는 **의도된 한계**다(2절 비목표). 클라이언트에 심은 비밀은 그 기기에서 지킬 수 없다.
- **서버 신원 미검증.** 클라이언트는 접속 대상이 진짜 Hermes 서버인지 확인하지 않는다. TLS 인증서 검증이 도입돼야 해결된다. → 9.2

## 6. 테스트

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

**`Inventory/HermesInventory.spec.cpp` 확장**

- `Add(id, MAX_int32)` 후 `Add(id, 1)` → `MAX_int32`에서 포화하고 **음수가 되지 않음**

**큐 상한(4.10, 4.14)은 자동화 테스트를 만들지 않는다.** `FHermesSocketWorker`는 실제 `FSocket`과 전용 스레드에 묶여 있어, 단위 테스트로 검증하려면 소켓을 인터페이스로 추상화하는 리팩터링이 선행되어야 한다. 그 리팩터링은 이 작업의 목적과 무관하게 범위를 크게 키운다. 아래 수동 검증에서 실제 폭주 시나리오로 확인한다.

### 회귀

기존 자동화 테스트 5종이 통과해야 한다. `HermesMessages.spec.cpp`는 identify 형식이 바뀌므로 **기대값 갱신이 필요하다** — 이 변경은 회귀가 아니라 의도된 프로토콜 변경이다.

```
UnrealEditor-Cmd.exe HermesAgentNPC.uproject -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

### 수동 검증 (통합 테스트 체크리스트에 추가)

설정:

- Project Settings > Plugins > Hermes Agent NPC 화면에 항목이 보이고, 변경 시 `Config/DefaultGame.ini`에 기록된다
- ini에 `Host=192.168.0.111`을 두고 PIE → 접속·대화·액션이 동작한다
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
- v1 서버(구버전)에 접속 → 명확한 에러 로그와 함께 연결이 닫히고, 조용히 동작하지 않는다

## 7. 문서 갱신

- `ue5-socket-protocol.md` → **v2로 개정.** §3 핸드셰이크에 발급/검증 두 경로, §4.1 identify에 `protocol_version`·`session_token`, §4.2 identified에 `player_id`·`session_token`, §5 `not_authorized`를 선택 사항에서 **필수**로 변경. §6에 클라이언트가 적용하는 파라미터 범위 검증과 레이트 리밋을 명시(서버 구현자가 알아야 `ok=false` 회신을 이해할 수 있다). §7 체크리스트의 `192.168.0.111:8770` 고정 표현을 "설정된 엔드포인트"로 교체.
- `README.md` — 서버 설정 절 추가(Project Settings 경로, ini 예시, 커맨드라인 인자, Shipping에서 비활성화됨). 프로토콜 v2 요구사항 명시.
- `HermesAgentNPC_Documentation.html` — 하드코딩 주소 서술을 설정 기반으로 교체.

세 문서 모두 `192.168.0.111`을 플러그인 사양처럼 기술한 부분이 있다. 하드코딩 제거가 목적이므로 문서에서도 걷어낸다.

## 8. 완료 조건

- [ ] 플러그인 소스 전체에서 `192.168.0.111` 문자열이 검색되지 않는다
- [ ] 4.5 표의 4개 소비 지점이 모두 설정을 참조한다
- [ ] `Config/DefaultGame.ini`가 git에 추가되고 사내 주소를 담고 있다
- [ ] 서버가 꺼진 상태에서 PIE 정지가 즉시 완료된다 (백오프 최대치에서도)
- [ ] Shipping 구성에서 `-HermesHost=`가 무시된다
- [ ] 인바운드 큐 상한·틱 예산·아웃바운드 상한·보류 발화 상한이 모두 동작한다
- [ ] 액션 레이트 리밋이 동작하고, 완료된 액션의 타이머가 취소된다
- [ ] `move_to` 비정상 좌표와 `item_transfer` 과대 수량이 `ok=false`로 거부된다
- [ ] `FGuid::NewGuid()` 기반 `player_id` 생성 코드가 삭제되었다
- [ ] 변조된 `session_token`으로는 접속할 수 없다
- [ ] v1 서버 접속 시 조용히 동작하지 않고 명확히 실패한다
- [ ] 신규 spec 테스트 4종(`HermesSettings`, `HermesActionParams`, `HermesRateLimiter`, `HermesUtil`)과 확장된 기존 테스트 2종(`HermesMessages`, `HermesInventory`)이 통과한다
- [ ] 기존 자동화 테스트 5종이 통과한다 (identify 형식 변경분 기대값 갱신 포함)
- [ ] 6절 수동 검증 항목이 확인된다
- [ ] 프로토콜 문서가 v2로 개정되고, README·HTML 문서가 갱신된다

## 9. 후속 과제 (별도 스펙)

### 9.1 미해결로 남는 위협

| 위협 | 본 설계 후 상태 | 담당 |
|---|---|---|
| `player_id`를 안 제3자의 사칭 | **차단됨** | 5절 |
| 경로상 공격자의 도청 | 미해결 | 9.2 |
| 경로상 공격자의 `action_request` **주입** | 미해결 | 9.2 |
| 가짜 서버로의 유인 | 미해결 | 9.2 |
| 기기 소유자의 토큰 추출 | **의도적으로 미해결** | — |
| 서버 자원 고갈 | 미해결 | 9.3 |
| 프롬프트 인젝션 | 부분 완화 | 9.3 |

### 9.2 TLS 전송

평문 TCP(`HermesSocketWorker.cpp:56`)라 인터넷 구간에서 대화 내용과 액션 명령이 노출된다.

**도청보다 주입이 더 큰 위험이다.** 인증도 암호화도 없는 구간에서는 경로상 공격자가 엿듣는 데 그치지 않고 `action_request` 프레임을 직접 만들어 넣을 수 있다. 화이트리스트(4.11 포함)가 피해 범위를 제한하지만, NPC는 여전히 공격자가 고른 액션을 실행한다. 5절의 토큰 인증도 평문 구간에서는 토큰 자체가 취득되므로 이 위협을 막지 못한다.

`FSslSocket`/`SslModule` 경유 또는 WebSocket(wss) 전환이 필요하며 서버가 함께 바뀌어야 한다. **서버 인증서 검증**을 반드시 포함해야 하며(그래야 5.6의 "서버 신원 미검증"이 닫힌다), 이때 5.3의 토큰 회전 정책도 재검토한다.

### 9.3 서버측 과제 (이 저장소 밖)

- **레이트 리밋** — 연결 수·메시지 빈도 제한이 없으면 llama-server(GPU 자원)까지 과부하가 전파된다. 본 설계의 클라이언트측 상한과 레이트 리밋은 **클라이언트를 보호할 뿐 서버를 보호하지 않는다.**
- **프롬프트 인젝션** — 플레이어 발화에 심긴 지시문으로 LLM이 의도치 않은 액션을 호출할 수 있다. 화이트리스트는 "무엇을 실행할 수 있는가"만 막고 "언제 실행하는가"는 LLM 판단에 남는다. 클라이언트측 완화는 4.11의 파라미터 하드 바운드와 4.13의 레이트 리밋이 전부다.

### 9.4 DNS 해석 블로킹 잔여 지연

4.8의 조각 sleep으로도 `GetAddressInfo()` 자체는 중간에 깰 수 없어, 종료 응답이 OS DNS 타임아웃만큼 지연될 수 있다. 본 설계가 악화시키지 않으므로 유예하되, 문제가 되면 해석 결과 캐싱이나 비동기 해석으로 처리한다.

### 9.5 서버 재구축 시 모듈 경계

서버를 새로 지을 때 다음 다섯 축을 분리하면 이후 교체·확장이 쉬워진다.

- **전송/세션** — 소켓 수락, 프레이밍, 자격 증명 검증, `player_id` ↔ 연결 바인딩, 재접속 시 세션 복원
- **신원 저장소** — `player_id` ↔ `session_token` 매핑. 5절이 요구하는 신규 구성 요소이며, 재시작 후에도 유지되어야 플레이어가 세션에서 잠기지 않는다
- **대화 저장소** — `chat_id`별 히스토리. 분리하면 서버 재시작에도 맥락이 유지되고 인스턴스 확장이 가능해진다
- **LLM 백엔드** — llama-server 호출을 인터페이스로 감싸면 llama.cpp ↔ vLLM ↔ 클라우드 API 교체가 설정 변경으로 끝난다
- **액션 카탈로그** — 화이트리스트 명령 정의와 LLM 툴 스키마를 한 곳에서 선언하면, 프로토콜 문서 §6·서버 툴 정의·UE 핸들러 세 곳을 손으로 맞추다 어긋나는 문제(1.2-c의 원인)를 구조적으로 줄일 수 있다

클라이언트는 `ue5-socket-protocol.md`가 자기완결적 계약 역할을 하므로 서버 내부 구조에 영향받지 않는다.
