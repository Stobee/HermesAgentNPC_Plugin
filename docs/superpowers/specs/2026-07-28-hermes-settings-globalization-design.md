# Hermes Agent NPC — 서버 설정 전역화 및 입력 강건성 설계

- 작성일: 2026-07-28
- 상태: 승인 대기
- 대상 브랜치: master
- 범위: **UE5 클라이언트 플러그인만.** 서버 프로토콜 변경 없음 (v1 유지)

## 1. 문제

### 1.1 서버 주소 하드코딩

플러그인 내부에 백엔드 서버 주소가 박혀 있다.

`Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:13-15`

```cpp
static const TCHAR* HermesHost = TEXT("192.168.0.111");
static const int32  HermesPort = 8770;
static const TCHAR* SaveSlot   = TEXT("HermesPlayer");
```

`UHermesConnectionSubsystem::Initialize()`가 이 상수로 `FHermesSocketWorker`를 즉시 생성하므로, 플러그인을 도입한 프로젝트가 서버를 바꾸려면 플러그인 C++ 소스를 수정하고 재컴파일해야 한다. "복사해 넣으면 바로 쓰는 완제품 플러그인"이라는 목표와 충돌한다.

같은 성격의 하드코딩이 3곳 더 있다.

| 위치 | 값 | 의미 |
|---|---|---|
| `HermesSocketWorker.cpp:147-148` | `0.5f` / `30.f` | 재연결 백오프 초기값 / 상한 |
| `HermesActionDispatcher.cpp:64` | `15.f` | 액션 응답 타임아웃(초) |
| `HermesNPCCharacter.cpp:66` | `150.f` | 플레이어 추적 유지 거리(cm) |

### 1.2 신뢰 경계 확장에 따르는 강건성 부족

주소를 설정으로 여는 순간 **클라이언트가 붙는 대상이 고정 사내 IP에서 임의 주소로 넓어진다.** 지금까지는 서버를 암묵적으로 신뢰해도 무방했으나, 앞으로는 오작동하거나 악의적인 피어를 상정해야 한다. 코드 확인 결과 다음 취약점이 있다.

**(a) 인바운드 큐 무제한 — 메모리 고갈 및 프레임 행**

`HermesSocketWorker.h:44-45`의 `TQueue` 두 개에 크기 제한이 없다.

- `ReceiveAvailable()`(`HermesSocketWorker.cpp:115~`)이 `while (Socket->HasPendingData(Pending))` 루프로 계속 수신해 `Inbound`에 무한 적재한다.
- 게임 스레드 `HermesConnectionSubsystem.cpp:109`가 `while (Worker->DequeueInbound(Json))` 로 **한 틱에 큐 전체를 소비**하며 각 프레임마다 JSON 파싱과 액션 디스패치를 수행한다. 프레임이 대량으로 쌓이면 그 틱이 통째로 멈춘다.

**(b) 중단 불가능한 백오프 sleep — 종료 시 게임 스레드 행**

`HermesSocketWorker.cpp:161-162`:

```cpp
const float Jitter = FMath::FRandRange(0.f, Backoff * 0.25f);
FPlatformProcess::Sleep(Backoff + Jitter);   // 최대 30초, 중간에 깨울 수 없음
```

`~FHermesSocketWorker()`(`HermesSocketWorker.cpp:16-25`)는 `Thread->Kill(true)`로 `Run()` 반환을 대기한다. 서버가 꺼진 상태에서 PIE를 정지하면 게임 스레드가 최대 30초 멈춘다.

**이 문제는 본 설계가 직접 악화시킨다.** `MaxReconnectDelay`를 설정으로 열면서 상한을 300초로 두면 최악 5분 행이 된다. 따라서 이 설계 안에서 반드시 함께 고쳐야 한다.

**(c) 액션 파라미터 상한 검증 부재**

`UHermesActionDispatcher::Dispatch()`(`HermesActionDispatcher.cpp:18-33`)는 `CanHandle()`로 **명령 이름만** 화이트리스트 검사하고, 파라미터 검증은 각 핸들러 자율에 맡긴다. 하한(음수·0)은 이미 막혀 있으나 상한과 유한성이 비어 있다.

| 위치 | 문제 |
|---|---|
| `MoveToActionHandler.cpp:17-25` | 좌표 무검증. 숫자이기만 하면 통과한다. `1e308` 입력 시 `(float)` 캐스트로 `inf`가 되어 `FVector(inf,inf,inf)`가 `MoveToLocation()`에 전달된다 |
| `ItemTransferActionHandler.cpp:25` | `quantity` 상한 없음. `2e9`는 `Qd < 1` 검사를 통과하고 int32에도 들어가, `HermesInventoryComponent.cpp:31`의 `It->Quantity += Qty`에서 **int32 오버플로 → 음수 수량**이 된다 |
| `ItemTransferActionHandler.cpp:30` | `(int32)Qd` — `Qd > INT32_MAX`면 C++ 미정의 동작 |
| `ItemTransferActionHandler.cpp:24` | `item_id` 무검증. 빈 문자열이나 임의 길이 문자열로 인벤토리 항목이 생성된다 |

참고로 **음수 수량을 이용한 아이템 복제 경로는 이미 이중으로 차단되어 있다** (`ItemTransferActionHandler.cpp:25`의 `Qd < 1`, `HermesInventoryComponent.cpp:25,43`의 `Qty <= 0`). 이번에 메우는 것은 반대쪽인 상한과 유한성이다.

`follow_player`(bool만 사용)와 `inventory_manage`(고정 문자열 비교 후 조회 키로만 사용)는 검토 결과 문제가 없어 수정 대상이 아니다.

**(d) 커맨드라인 오버라이드가 만드는 리다이렉트 표면**

본 설계가 추가하는 `-HermesHost=` 인자는 배포된 게임에서도 동작하면 최종 사용자가 클라이언트를 임의 서버로 붙일 수 있게 한다. 기능을 만드는 쪽에서 함께 막는다.

## 2. 목표와 비목표

### 목표

- 플러그인 소스에서 `192.168.0.111`을 완전히 제거한다.
- 플러그인 도입 개발자가 **UE 에디터 Project Settings 화면**에서 서버 주소를 지정할 수 있다.
- 빌드/배포 담당자가 **재컴파일 없이** ini 수정 또는 커맨드라인 인자로 서버를 전환할 수 있다.
- Host에 **호스트명(도메인)** 을 쓸 수 있다. 컨테이너명·k8s 서비스명·클라우드 엔드포인트 대응.
- 1.1의 상수 4종을 같은 설정 체계로 흡수한다.
- 1.2의 (a)~(d)를 해소해, 신뢰할 수 없는 피어에 붙어도 클라이언트가 메모리 고갈·프레임 행·미정의 동작에 빠지지 않는다.

### 비목표 (명시적 제외)

- **서버 인증.** identify에 토큰을 추가하는 작업은 프로토콜 변경이 필요하므로 별도 스펙으로 분리한다. 8절 참조.
- **TLS.** 동일하게 별도 스펙. 8절 참조.
- **서버측 레이트 리밋·프롬프트 인젝션 방어.** 이 저장소 범위 밖이다. 8절 참조.
- **런타임 서버 전환 API.** 게임 실행 중 블루프린트로 `Connect(Host, Port)`를 호출하는 기능은 만들지 않는다. 설정은 `Initialize()`에서 1회 읽고 확정된다.
- **최종 사용자 설정 노출.** 플레이어가 자기 PC에서 서버를 바꾸는 경로는 만들지 않는다.
- **설정 저장 즉시 재연결.** 에디터에서 값을 바꾸면 **다음 PIE/게임 시작부터** 적용된다.
- **`FHermesFrameCodec::MaxBodySize`(1 MiB) 설정화.** 4.3 참조.
- **환경별 DataAsset 프로필.** 커맨드라인 오버라이드가 같은 문제를 더 단순하게 해결한다.
- **아이템 카탈로그 검증.** `item_id`가 게임에 실제 존재하는지는 게임별 데이터에 의존하므로, 형식 검증(빈 문자열·길이)까지만 하고 카탈로그 대조는 확장 지점으로 남긴다.

## 3. 접근법 결정

`UDeveloperSettings` 파생 클래스 하나(`UHermesSettings`)를 두고, 그 위에 커맨드라인 오버라이드 레이어를 얹는다.

검토했으나 채택하지 않은 대안:

- **`UObject(config=Game)` + `GConfig` 수동 읽기** — 의존성이 없어 가볍지만 에디터 설정 UI가 생기지 않는다. 도입 개발자가 ini 키 이름을 문서에서 찾아 손으로 써야 하므로 목표에 어긋난다.
- **`UDataAsset` 프로필 + 세팅에서 활성 프로필 선택** — 환경 전환이 드롭다운 1회로 끝나지만 설정 계층이 2단이 되어 값의 출처 추적이 어려워진다. 환경 전환은 커맨드라인으로 이미 해결되므로 불필요하다.

## 4. 설계

### 4.1 값의 우선순위

낮은 순서부터 높은 순서로 덮어쓴다.

1. **플러그인 기본값** — `UHermesSettings` 프로퍼티 초기화자. 중립값 `127.0.0.1:8770`.
2. **프로젝트 ini** — `Config/DefaultGame.ini`. 에디터 Project Settings UI가 이 파일에 쓴다.
3. **커맨드라인** — `-HermesHost=` / `-HermesPort=`. Host와 Port에만 적용되며, Shipping 빌드에서는 비활성화된다 (4.9).

커맨드라인 오버라이드를 Host/Port로 한정하는 이유: 이 계층의 목적은 "같은 빌드를 다른 서버에 붙이는 것"이다. 백오프나 추적 거리를 실행할 때마다 바꿔야 할 시나리오는 없다.

### 4.2 설정 클래스

신규 파일: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.h` / `.cpp`

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

    /** 액션 요청 응답 대기 한계(초). 초과 시 timeout 결과를 서버로 회신한다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="120.0"))
    float ActionTimeoutSeconds = 15.f;

    /** 게임 스레드가 소비하기 전까지 쌓아둘 수 있는 최대 수신 프레임 수. 초과 시 연결을 끊는다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="16", ClampMax="65536"))
    int32 MaxInboundQueueSize = 1024;

    /** 한 틱에 처리할 최대 수신 프레임 수. 나머지는 다음 틱으로 미룬다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1", ClampMax="4096"))
    int32 MaxInboundFramesPerTick = 64;

    // ---- Gameplay ----

    /** 플레이어 UUID를 보관하는 SaveGame 슬롯 이름. */
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
     * InOutHost/InOutPort를 조건에 맞을 때만 덮어쓴다.
     */
    static void ApplyCommandLineOverrides(const TCHAR* CmdLine,
                                          FString& InOutHost, int32& InOutPort);
};
```

`ClampMin`/`ClampMax`는 에디터 UI 단에서 백오프 0초나 포트 999999 같은 값의 입력을 막는다.

`ApplyCommandLineOverrides()`를 `static`으로 분리하는 이유는 테스트가 실제 프로세스 커맨드라인에 의존하지 않게 하기 위함이다. 5절 신규 테스트는 이 함수를 대상으로 한다.

`GetResolvedEndpoint()` 규칙:

- `-HermesHost=` 가 있고 값이 비어있지 않으면 `OutHost`를 덮는다. 없거나 빈 문자열이면 ini 값을 유지한다.
- `-HermesPort=` 가 있고 정수로 파싱되며 1~65535 범위이면 `OutPort`를 덮는다. 파싱 실패나 범위 밖이면 ini 값을 유지한다.
- Host/Port는 서로 독립적으로 판정한다. 한쪽만 지정해도 다른 쪽은 영향받지 않는다.

### 4.3 `MaxBodySize`를 제외하는 근거

`FHermesFrameCodec::MaxBodySize`(1 MiB)는 설정으로 열지 않는다.

- 이 값은 서버와 합의된 **프로토콜 불변식**이다 (`ue5-socket-protocol.md` §1). 클라이언트만 올려도 서버가 1 MiB에서 끊으면 효과가 없고, 원인 파악이 어려운 장애가 된다.
- `FHermesFrameCodec` / `FFrameAccumulator`는 엔진 게임플레이 타입에 의존하지 않는 순수 로직으로 설계되어 `HermesFrameCodec.spec.cpp`가 단독 검증한다. 여기에 `UHermesSettings` 참조를 넣으면 그 격리가 깨진다.

컴파일 타임 안전 상한으로 유지한다.

### 4.4 DNS 해석

`HermesSocketWorker.cpp:47`의 IPv4 전용 파싱을 주소 해석으로 교체한다.

변경 전:

```cpp
FIPv4Address Addr;
if (!FIPv4Address::Parse(Host, Addr)) return false;
TSharedRef<FInternetAddr> InetAddr = SS->CreateInternetAddr();
InetAddr->SetIp(Addr.Value);
InetAddr->SetPort(Port);
```

변경 후:

```cpp
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

**IPv4 우선 정책의 이유:** 듀얼스택 환경에서 OS가 IPv6를 먼저 반환하는 경우가 있는데, 현재 서버가 IPv4만 수신하면 접속에 실패한다. IPv4를 먼저 고르면 기존 사내 환경의 동작이 바뀌지 않으면서, IPv6 전용 엔드포인트도 폴백으로 지원된다.

**블로킹 특성:** `GetAddressInfo()`는 동기 호출이며 도달 불가 호스트명에서 OS DNS 타임아웃(수 초)까지 멈춘다. 이 코드는 `FHermesSocketWorker::Run()` 루프, 즉 전용 백그라운드 스레드에서만 실행되므로 게임 스레드는 영향받지 않는다. 실패는 기존 재연결 백오프 루프가 그대로 흡수한다. 별도 비동기화는 하지 않는다.

소켓 생성(`FTcpSocketBuilder`)은 선택된 주소의 프로토콜 타입을 따르도록 한다.

### 4.5 소비 지점 변경

| 파일 | 현재 | 변경 후 |
|---|---|---|
| `Connection/HermesConnectionSubsystem.cpp:13-15` | `static const` 3개 | 삭제. `GetDefault<UHermesSettings>()`로 읽어 Worker 생성자에 주입. `SaveSlot`은 `SaveSlotName` 사용 |
| `Transport/HermesSocketWorker.cpp:147-148` | 지역 상수 `0.5f`/`30.f` | 생성자 인자로 주입받아 멤버로 보관 |
| `Actions/HermesActionDispatcher.cpp:64` | 리터럴 `15.f` | `GetDefault<UHermesSettings>()->ActionTimeoutSeconds` |
| `NPC/HermesNPCCharacter.cpp:66` | 리터럴 `150.f` | `GetDefault<UHermesSettings>()->FollowDistance` |

**`FHermesSocketWorker`가 설정을 직접 읽지 않고 주입받는 이유:** 워커는 전용 스레드에서 실행되는데 `UHermesSettings`는 `UObject`다. 워커 스레드에서 UObject를 만지는 것을 피하기 위해, 값은 `UHermesConnectionSubsystem::Initialize()`(게임 스레드) 시점에 읽어 평범한 값 타입으로 복사해 넘긴다. "다음 PIE부터 적용" 정책과도 자연히 일치한다.

`UHermesActionDispatcher`와 `AHermesNPCCharacter`는 게임 스레드에서만 동작하므로 `GetDefault<>` 직접 읽기가 안전하다.

`FHermesSocketWorker` 생성자 시그니처:

```cpp
struct FHermesWorkerConfig
{
    FString Host;
    int32   Port                  = 8770;
    float   InitialReconnectDelay = 0.5f;
    float   MaxReconnectDelay     = 30.f;
    int32   MaxInboundQueueSize   = 1024;
};

explicit FHermesSocketWorker(const FHermesWorkerConfig& InConfig);
```

인자가 5개로 늘어나므로 위치 인자 나열 대신 설정 구조체를 받는다. 호출부에서 어떤 값이 무엇인지 읽히고, 이후 항목이 추가돼도 시그니처가 흔들리지 않는다.

### 4.6 빌드 설정

`HermesAgentNPC.Build.cs`의 `PublicDependencyModuleNames`에 `"DeveloperSettings"` 추가. 런타임 모듈이므로 패키징에 문제없다.

### 4.7 프로젝트 ini

`Config/DefaultGame.ini` 신규 생성:

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=192.168.0.111
Port=8770
```

`Config/` 디렉터리는 현재 git 미추적 상태이나 `.gitignore` 대상은 아니다. `DefaultEngine.ini`, `DefaultInput.ini`와 함께 이번에 git에 추가한다.

이로써 사내 서버 주소는 **플러그인이 아니라 이 프로젝트의 설정**으로 이동한다.

### 4.8 백오프 sleep 중단 응답성 (1.2-b 대응)

`FHermesSocketWorker::Run()`의 통짜 `Sleep`을 조각 sleep으로 바꾼다.

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

`Run()`의 백오프 대기를 이 함수로 교체하고, 대기 직후 `if (bStopRequested) break;` 를 둔다. 이로써 `MaxReconnectDelay`가 300초여도 종료 응답 지연은 **100ms 이내**로 고정된다.

`ConnectSocket()` 안의 `GetAddressInfo()` DNS 블로킹은 이 방식으로 줄일 수 없다 (OS 호출이라 중간에 깰 수 없음). 다만 그 상한은 OS 기본 DNS 타임아웃(통상 수 초)이고 설정으로 늘어나지 않으므로, 본 설계가 악화시키지 않는다. 허용 가능한 잔여 지연으로 두고 8절 후속 과제에 기록한다.

### 4.9 Shipping 빌드 오버라이드 차단 (1.2-d 대응)

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

**이것이 방어의 전부가 아님을 명시한다.** 서버 주소는 클라이언트가 알아야만 접속할 수 있으므로 패킷 캡처로 언제든 드러나고, 바이너리를 소유한 사용자는 ini를 직접 고칠 수도 있다. 주소 은닉은 obscurity이지 보안이 아니다. **접근 통제의 본체는 서버측 인증(8절)이다.** 이 조치는 "기본 경로를 막는 것"까지가 역할이다.

테스트가 이 분기에 막히지 않도록, 5절 테스트는 비-Shipping 구성에서만 수행한다 (자동화 테스트 자체가 Shipping에 포함되지 않으므로 추가 조치는 불필요).

### 4.10 인바운드 큐 상한과 틱 예산 (1.2-a 대응)

**큐 상한 (워커 스레드 측)**

`TQueue`는 크기 조회를 제공하지 않으므로 `FThreadSafeCounter InboundCount`를 함께 둔다.

- `ReceiveAvailable()`이 `Inbound.Enqueue()` 할 때마다 `Increment()`
- `DequeueInbound()` 성공 시 `Decrement()`
- `InboundCount`가 `Config.MaxInboundQueueSize`를 넘으면 `ReceiveAvailable()`이 `false`를 반환한다 → 기존 프레이밍 위반과 **동일 경로**로 `CloseSocket()` 후 백오프 재연결

기존 에러 처리 경로를 재사용하므로 새 상태가 늘지 않는다. 정상 서버라면 게임 스레드가 매 틱 비우므로 1024에 도달할 일이 없고, 도달했다는 것은 피어가 소비 속도를 무시하고 밀어넣고 있다는 뜻이다.

**틱 예산 (게임 스레드 측)**

`UHermesConnectionSubsystem::Tick()`의 소비 루프를 무제한에서 예산제로 바꾼다.

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

남은 프레임은 다음 틱에서 처리된다. 유입이 예산을 계속 초과하면 큐가 차오르고, 결국 위의 큐 상한이 연결을 끊는다. 두 장치가 함께 동작해 **어느 경우에도 한 틱이 무한정 길어지지 않는다.**

### 4.11 액션 파라미터 하드 바운드 (1.2-c 대응)

검증 헬퍼를 `Actions/HermesActionParams.h` / `.cpp`에 모아 각 핸들러가 호출한다. 검증 로직을 핸들러 밖에 두는 이유는 규칙을 한 곳에서 읽을 수 있게 하고, 엔진 타입에 의존하지 않는 부분을 단독 테스트하기 위함이다.

```cpp
namespace HermesParams
{
    /** 유한하고 |v| <= Limit 인지 검사한다. NaN/Inf는 거부. */
    bool IsValidCoordinate(double V, float Limit);

    /** 유한한 정수이고 1 <= V <= Max 인지 검사한다. int32 캐스트 전에 호출한다. */
    bool IsValidQuantity(double V, int32 Max, int32& OutQty);

    /** 비어있지 않고 길이가 MaxLen 이하인지 검사한다. */
    bool IsValidItemId(const FString& Id, int32 MaxLen);
}
```

**`MoveToActionHandler`** — 좌표 3축 각각에 `IsValidCoordinate(V, MaxWorldCoordinate)`를 적용하고, 실패 시 `ok=false, error="coordinate out of range"`로 회신한다. `FVector` 생성 전에 검사하므로 non-finite 벡터가 엔진에 들어가지 않는다.

**`ItemTransferActionHandler`** — `IsValidQuantity(Qd, MaxItemQuantity, Qty)`로 **`(int32)` 캐스트 이전에** 범위를 확정한다. 미정의 동작 경로가 사라진다. `item_id`에는 `IsValidItemId(ItemId, MaxItemIdLength)`를 적용한다. 실패 시 각각 `"quantity out of range"`, `"invalid item_id"`.

**`UHermesInventoryComponent::Add()`** — 핸들러 검증과 별개로 누적 오버플로를 자체 차단한다. 이 컴포넌트는 액션 경로 외에서도 호출될 수 있으므로 방어를 겹쳐 둔다.

```cpp
if (UHermesItem* It = Find(ItemId))
{
    // int32 오버플로 방지: 상한에서 포화시킨다
    It->Quantity = (It->Quantity > MAX_int32 - Qty) ? MAX_int32 : It->Quantity + Qty;
    return;
}
```

`follow_player`와 `inventory_manage` 핸들러는 코드 검토 결과 상한 문제가 없어 변경하지 않는다.

## 5. 테스트

### 신규 자동화 테스트

**`Settings/HermesSettings.spec.cpp`** — 커맨드라인 오버라이드 계층. 대상은 `UHermesSettings::ApplyCommandLineOverrides(const TCHAR*, FString&, int32&)`이며, 실제 프로세스 커맨드라인이 아니라 테스트가 만든 문자열을 넘겨 검증한다.

- 오버라이드 인자 없음 → ini/기본값이 그대로 반환된다
- `-HermesHost=` 만 지정 → Host만 덮이고 Port는 유지된다
- `-HermesPort=` 만 지정 → Port만 덮이고 Host는 유지된다
- `-HermesPort=abc` (정수 파싱 실패) → 기존 Port가 유지된다
- `-HermesPort=0`, `-HermesPort=70000` (범위 밖) → 기존 Port가 유지된다
- `-HermesHost=` (빈 값) → 기존 Host가 유지된다

**`Actions/HermesActionParams.spec.cpp`** — 파라미터 검증 헬퍼. 엔진 의존이 없어 단독 검증이 가능하다.

- `IsValidCoordinate`: 정상값 통과 / `NaN`·`+Inf`·`-Inf` 거부 / 한계값 경계(`Limit`, `Limit + 1`) 판정 / 음수 방향 대칭 확인
- `IsValidQuantity`: `1`과 `Max` 통과 / `0`·`-1` 거부 / `Max + 1` 거부 / `1e18`(int32 범위 초과) 거부 / 소수(`1.5`) 거부 / `NaN` 거부
- `IsValidItemId`: 정상 통과 / 빈 문자열 거부 / `MaxLen` 통과, `MaxLen + 1` 거부

**`Inventory/HermesInventory.spec.cpp` 확장** — 기존 파일에 케이스 추가.

- `Add(id, MAX_int32)` 후 다시 `Add(id, 1)` → 수량이 `MAX_int32`에서 포화하고 **음수가 되지 않는다**

**큐 상한(4.10)은 자동화 테스트를 만들지 않는다.** `FHermesSocketWorker`는 실제 `FSocket`과 전용 스레드에 묶여 있어, 단위 테스트로 검증하려면 소켓을 인터페이스로 추상화하는 리팩터링이 선행되어야 한다. 그 리팩터링은 이 작업의 목적과 무관하게 범위를 크게 키운다. 대신 아래 수동 검증에서 실제 폭주 시나리오로 확인한다.

### 회귀

기존 자동화 테스트 5종이 그대로 통과해야 한다.

```
UnrealEditor-Cmd.exe HermesAgentNPC.uproject -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

### 수동 검증 (통합 테스트 체크리스트에 추가)

설정:

- 에디터 Project Settings > Plugins > Hermes Agent NPC 화면에 항목이 보이고, 값 변경 시 `Config/DefaultGame.ini`에 기록된다
- ini에 `Host=192.168.0.111`을 두고 PIE 실행 → 기존과 동일하게 접속·대화·액션이 동작한다
- `Host=localhost` 로 바꿔 실행 → 호스트명이 해석된다 (DNS 경로 확인)
- `-HermesHost=<다른IP>` 인자로 실행 → ini 값을 무시하고 해당 IP로 접속을 시도한다

강건성:

- **서버를 끈 상태로 PIE 실행 후 즉시 정지** → 에디터가 멈추지 않고 즉시 정지된다 (4.8 검증). `MaxReconnectDelay=300`으로 올려두고 백오프가 충분히 커진 뒤에도 재확인한다
- 도달 불가 호스트명 지정 → 게임 스레드가 멈추지 않고 백오프 재연결이 계속 시도된다
- 테스트용 스크립트로 프레임을 대량 전송 → 메모리가 무한 증가하지 않고, 상한 초과 시 연결이 끊긴 뒤 재연결된다 (4.10 검증)
- `move_to`에 `x: 1e308` 전송 → `ok=false, error="coordinate out of range"`가 회신되고 크래시하지 않는다
- `item_transfer`에 `quantity: 2e9` 전송 → `ok=false`가 회신되고 인벤토리 수량이 음수가 되지 않는다

## 6. 문서 갱신

- `README.md` — "빠른 시작" 뒤에 서버 설정 절 추가. Project Settings 경로, ini 예시, 커맨드라인 인자 설명, Shipping 빌드에서 오버라이드가 비활성화된다는 점 명시.
- `HermesAgentNPC_Documentation.html` — 하드코딩 주소 서술을 설정 기반으로 교체.
- `ue5-socket-protocol.md` — **프로토콜 변경은 없다.** 다만 §7 클라이언트 체크리스트의 `192.168.0.111:8770` 고정 표현을 "설정된 엔드포인트"로 바꾸고, §6 액션 카탈로그에 클라이언트가 적용하는 파라미터 범위 검증을 주석으로 덧붙인다. 서버가 범위 밖 값을 보내면 `ok=false`로 회신된다는 사실은 서버 구현자가 알아야 한다.

두 문서 모두 `192.168.0.111`을 플러그인 사양처럼 기술한 부분이 있다. 하드코딩 제거가 이 작업의 목적이므로 문서에서도 걷어낸다.

## 7. 완료 조건

- [ ] 플러그인 소스 전체에서 `192.168.0.111` 문자열이 검색되지 않는다
- [ ] 4.5 표의 4개 소비 지점이 모두 설정을 참조한다
- [ ] `Config/DefaultGame.ini`가 git에 추가되고 사내 주소를 담고 있다
- [ ] 서버가 꺼진 상태에서 PIE 정지가 즉시 완료된다 (백오프가 최대치에 도달한 뒤에도)
- [ ] Shipping 구성에서 `-HermesHost=`가 무시된다
- [ ] 인바운드 큐가 상한에서 연결을 끊고 재연결하며, 한 틱 처리량이 예산으로 제한된다
- [ ] `move_to` 비정상 좌표와 `item_transfer` 과대 수량이 `ok=false`로 거부된다
- [ ] 신규 spec 테스트(`HermesSettings`, `HermesActionParams`)와 확장된 인벤토리 테스트가 통과한다
- [ ] 기존 자동화 테스트 5종이 통과한다
- [ ] 5절 수동 검증 항목이 확인된다
- [ ] README, HTML 문서, 프로토콜 문서 §7이 갱신된다

## 8. 후속 과제 (별도 스펙)

본 설계에서 의도적으로 제외한 항목과 그 근거를 기록한다. 서버를 새로 구축할 예정이므로 프로토콜 v2 설계가 다음 사이클의 출발점이 된다.

### 8.1 프로토콜 v2 — 인증

**문제.** 현재 identify는 `player_id`(UUID)만 보내면 즉시 그 플레이어로 인정된다 (`ue5-socket-protocol.md` §4.1, `HermesMessages.cpp:22-32`). UUID는 `UHermesSaveGame`(`HermesSaveGame.h:11-12`)에 평문으로 저장되고 `.sav`는 암호화되지 않아 추출이 쉽다. 프로토콜 §5의 `not_authorized`는 허용 목록 방식이며 "if enabled" 선택 사항이다.

**한계 인식.** 인증 강제는 서버만 할 수 있다. 클라이언트에 심은 비밀은 패키징된 바이너리 안에 있으므로 기기 소유자에게는 결국 노출된다. 따라서 목표는 "기기 소유자 차단"이 아니라 **"제3자가 남의 세션을 가로채는 것 차단"** 으로 설정해야 한다. 서버 발급 토큰과 세션 바인딩이 현실적인 방향이다.

**의존.** 프로토콜 문서 v2 확정 → 클라이언트·서버 각자 구현. 토큰 값은 본 설계의 `UHermesSettings`에 프로퍼티를 추가하는 형태로 자연히 확장된다.

### 8.2 TLS 전송

평문 TCP(`HermesSocketWorker.cpp:56`)라 인터넷 구간에서 대화 내용과 액션 명령이 노출된다. LAN 프로토타입에서는 유예 가능하나 외부 배포 시 필수다. `FSslSocket`/`SslModule` 경유 또는 WebSocket(wss) 전환이 필요하며 서버가 함께 바뀌어야 한다. 8.1 이후로 둔다.

### 8.3 서버측 과제 (이 저장소 밖)

- **레이트 리밋** — 연결 수·메시지 빈도 제한이 없으면 llama-server(GPU 자원)까지 과부하가 전파된다. 본 설계의 클라이언트측 큐 상한은 **클라이언트를 보호할 뿐 서버를 보호하지 않는다.**
- **프롬프트 인젝션** — 플레이어 발화에 심긴 지시문으로 LLM이 의도치 않은 액션을 호출할 수 있다. 화이트리스트는 "무엇을 실행할 수 있는가"만 막고 "언제 실행하는가"는 LLM 판단에 남는다. 클라이언트가 할 수 있는 완화는 4.11의 파라미터 하드 바운드와 액션 레이트 리밋이 전부다.

### 8.4 DNS 해석 블로킹 잔여 지연

4.8의 조각 sleep으로도 `GetAddressInfo()` 자체는 중간에 깰 수 없다. 종료 응답이 OS DNS 타임아웃(통상 수 초)만큼 지연될 수 있다. 본 설계가 악화시키지 않으므로 유예하되, 문제가 되면 해석 결과 캐싱이나 비동기 해석으로 처리한다.

### 8.5 서버 재구축 시 모듈 경계

서버를 새로 지을 때 다음 네 축을 분리하면 이후 교체·확장이 쉬워진다.

- **전송/세션** — 소켓 수락, 프레이밍, `player_id` ↔ 연결 바인딩, 재접속 시 세션 복원
- **대화 저장소** — `chat_id`별 히스토리. 분리하면 서버 재시작에도 맥락이 유지되고 인스턴스 확장이 가능해진다
- **LLM 백엔드** — llama-server 호출을 인터페이스로 감싸면 llama.cpp ↔ vLLM ↔ 클라우드 API 교체가 설정 변경으로 끝난다
- **액션 카탈로그** — 화이트리스트 명령 정의와 LLM 툴 스키마를 한 곳에서 선언하면, 프로토콜 문서 §6·서버 툴 정의·UE 핸들러 세 곳을 손으로 맞추다 어긋나는 문제(1.2-c의 원인)를 구조적으로 줄일 수 있다

클라이언트는 `ue5-socket-protocol.md`가 자기완결적 계약 역할을 하므로 서버 내부 구조에 영향받지 않는다.
