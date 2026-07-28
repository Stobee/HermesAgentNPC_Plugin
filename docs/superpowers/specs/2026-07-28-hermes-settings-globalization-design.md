# Hermes Agent NPC — 서버 설정 전역화 설계

- 작성일: 2026-07-28
- 상태: 승인 대기
- 대상 브랜치: master

## 1. 문제

플러그인 내부에 백엔드 서버 주소가 하드코딩되어 있다.

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

## 2. 목표와 비목표

### 목표

- 플러그인 소스에서 `192.168.0.111`을 완전히 제거한다.
- 플러그인 도입 개발자가 **UE 에디터 Project Settings 화면**에서 서버 주소를 지정할 수 있다.
- 빌드/배포 담당자가 **재컴파일 없이** ini 수정 또는 커맨드라인 인자로 서버를 전환할 수 있다.
- Host에 **호스트명(도메인)** 을 쓸 수 있다. 컨테이너명·k8s 서비스명·클라우드 엔드포인트 대응.
- 위 표의 상수 4종도 같은 설정 체계로 흡수한다.

### 비목표 (명시적 제외)

- **런타임 서버 전환 API.** 게임 실행 중 블루프린트로 `Connect(Host, Port)`를 호출하는 기능은 만들지 않는다. 설정은 `Initialize()`에서 1회 읽고 확정된다.
- **최종 사용자 설정 노출.** 플레이어가 자기 PC에서 서버를 바꾸는 경로는 만들지 않는다.
- **설정 저장 즉시 재연결.** 에디터에서 값을 바꾸면 **다음 PIE/게임 시작부터** 적용된다. `PostEditChangeProperty` 훅으로 워커 스레드를 재시작하지 않는다.
- **`FHermesFrameCodec::MaxBodySize`(1 MiB) 설정화.** 아래 4.3 참조.
- **환경별 DataAsset 프로필.** 커맨드라인 오버라이드가 같은 문제를 더 단순하게 해결한다.

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
3. **커맨드라인** — `-HermesHost=` / `-HermesPort=`. Host와 Port에만 적용된다.

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

    /** Hermes 백엔드 호스트명 또는 IP 주소. */
    UPROPERTY(EditAnywhere, config, Category="Connection")
    FString Host = TEXT("127.0.0.1");

    UPROPERTY(EditAnywhere, config, Category="Connection", meta=(ClampMin="1", ClampMax="65535"))
    int32 Port = 8770;

    /** 재연결 첫 대기 시간(초). 실패할 때마다 2배씩 증가한다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="0.05", ClampMax="10.0"))
    float InitialReconnectDelay = 0.5f;

    /** 재연결 대기 시간 상한(초). */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="300.0"))
    float MaxReconnectDelay = 30.f;

    /** 액션 요청 응답 대기 한계(초). 초과 시 timeout 결과를 서버로 회신한다. */
    UPROPERTY(EditAnywhere, config, Category="Connection|Tuning", meta=(ClampMin="1.0", ClampMax="120.0"))
    float ActionTimeoutSeconds = 15.f;

    /** 플레이어 UUID를 보관하는 SaveGame 슬롯 이름. */
    UPROPERTY(EditAnywhere, config, Category="Gameplay")
    FString SaveSlotName = TEXT("HermesPlayer");

    /** follow_player 액션에서 NPC가 유지하는 거리(cm). */
    UPROPERTY(EditAnywhere, config, Category="Gameplay", meta=(ClampMin="50.0"))
    float FollowDistance = 150.f;

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

`ApplyCommandLineOverrides()`를 `static`으로 분리하는 이유는 테스트가 실제 프로세스 커맨드라인에 의존하지 않게 하기 위함이다. 5절 신규 테스트는 이 함수를 대상으로 한다.

`ClampMin`/`ClampMax`는 에디터 UI 단에서 백오프 0초나 포트 999999 같은 값의 입력을 막는다.

`GetResolvedEndpoint()` 규칙:

- `-HermesHost=` 가 있고 값이 비어있지 않으면 `OutHost`를 덮는다. 없거나 빈 문자열이면 ini 값을 유지한다.
- `-HermesPort=` 가 있고 정수로 파싱되며 1~65535 범위이면 `OutPort`를 덮는다. 파싱 실패나 범위 밖이면 ini 값을 유지한다.
- Host/Port는 서로 독립적으로 판정한다. 한쪽만 지정해도 다른 쪽은 영향받지 않는다.

### 4.3 `MaxBodySize`를 제외하는 근거

`FHermesFrameCodec::MaxBodySize`(1 MiB)는 설정으로 열지 않는다.

- 이 값은 서버와 합의된 **프로토콜 불변식**이다. 클라이언트만 올려도 서버가 1 MiB에서 끊으면 효과가 없고, 원인 파악이 어려운 장애가 된다.
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
FHermesSocketWorker(const FString& InHost, int32 InPort,
                    float InInitialReconnectDelay, float InMaxReconnectDelay);
```

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

## 5. 테스트

### 신규 자동화 테스트

`Settings/HermesSettings.spec.cpp` — `GetResolvedEndpoint()`의 오버라이드 계층이 이번에 새로 생기는 유일한 분기 로직이다.

- 오버라이드 인자 없음 → ini/기본값이 그대로 반환된다
- `-HermesHost=` 만 지정 → Host만 덮이고 Port는 유지된다
- `-HermesPort=` 만 지정 → Port만 덮이고 Host는 유지된다
- `-HermesPort=abc` (정수 파싱 실패) → 기존 Port가 유지된다
- `-HermesPort=0`, `-HermesPort=70000` (범위 밖) → 기존 Port가 유지된다
- `-HermesHost=` (빈 값) → 기존 Host가 유지된다

테스트 대상은 4.2에 정의한 `UHermesSettings::ApplyCommandLineOverrides(const TCHAR*, FString&, int32&)`이다. 실제 프로세스 커맨드라인이 아니라 테스트가 만든 문자열을 넘겨 검증한다.

### 회귀

기존 자동화 테스트 5종이 그대로 통과해야 한다.

```
UnrealEditor-Cmd.exe HermesAgentNPC.uproject -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

### 수동 검증 (통합 테스트 체크리스트에 추가)

- 에디터 Project Settings > Plugins > Hermes Agent NPC 화면에 항목이 보이고, 값 변경 시 `Config/DefaultGame.ini`에 기록된다
- ini에 `Host=192.168.0.111`을 두고 PIE 실행 → 기존과 동일하게 접속·대화·액션이 동작한다
- `Host=localhost` 로 바꿔 실행 → 호스트명이 해석된다 (DNS 경로 확인)
- `-HermesHost=<다른IP>` 인자로 실행 → ini 값을 무시하고 해당 IP로 접속을 시도한다
- 도달 불가 호스트명 지정 → 게임 스레드가 멈추지 않고, 백오프 재연결이 계속 시도된다

## 6. 문서 갱신

- `README.md` — "빠른 시작" 뒤에 서버 설정 절 추가. Project Settings 경로, ini 예시, 커맨드라인 인자 설명.
- `HermesAgentNPC_Documentation.html` — 하드코딩 주소 서술을 설정 기반으로 교체.

두 문서 모두 `192.168.0.111`을 플러그인 사양처럼 기술한 부분이 있다. 하드코딩 제거가 이 작업의 목적이므로 문서에서도 걷어낸다.

## 7. 완료 조건

- [ ] 플러그인 소스 전체에서 `192.168.0.111` 문자열이 검색되지 않는다
- [ ] 위 4.5 표의 4개 소비 지점이 모두 설정을 참조한다
- [ ] `Config/DefaultGame.ini`가 git에 추가되고 사내 주소를 담고 있다
- [ ] `HermesSettings.spec.cpp` 신규 테스트가 통과한다
- [ ] 기존 자동화 테스트 5종이 통과한다
- [ ] 5절 수동 검증 항목이 확인된다
- [ ] README와 HTML 문서가 갱신된다
