# Hermes Agent NPC — 설정 전역화 · 강건성 · 프로토콜 v2 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 하드코딩된 서버 주소를 프로젝트 설정으로 옮기고, 신뢰할 수 없는 피어에 대한 입력 강건성을 확보하며, 프로토콜을 v2(서버 발급 신원 · 스트리밍 · 연결 유지 · TLS)로 개정한다.

**Architecture:** `UDeveloperSettings` 파생 클래스 하나가 모든 설정의 단일 출처가 되고, 그 위에 커맨드라인 오버라이드 계층을 얹는다. 워커 스레드는 `UObject`를 만지지 않도록 게임 스레드에서 값을 복사받는다. TLS는 `IHermesTransport` 인터페이스 뒤에 숨겨 기존 프레이밍·큐·백오프 로직을 건드리지 않는다. 검증·판정 로직은 전부 시간/입력을 주입받는 순수 함수로 분리해 자동화 테스트로 덮는다.

**Tech Stack:** UE 5.8, C++17, `DeveloperSettings` / `Sockets` / `Networking` / `SSL`(OpenSSL) 모듈, UE Automation Test.

**설계 문서:** `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md`

## Global Constraints

- **모듈 루트가 인클루드 경로다.** 헤더는 항상 `"Subdir/Header.h"` 형태로 include한다 (`HermesAgentNPC.Build.cs`의 `PublicIncludePaths.Add(ModuleDirectory)`).
- **테스트 매크로:** `IMPLEMENT_SIMPLE_AUTOMATION_TEST(F<Name>Test, "Hermes.<Category>.<Behavior>", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)`. 본문은 `bool F<Name>Test::RunTest(const FString& Parameters)`이고 마지막에 `return true;`.
- **테스트 파일명:** 대상 코드와 같은 폴더에 `<Name>.spec.cpp`.
- **주석은 한국어로 쓴다.** 기존 소스 전체가 한국어 주석이다.
- **워커 스레드에서 `UObject`를 접근하지 않는다.** 설정은 게임 스레드에서 읽어 값 타입으로 복사해 넘긴다.
- **`FHermesFrameCodec::MaxBodySize`(1 MiB)는 설정으로 열지 않는다.** 프로토콜 불변식이므로 컴파일 타임 상수로 유지한다.
- **빌드 명령:**
  ```powershell
  & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
  ```
- **전체 테스트 명령:**
  ```powershell
  & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
  ```
  단일 테스트는 `Hermes` 대신 전체 테스트명을 넣는다 (예: `Hermes.Settings.CommandLineOverride`).
- **기존 자동화 테스트 5종이 매 단계 통과해야 한다.** Task 21에서 `identify` 형식이 바뀌며 `HermesMessages.spec.cpp` 기대값이 갱신되는 것만 예외이고, 그 외 기존 테스트를 깨는 변경은 실수다.

---

## File Structure

### 신규 파일

| 경로 (`Plugins/HermesAgentNPC/Source/HermesAgentNPC/` 기준) | 책임 |
|---|---|
| `Settings/HermesSettings.h` / `.cpp` | 모든 설정의 단일 출처. 커맨드라인 오버라이드 해석 |
| `Settings/HermesSettings.spec.cpp` | 오버라이드 계층 테스트 |
| `Actions/HermesActionParams.h` / `.cpp` | 액션 파라미터 범위·유한성 검증 (순수) |
| `Actions/HermesActionParams.spec.cpp` | 위 테스트 |
| `Actions/HermesRateLimiter.h` / `.cpp` | 토큰 버킷 (순수, 시간 주입) |
| `Actions/HermesRateLimiter.spec.cpp` | 위 테스트 |
| `Connection/HermesUtil.h` / `.cpp` | 경계 있는 배열 push (순수) |
| `Connection/HermesUtil.spec.cpp` | 위 테스트 |
| `Connection/HermesLiveness.h` / `.cpp` | ping/사망 판정 결정 로직 (순수, 시간 주입) |
| `Connection/HermesLiveness.spec.cpp` | 위 테스트 |
| `Connection/HermesPendingChats.h` / `.cpp` | 진행 중 발화 추적 (순수, 시간 주입) |
| `Connection/HermesPendingChats.spec.cpp` | 위 테스트 |
| `Transport/HermesWorkerConfig.h` | 워커·전송에 넘기는 값 타입 설정 구조체 |
| `Transport/IHermesTransport.h` | 바이트 스트림 전송 인터페이스 |
| `Transport/HermesPlainTransport.h` / `.cpp` | `FSocket` 기반 평문 전송 |
| `Transport/HermesTlsPolicy.h` / `.cpp` | TLS 검증 이름·모드 결정 (순수) |
| `Transport/HermesTlsPolicy.spec.cpp` | 위 테스트 |
| `Transport/HermesTlsTransport.h` / `.cpp` | OpenSSL 기반 TLS 전송 |
| `Config/DefaultGame.ini` (프로젝트 루트) | 이 프로젝트의 서버 주소·TLS 설정 |

### 수정 파일

| 경로 | 변경 성격 |
|---|---|
| `HermesAgentNPC.Build.cs` | `DeveloperSettings`, `SSL`, OpenSSL 의존 추가 |
| `Transport/HermesSocketWorker.h` / `.cpp` | 설정 주입, 전송 추상화 사용, 큐 상한, 중단 가능 sleep |
| `Connection/HermesConnectionSubsystem.h` / `.cpp` | 설정 소비, 자격 증명, 스트리밍, liveness, 발화 추적 |
| `HermesSaveGame.h` | `SessionToken` 필드 |
| `Protocol/HermesMessages.h` / `.cpp` | v2 identify, `chat_delta`, `ping` 빌더, `identified` 파서 |
| `Protocol/HermesMessages.spec.cpp` | v2 기대값 |
| `Actions/HermesActionDispatcher.h` / `.cpp` | 설정 기반 타임아웃, 레이트 리밋, 타이머 회수 |
| `Actions/MoveToActionHandler.cpp` | 좌표 검증 |
| `Actions/ItemTransferActionHandler.cpp` | 수량·item_id 검증 |
| `Inventory/HermesInventoryComponent.cpp` | 누적 오버플로 포화 |
| `Inventory/HermesInventory.spec.cpp` | 포화 테스트 |
| `NPC/HermesNPCCharacter.cpp` | 추적 거리 설정화 |
| `UI/HermesDialogueWidget.h` / `.cpp` | 델타 누적, id 상관, 실패 표시 |
| `ue5-socket-protocol.md`, `README.md`, `HermesAgentNPC_Documentation.html` | v2 문서화 |

---

## 설계 문서 커버리지

설계 문서의 각 절이 어느 Task로 구현되는지의 대응표. 스펙을 고칠 때 이 표로 영향받는 Task를 찾는다.

| 스펙 절 | Task | 비고 |
|---|---|---|
| 4.1 값의 우선순위 | 1 | `GetResolvedEndpoint` |
| 4.2 설정 클래스 | 1 | 프로퍼티 전체를 한 번에 정의 |
| 4.3 `MaxBodySize` 제외 | — | 변경 없음. Global Constraints에 불변식으로 명시 |
| 4.4 DNS 해석 | 3 | IPv4 우선 |
| 4.5 소비 지점 변경 | 2 | 하드코딩 4곳 |
| 4.6 빌드 설정 | 1, 16 | `DeveloperSettings` / `SSL`+OpenSSL |
| 4.7 프로젝트 ini | 2, 19 | 2에서 임시값, 19에서 배포값 |
| 4.8 백오프 sleep | 4 | |
| 4.9 Shipping 오버라이드 차단 | 1 | `ApplyCommandLineOverrides` 내부 |
| 4.10 인바운드 상한·틱 예산 | 7 | |
| 4.11 파라미터 하드 바운드 | 5 | |
| 4.12 보류 발화 상한 | 8 | |
| 4.13 레이트 리밋·타이머 회수 | 6 | |
| 4.14 아웃바운드 상한 | 7 | |
| 5.1~5.5 신원 발급 | 9, 10 | 9는 메시지 계층, 10은 흐름 |
| 5.6 스트리밍 | 11 | |
| 5.7 연결 유지·사망 판정 | 12 | `RequestReconnect`는 10에서 선행 추가 |
| 5.8 대화 타임아웃·상관 | 13 | |
| §4.5/§4.10 action_event (프로토콜 개정분) | 13b | move_to 두 단계 응답 |
| 6.1 전송 추상화 | 14 | 순수 리팩터링 |
| 6.2 SSL 컨텍스트 | 16 | `CreateContext` |
| 6.3 연결 수립 절차 | 16 | `Connect` |
| 6.4 검증 정책 3종 | 15, 16 | 15는 결정, 16은 적용 |
| 6.5 다운그레이드 금지 | 15, 16 | `ResolveUseTls` + 4중 차단 |
| 6.6 워커 스레드 통합 | 16 | `DoHandshake` |
| 6.7 프로토콜 문서 변경 | 17 | |
| 6.8 남는 위험 명시 | 18 | README 보안 모델 표 |
| 7 테스트 | 전 Task + 19 | 신규 spec 파일 7종 |
| 8 문서 갱신 | 17, 18 | |
| 9 완료 조건 | 19 | Step 7에서 대조 |

---

# Phase 1 — 설정 전역화

## Task 1: `UHermesSettings` 클래스와 커맨드라인 오버라이드

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesAgentNPC.Build.cs`

**Interfaces:**
- Consumes: 없음 (첫 태스크)
- Produces:
  - `UHermesSettings` — `GetDefault<UHermesSettings>()`로 읽는 설정 CDO. 프로퍼티는 아래 헤더 참조
  - `void UHermesSettings::GetResolvedEndpoint(FString& OutHost, int32& OutPort) const`
  - `static void UHermesSettings::ApplyCommandLineOverrides(const TCHAR* CmdLine, FString& InOutHost, int32& InOutPort)`

- [ ] **Step 1: `Build.cs`에 `DeveloperSettings` 의존 추가**

`Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesAgentNPC.Build.cs`의 `PublicDependencyModuleNames` 배열 마지막 항목 `"UMG", "Slate", "SlateCore"` 뒤에 추가한다.

```csharp
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"Sockets", "Networking",
			"Json", "JsonUtilities",
			"AIModule", "NavigationSystem",
			"UMG", "Slate", "SlateCore",
			"DeveloperSettings"
		});
```

- [ ] **Step 2: 설정 헤더 작성**

`Settings/HermesSettings.h` 전체 내용:

```cpp
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
	static void ApplyCommandLineOverrides(const TCHAR* CmdLine,
	                                      FString& InOutHost, int32& InOutPort);
};
```

- [ ] **Step 3: 실패하는 테스트 작성**

`Settings/HermesSettings.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Settings/HermesSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesSettingsCommandLineTest,
	"Hermes.Settings.CommandLineOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesSettingsCommandLineTest::RunTest(const FString& Parameters)
{
	// 인자가 없으면 ini/기본값이 그대로 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-SomeOtherFlag"), Host, Port);
		TestEqual(TEXT("host unchanged"), Host, TEXT("ini.example.com"));
		TestEqual(TEXT("port unchanged"), Port, 8770);
	}

	// Host만 지정하면 Port는 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesHost=10.0.0.5"), Host, Port);
		TestEqual(TEXT("host overridden"), Host, TEXT("10.0.0.5"));
		TestEqual(TEXT("port still ini"), Port, 8770);
	}

	// Port만 지정하면 Host는 유지된다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=9999"), Host, Port);
		TestEqual(TEXT("host still ini"), Host, TEXT("ini.example.com"));
		TestEqual(TEXT("port overridden"), Port, 9999);
	}

	// 정수로 파싱되지 않는 포트는 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=abc"), Host, Port);
		TestEqual(TEXT("bad port ignored"), Port, 8770);
	}

	// 범위 밖 포트는 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=0"), Host, Port);
		TestEqual(TEXT("port 0 ignored"), Port, 8770);

		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesPort=70000"), Host, Port);
		TestEqual(TEXT("port 70000 ignored"), Port, 8770);
	}

	// 빈 Host 값은 무시한다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(TEXT("-HermesHost="), Host, Port);
		TestEqual(TEXT("empty host ignored"), Host, TEXT("ini.example.com"));
	}

	// 둘 다 지정하면 둘 다 덮인다.
	{
		FString Host = TEXT("ini.example.com");
		int32 Port = 8770;
		UHermesSettings::ApplyCommandLineOverrides(
			TEXT("-HermesHost=hermes.local -HermesPort=1234"), Host, Port);
		TestEqual(TEXT("both host"), Host, TEXT("hermes.local"));
		TestEqual(TEXT("both port"), Port, 1234);
	}

	return true;
}
```

> **주의:** 위 테스트는 `UE_BUILD_SHIPPING`이 0인 구성(Development/Editor)에서만 오버라이드가 동작한다는 전제로 작성되었다. 자동화 테스트는 Shipping에 포함되지 않으므로 문제없다.

- [ ] **Step 4: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 컴파일 실패. `ApplyCommandLineOverrides`와 `GetResolvedEndpoint`가 선언만 있고 정의가 없어 링크 에러(`unresolved external symbol`)가 난다.

- [ ] **Step 5: 최소 구현 작성**

`Settings/HermesSettings.cpp` 전체 내용:

```cpp
#include "Settings/HermesSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UHermesSettings::ApplyCommandLineOverrides(const TCHAR* CmdLine,
                                                FString& InOutHost, int32& InOutPort)
{
#if UE_BUILD_SHIPPING
	// 배포 빌드에서는 최종 사용자가 클라이언트를 임의 서버로 리다이렉트하지 못하게 한다.
	// 주소 은닉이 목적이 아니라, 기본 경로를 막는 것이 목적이다.
	(void)CmdLine; (void)InOutHost; (void)InOutPort;
	return;
#else
	if (!CmdLine)
	{
		return;
	}

	FString HostOverride;
	if (FParse::Value(CmdLine, TEXT("HermesHost="), HostOverride) && !HostOverride.IsEmpty())
	{
		InOutHost = HostOverride;
	}

	int32 PortOverride = 0;
	if (FParse::Value(CmdLine, TEXT("HermesPort="), PortOverride))
	{
		// 범위 밖이면 ini 값을 유지한다. 잘못된 인자로 연결이 조용히 깨지지 않게 한다.
		if (PortOverride >= 1 && PortOverride <= 65535)
		{
			InOutPort = PortOverride;
		}
	}
#endif
}

void UHermesSettings::GetResolvedEndpoint(FString& OutHost, int32& OutPort) const
{
	OutHost = Host;
	OutPort = Port;
	ApplyCommandLineOverrides(FCommandLine::Get(), OutHost, OutPort);
}
```

- [ ] **Step 6: 빌드 후 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공. 테스트 6종 통과 (기존 5종 + `Hermes.Settings.CommandLineOverride`).

- [ ] **Step 7: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesAgentNPC.Build.cs
git commit -F - <<'EOF'
feat: UHermesSettings 설정 클래스 및 커맨드라인 오버라이드 추가

에디터 Project Settings > Plugins > Hermes Agent NPC 에 노출되고
값은 Config/DefaultGame.ini 에 저장된다. -HermesHost= / -HermesPort=
오버라이드는 Shipping 빌드에서 비활성화된다.

파싱 로직을 static 순수 함수로 분리해 실제 프로세스 커맨드라인에
의존하지 않고 테스트한다.
EOF
```

---

## Task 2: 하드코딩 상수를 설정 소비로 전환

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesWorkerConfig.h`
- Create: `Config/DefaultGame.ini` (프로젝트 루트)
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp:11-14,145-165`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:13-15,24,47,53`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionDispatcher.cpp:64`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/NPC/HermesNPCCharacter.cpp:66`

**Interfaces:**
- Consumes: `UHermesSettings` (Task 1)
- Produces:
  - `struct FHermesTlsConfig` — TLS 값 묶음 (Phase 4에서 채워 씀)
  - `struct FHermesWorkerConfig` — 워커에 넘기는 값 타입 설정
  - `FHermesSocketWorker::FHermesSocketWorker(const FHermesWorkerConfig& InConfig)` — 생성자 시그니처 변경

- [ ] **Step 1: 설정 구조체 헤더 작성**

`Transport/HermesWorkerConfig.h` 전체 내용:

```cpp
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
	int32   MaxInboundQueueSize   = 1024;
	int32   MaxOutboundQueueSize  = 256;

	FHermesTlsConfig Tls;
};
```

- [ ] **Step 2: 워커 헤더를 설정 구조체 기반으로 변경**

`Transport/HermesSocketWorker.h`에서 `#include "Protocol/HermesFrameCodec.h"` 아래에 include를 추가하고, 생성자와 멤버를 바꾼다.

```cpp
#include "Transport/HermesWorkerConfig.h"
```

생성자 선언(`:18`)을 교체:

```cpp
	explicit FHermesSocketWorker(const FHermesWorkerConfig& InConfig);
```

멤버 `FString Host; int32 Port;`(`:37-38`)를 교체:

```cpp
	FHermesWorkerConfig Config;
```

- [ ] **Step 3: 워커 구현에서 설정을 사용하도록 변경**

`Transport/HermesSocketWorker.cpp:11-14`의 생성자를 교체:

```cpp
FHermesSocketWorker::FHermesSocketWorker(const FHermesWorkerConfig& InConfig)
	: Config(InConfig)
{
}
```

`ConnectSocket()` 안에서 `Host`, `Port`를 쓰던 두 곳을 `Config.Host`, `Config.Port`로 바꾼다.

```cpp
	FIPv4Address Addr;
	if (!FIPv4Address::Parse(Config.Host, Addr))
	{
		return false;
	}

	TSharedRef<FInternetAddr> InetAddr = SS->CreateInternetAddr();
	InetAddr->SetIp(Addr.Value);
	InetAddr->SetPort(Config.Port);
```

`Run()`(`:146-148`)의 백오프 지역 상수를 설정 기반으로 교체:

```cpp
uint32 FHermesSocketWorker::Run()
{
	float Backoff = Config.InitialReconnectDelay;
	const float MaxBackoff = Config.MaxReconnectDelay;
```

그리고 성공 시 리셋하는 줄(`:157`)도 교체:

```cpp
				Backoff = Config.InitialReconnectDelay; // 성공 시 리셋
```

- [ ] **Step 4: 구독 시스템에서 설정을 읽어 주입**

`Connection/HermesConnectionSubsystem.cpp`의 include에 추가:

```cpp
#include "Settings/HermesSettings.h"
#include "Transport/HermesWorkerConfig.h"
```

`:13-15`의 `static const` 3줄을 **삭제**한다.

`Initialize()`(`:17-29`)를 교체:

```cpp
void UHermesConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	PlayerId = LoadOrCreatePlayerId();

	Dispatcher = NewObject<UHermesActionDispatcher>(this);

	// 워커는 전용 스레드에서 돌므로 UObject를 넘기지 않고 값만 복사해 전달한다.
	FHermesWorkerConfig Cfg;
	Settings->GetResolvedEndpoint(Cfg.Host, Cfg.Port);
	Cfg.InitialReconnectDelay = Settings->InitialReconnectDelay;
	Cfg.MaxReconnectDelay     = Settings->MaxReconnectDelay;
	Cfg.MaxInboundQueueSize   = Settings->MaxInboundQueueSize;
	Cfg.MaxOutboundQueueSize  = Settings->MaxOutboundQueueSize;

	Worker = MakeUnique<FHermesSocketWorker>(Cfg);
	Worker->Start();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHermesConnectionSubsystem::Tick), 0.f);
}
```

`LoadOrCreatePlayerId()`(`:45-55`)에서 `SaveSlot` 상수를 설정으로 교체:

```cpp
FString UHermesConnectionSubsystem::LoadOrCreatePlayerId()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		if (!SG->PlayerId.IsEmpty()) return SG->PlayerId;
	}
	UHermesSaveGame* NewSG = Cast<UHermesSaveGame>(UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	NewSG->PlayerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	UGameplayStatics::SaveGameToSlot(NewSG, SaveSlot, 0);
	return NewSG->PlayerId;
}
```

- [ ] **Step 5: 디스패처와 NPC의 리터럴 교체**

`Actions/HermesActionDispatcher.cpp`의 include에 추가:

```cpp
#include "Settings/HermesSettings.h"
```

`:64`의 `15.f`를 교체 — 타이머 설정 호출부는 이렇게 된다:

```cpp
	// 액션 응답 타임아웃 폴백 (World 가 있을 때만; 즉시성 핸들러엔 bDone 가드로 무해)
	if (UWorld* World = GetWorld())
	{
		const float TimeoutSeconds = GetDefault<UHermesSettings>()->ActionTimeoutSeconds;
		FTimerHandle Th;
		World->GetTimerManager().SetTimer(Th, [bDone, Id, OnResult]()
		{
			if (*bDone)
			{
				return;
			}
			*bDone = true;
			OnResult(HermesJson::MakeActionResult(Id, false, nullptr, TEXT("timeout")));
		}, TimeoutSeconds, false);
	}
```

`NPC/HermesNPCCharacter.cpp`의 include에 추가:

```cpp
#include "Settings/HermesSettings.h"
```

`:66`을 교체:

```cpp
		AI->MoveToActor(Player, GetDefault<UHermesSettings>()->FollowDistance);
```

- [ ] **Step 6: 프로젝트 ini 작성**

`Config/DefaultGame.ini` 신규 생성:

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=192.168.0.111
Port=8770
bUseTLS=False
```

> `bUseTLS=False`는 **Phase 4가 끝나기 전까지의 임시값**이다. Task 26에서 서버 인증서 설정과 함께 `True`로 바꾼다. 이 시점에는 TLS 구현 자체가 없으므로 이 값은 읽히기만 하고 아무 동작도 하지 않는다.

- [ ] **Step 7: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 6종 통과.

- [ ] **Step 8: 하드코딩이 사라졌는지 확인**

```powershell
Select-String -Path "Plugins\HermesAgentNPC\Source\HermesAgentNPC\*" -Pattern "192\.168\.0\.111" -Recurse
```

Expected: 출력 없음. 결과가 나오면 놓친 곳이 있다는 뜻이다.

- [ ] **Step 9: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source Config/DefaultGame.ini
git commit -F - <<'EOF'
refactor: 하드코딩된 주소·상수를 UHermesSettings 소비로 전환

플러그인 소스에서 192.168.0.111 을 제거하고 사내 주소를 프로젝트의
Config/DefaultGame.ini 로 옮긴다. 재연결 백오프, 액션 타임아웃,
추적 거리, SaveGame 슬롯명도 함께 설정화한다.

워커는 전용 스레드에서 돌므로 UObject 대신 FHermesWorkerConfig 값
사본을 생성자로 받는다. 인자가 여럿이라 위치 인자 대신 구조체를 쓴다.
EOF
```

---

## Task 3: DNS 호스트명 해석 (IPv4 우선)

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp:37-60`

**Interfaces:**
- Consumes: `FHermesWorkerConfig::Host` / `Port` (Task 2)
- Produces: 없음 (내부 동작 변경)

- [ ] **Step 1: `ConnectSocket()`의 주소 해석 교체**

`Transport/HermesSocketWorker.cpp` 상단 include에서 `#include "Interfaces/IPv4/IPv4Address.h"`를 **삭제**하고 아래를 추가한다.

```cpp
#include "SocketTypes.h"
```

`ConnectSocket()` 안에서 `FIPv4Address Addr;` 블록부터 `InetAddr->SetPort(...)` 까지를 아래로 교체한다.

```cpp
	// 호스트명과 IP를 모두 처리한다. 컨테이너명·k8s 서비스명 같은 유동 주소 대응.
	FAddressInfoResult Result = SS->GetAddressInfo(*Config.Host, nullptr,
		EAddressInfoFlags::Default, NAME_None);
	if (Result.ReturnCode != SE_NO_ERROR || Result.Results.Num() == 0)
	{
		return false;
	}

	// IPv4 우선. 듀얼스택에서 OS가 IPv6를 먼저 주더라도 서버가 IPv4만 수신하면
	// 접속이 실패하므로, 기존 환경의 동작을 바꾸지 않기 위해 IPv4를 먼저 고른다.
	const FAddressInfoResultData* Chosen = &Result.Results[0];
	for (const FAddressInfoResultData& R : Result.Results)
	{
		if (R.Address->GetProtocolType() == FNetworkProtocolTypes::IPv4)
		{
			Chosen = &R;
			break;
		}
	}

	TSharedRef<FInternetAddr> InetAddr = Chosen->Address->Clone();
	InetAddr->SetPort(Config.Port);
```

이어지는 소켓 생성부는 선택된 주소의 프로토콜을 따르도록 바꾼다.

**`FTcpSocketBuilder` 를 쓸 수 없다.** 이 빌더는 `FIPv4Endpoint` 에서 프로토콜을 유도하므로 구조적으로 IPv4 전용이고, 프로토콜을 지정하는 메서드가 없다(`WithProtocol` 은 존재하지 않는다). `ISocketSubsystem::CreateSocket` 을 직접 부른다.

```cpp
	// FTcpSocketBuilder 는 FIPv4Endpoint 에서 프로토콜을 유도하므로 구조적으로
	// IPv4 전용이다. IPv6 폴백을 지원하려면 CreateSocket 을 직접 불러야 한다.
	Socket = SS->CreateSocket(NAME_Stream, TEXT("HermesClient"),
		Chosen->Address->GetProtocolType());
	if (!Socket)
	{
		return false;
	}

	// 빌더의 AsBlocking() 과 동일하게 맞춘다. 연결 후 논블로킹으로 전환한다.
	Socket->SetNonBlocking(false);
```

`#include "Common/TcpSocketBuilder.h"` 는 더 이상 필요 없으므로 지운다. 대신 `#include "SocketTypes.h"` 와 `#include "IPAddress.h"` 를 추가한다.

- [ ] **Step 2: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 6종 통과.

> DNS 해석은 네트워크에 의존하므로 자동화 테스트를 만들지 않는다. Step 3의 수동 확인으로 대체한다.

- [ ] **Step 3: 수동 확인 — 호스트명 해석**

`Config/DefaultGame.ini`의 `Host`를 임시로 `localhost`로 바꾸고 에디터에서 PIE를 실행한다.

Expected: `FIPv4Address::Parse`로는 실패하던 `localhost`가 해석되어 `127.0.0.1:8770` 접속을 **시도**한다. 서버가 없으면 접속 실패 후 백오프 재연결이 반복되는 것이 정상이며, "주소 해석 실패"가 아니라 "연결 실패"로 진행되는지가 확인 대상이다.

확인 후 `Host`를 `192.168.0.111`로 되돌린다.

- [ ] **Step 4: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp
git commit -F - <<'EOF'
feat: GetAddressInfo 기반 DNS 해석으로 호스트명 지원

FIPv4Address::Parse 는 숫자 IP만 받아 설정으로 주소를 여는 순간
도메인명 입력이 실패했다. ISocketSubsystem::GetAddressInfo 로 교체해
호스트명과 IP를 모두 처리한다.

듀얼스택에서 OS가 IPv6를 먼저 반환해도 서버가 IPv4만 수신하면
접속이 실패하므로 IPv4를 우선 선택하고, 없을 때만 IPv6로 넘어간다.
소켓도 선택된 주소의 프로토콜을 따른다.

이 호출은 워커 전용 스레드에서만 실행되므로 동기 DNS 블로킹이
게임 스레드에 영향을 주지 않는다.
EOF
```

---

# Phase 2 — 입력 강건성

## Task 4: 중단 가능한 백오프 sleep

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp:145-170`

**Interfaces:**
- Consumes: `FHermesWorkerConfig` (Task 2)
- Produces: `void FHermesSocketWorker::InterruptibleSleep(float Seconds)` (private)

**왜 이 태스크가 필요한가:** `~FHermesSocketWorker()`가 `Thread->Kill(true)`로 `Run()` 반환을 대기하는데, 백오프가 최대 30초를 통짜로 잔다. Task 2에서 `MaxReconnectDelay`를 설정으로 열면서 상한이 300초가 되었으므로 방치하면 최악 5분간 게임 스레드가 멈춘다.

- [ ] **Step 1: 헤더에 선언 추가**

`Transport/HermesSocketWorker.h`의 `private:` 섹션에서 `bool ReceiveAvailable();` 아래에 추가한다.

```cpp
	/** 총 Seconds 만큼 자되 100ms 마다 깨어나 중단 요청을 확인한다. */
	void InterruptibleSleep(float Seconds);
```

- [ ] **Step 2: 구현 추가**

`Transport/HermesSocketWorker.cpp`에서 `uint32 FHermesSocketWorker::Run()` **바로 위**에 정의를 추가한다.

```cpp
void FHermesSocketWorker::InterruptibleSleep(float Seconds)
{
	// 통짜 Sleep 은 소멸자의 Thread->Kill(true) 대기를 그만큼 늘린다.
	// MaxReconnectDelay 가 설정으로 최대 300초까지 열려 있어 조각내지 않으면
	// PIE 정지가 수 분간 멈출 수 있다.
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

- [ ] **Step 3: `Run()`의 백오프 대기를 교체**

`Run()` 안의 실패 경로(`else` 블록)를 아래로 교체한다.

```cpp
			else
			{
				const float Jitter = FMath::FRandRange(0.f, Backoff * 0.25f);
				InterruptibleSleep(Backoff + Jitter);
				if (bStopRequested)
				{
					break;
				}
				Backoff = FMath::Min(Backoff * 2.f, MaxBackoff);
				continue;
			}
```

- [ ] **Step 4: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 6종 통과.

- [ ] **Step 5: 수동 확인 — 종료 응답성**

1. `Config/DefaultGame.ini`에 `MaxReconnectDelay=300` 을 추가한다.
2. 서버가 꺼진 상태(또는 `Host`를 도달 불가 주소로)에서 에디터 PIE를 실행한다.
3. 백오프가 커지도록 **1분 이상** 방치한다.
4. PIE 정지 버튼을 누른다.

Expected: 에디터가 멈추지 않고 즉시 정지된다. 이 수정 전이라면 최대 300초 멈춘다.

확인 후 `MaxReconnectDelay` 줄을 ini에서 지운다.

- [ ] **Step 6: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport
git commit -m "fix: 백오프 대기를 조각 sleep 으로 바꿔 종료 응답성 확보"
```

커밋 본문에 아래 내용을 포함한다 (에디터가 열리면 붙여넣거나 `-m` 을 두 번 쓴다):

```
소멸자의 Thread->Kill(true) 가 Run() 반환을 대기하는데 백오프가
통짜 Sleep 이라 서버가 꺼진 상태에서 PIE 를 정지하면 게임 스레드가
그만큼 멈췄다. MaxReconnectDelay 를 설정으로 열면서 상한이 300초가
되어 방치하면 최악 5분 행이 된다.

100ms 조각마다 중단 요청을 확인해 종료 지연을 100ms 이내로 고정한다.
```

---

## Task 5: 액션 파라미터 하드 바운드

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionParams.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionParams.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionParams.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/MoveToActionHandler.cpp:15-35`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/ItemTransferActionHandler.cpp:20-30`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Inventory/HermesInventoryComponent.cpp:29-33`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Inventory/HermesInventory.spec.cpp`

**Interfaces:**
- Consumes: `UHermesSettings::MaxWorldCoordinate` / `MaxItemQuantity` / `MaxItemIdLength` (Task 1)
- Produces:
  - `bool HermesParams::IsValidCoordinate(double V, float Limit)`
  - `bool HermesParams::IsValidQuantity(double V, int32 Max, int32& OutQty)`
  - `bool HermesParams::IsValidItemId(const FString& Id, int32 MaxLen)`

**왜 이 태스크가 필요한가:** 하한(음수·0)은 이미 막혀 있으나 상한과 유한성이 비어 있다. `move_to`에 `1e308`이 오면 `(float)` 캐스트로 `inf`가 되어 `FVector(inf,inf,inf)`가 엔진에 들어가고, `item_transfer`에 `2e9`가 오면 인벤토리 누적에서 int32 오버플로로 **수량이 음수가 된다.**

- [ ] **Step 1: 검증 헬퍼 헤더 작성**

`Actions/HermesActionParams.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 액션 파라미터의 범위·유한성 검증.
 * 엔진 게임플레이 타입에 의존하지 않는 순수 로직이라 단독 테스트가 가능하다.
 * 서버를 신뢰하지 않는다는 전제로, 값을 쓰기 전에 반드시 통과시킨다.
 */
namespace HermesParams
{
	/** 유한하고 |V| <= Limit 인지 검사한다. NaN/Inf 는 거부. */
	bool IsValidCoordinate(double V, float Limit);

	/**
	 * 유한한 정수이고 1 <= V <= Max 인지 검사한다.
	 * int32 캐스트 이전에 호출해야 미정의 동작을 피할 수 있다.
	 * 통과 시에만 OutQty 에 값을 채운다.
	 */
	bool IsValidQuantity(double V, int32 Max, int32& OutQty);

	/** 비어있지 않고 길이가 MaxLen 이하인지 검사한다. */
	bool IsValidItemId(const FString& Id, int32 MaxLen);
}
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Actions/HermesActionParams.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Actions/HermesActionParams.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsCoordinateTest,
	"Hermes.ActionParams.Coordinate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsCoordinateTest::RunTest(const FString& Parameters)
{
	const float Limit = 1.0e7f;

	TestTrue(TEXT("0 ok"), HermesParams::IsValidCoordinate(0.0, Limit));
	TestTrue(TEXT("normal ok"), HermesParams::IsValidCoordinate(1200.0, Limit));
	TestTrue(TEXT("negative ok"), HermesParams::IsValidCoordinate(-1200.0, Limit));

	// 경계: Limit 은 통과, 그보다 크면 거부. 음수 방향도 대칭이어야 한다.
	TestTrue(TEXT("at limit ok"), HermesParams::IsValidCoordinate((double)Limit, Limit));
	TestTrue(TEXT("at -limit ok"), HermesParams::IsValidCoordinate(-(double)Limit, Limit));
	TestFalse(TEXT("over limit rejected"), HermesParams::IsValidCoordinate((double)Limit + 1.0, Limit));
	TestFalse(TEXT("under -limit rejected"), HermesParams::IsValidCoordinate(-(double)Limit - 1.0, Limit));

	// non-finite 는 전부 거부. 이것이 FVector(inf,...) 가 엔진에 들어가는 경로를 막는다.
	const double Inf = std::numeric_limits<double>::infinity();
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("+inf rejected"), HermesParams::IsValidCoordinate(Inf, Limit));
	TestFalse(TEXT("-inf rejected"), HermesParams::IsValidCoordinate(-Inf, Limit));
	TestFalse(TEXT("nan rejected"), HermesParams::IsValidCoordinate(NaN, Limit));
	TestFalse(TEXT("1e308 rejected"), HermesParams::IsValidCoordinate(1.0e308, Limit));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsQuantityTest,
	"Hermes.ActionParams.Quantity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsQuantityTest::RunTest(const FString& Parameters)
{
	const int32 Max = 999999;
	int32 Qty = -1;

	TestTrue(TEXT("1 ok"), HermesParams::IsValidQuantity(1.0, Max, Qty));
	TestEqual(TEXT("1 value"), Qty, 1);

	TestTrue(TEXT("max ok"), HermesParams::IsValidQuantity((double)Max, Max, Qty));
	TestEqual(TEXT("max value"), Qty, Max);

	TestFalse(TEXT("0 rejected"), HermesParams::IsValidQuantity(0.0, Max, Qty));
	TestFalse(TEXT("-1 rejected"), HermesParams::IsValidQuantity(-1.0, Max, Qty));
	TestFalse(TEXT("max+1 rejected"), HermesParams::IsValidQuantity((double)Max + 1.0, Max, Qty));

	// int32 범위를 넘는 값. 캐스트 전에 걸러야 미정의 동작을 피한다.
	TestFalse(TEXT("2e9 rejected"), HermesParams::IsValidQuantity(2.0e9, Max, Qty));
	TestFalse(TEXT("1e18 rejected"), HermesParams::IsValidQuantity(1.0e18, Max, Qty));

	// 소수는 거부. quantity 는 정수여야 한다.
	TestFalse(TEXT("1.5 rejected"), HermesParams::IsValidQuantity(1.5, Max, Qty));

	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("nan rejected"), HermesParams::IsValidQuantity(NaN, Max, Qty));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesParamsItemIdTest,
	"Hermes.ActionParams.ItemId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesParamsItemIdTest::RunTest(const FString& Parameters)
{
	const int32 MaxLen = 64;

	TestTrue(TEXT("normal ok"), HermesParams::IsValidItemId(TEXT("health_potion"), MaxLen));
	TestFalse(TEXT("empty rejected"), HermesParams::IsValidItemId(TEXT(""), MaxLen));

	const FString AtLimit = FString::ChrN(MaxLen, TEXT('a'));
	const FString OverLimit = FString::ChrN(MaxLen + 1, TEXT('a'));
	TestTrue(TEXT("at limit ok"), HermesParams::IsValidItemId(AtLimit, MaxLen));
	TestFalse(TEXT("over limit rejected"), HermesParams::IsValidItemId(OverLimit, MaxLen));

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `HermesParams::IsValidCoordinate` 등 3개가 `unresolved external symbol`로 나온다.

- [ ] **Step 4: 구현 작성**

`Actions/HermesActionParams.cpp` 전체 내용:

```cpp
#include "Actions/HermesActionParams.h"

namespace HermesParams
{
	bool IsValidCoordinate(double V, float Limit)
	{
		if (!FMath::IsFinite(V))
		{
			return false;
		}
		return FMath::Abs(V) <= (double)Limit;
	}

	bool IsValidQuantity(double V, int32 Max, int32& OutQty)
	{
		if (!FMath::IsFinite(V))
		{
			return false;
		}
		// 소수는 거부한다. quantity 는 프로토콜상 정수다.
		if (V != FMath::TruncToDouble(V))
		{
			return false;
		}
		if (V < 1.0)
		{
			return false;
		}
		const double MaxD = (double)FMath::Max(1, Max);
		if (V > MaxD)
		{
			return false;
		}
		// 위 검사로 1 <= V <= Max 가 보장되고 Max 의 ClampMax 는 2e9 < INT32_MAX 이므로
		// 이 시점의 캐스트는 안전하다.
		OutQty = (int32)V;
		return true;
	}

	bool IsValidItemId(const FString& Id, int32 MaxLen)
	{
		return !Id.IsEmpty() && Id.Len() <= FMath::Max(1, MaxLen);
	}
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.ActionParams; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.ActionParams.Coordinate` / `.Quantity` / `.ItemId` 3종 PASS.

- [ ] **Step 6: `MoveToActionHandler`에 좌표 검증 적용**

`Actions/MoveToActionHandler.cpp`의 include에 추가:

```cpp
#include "Actions/HermesActionParams.h"
#include "Settings/HermesSettings.h"
```

파라미터 파싱 블록 다음, `AAIController* AI = ...` **앞에** 검증을 넣는다.

```cpp
	// FVector 생성 전에 검사한다. non-finite 벡터가 MoveToLocation 에 들어가면
	// 엔진이 check 실패하거나 네비게이션이 비정상 동작한다.
	const float CoordLimit = GetDefault<UHermesSettings>()->MaxWorldCoordinate;
	if (!HermesParams::IsValidCoordinate(X, CoordLimit) ||
		!HermesParams::IsValidCoordinate(Y, CoordLimit) ||
		!HermesParams::IsValidCoordinate(Z, CoordLimit))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("coordinate out of range"));
		return;
	}
```

- [ ] **Step 7: `ItemTransferActionHandler`에 수량·item_id 검증 적용**

`Actions/ItemTransferActionHandler.cpp`의 include에 추가:

```cpp
#include "Actions/HermesActionParams.h"
#include "Settings/HermesSettings.h"
```

파라미터 파싱 블록(`:20-30`)을 아래로 교체한다.

```cpp
	FString Direction, ItemId;
	double Qd = 0;
	if (!Payload.Params.IsValid() ||
		!Payload.Params->TryGetStringField(TEXT("direction"), Direction) ||
		!Payload.Params->TryGetStringField(TEXT("item_id"), ItemId) ||
		!Payload.Params->TryGetNumberField(TEXT("quantity"), Qd))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid params"));
		return;
	}

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	if (!HermesParams::IsValidItemId(ItemId, Settings->MaxItemIdLength))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid item_id"));
		return;
	}

	// (int32) 캐스트 이전에 범위를 확정한다. Qd > INT32_MAX 면 캐스트 자체가
	// C++ 미정의 동작이고, 통과하더라도 인벤토리 누적에서 오버플로가 난다.
	int32 Qty = 0;
	if (!HermesParams::IsValidQuantity(Qd, Settings->MaxItemQuantity, Qty))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("quantity out of range"));
		return;
	}
```

이 교체로 기존의 `const int32 Qty = (int32)Qd;` 줄은 사라진다. 아래 `give`/`receive` 분기는 그대로 `Qty`를 쓴다.

- [ ] **Step 8: 인벤토리 누적 오버플로 포화**

`Inventory/HermesInventoryComponent.cpp`의 `Add()`에서 기존 항목 갱신 부분을 교체한다.

```cpp
	if (UHermesItem* It = Find(ItemId))
	{
		// 핸들러 검증과 별개로 겹쳐 막는다. 이 컴포넌트는 액션 경로 외에서도 호출된다.
		// int32 오버플로가 나면 수량이 음수가 되어 아이템 복제 버그가 된다.
		It->Quantity = (It->Quantity > MAX_int32 - Qty) ? MAX_int32 : It->Quantity + Qty;
		return;
	}
```

- [ ] **Step 9: 인벤토리 포화 테스트 추가**

`Inventory/HermesInventory.spec.cpp` 파일 **끝에** 새 테스트를 덧붙인다.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesInventoryOverflowTest,
	"Hermes.Inventory.AddSaturates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesInventoryOverflowTest::RunTest(const FString& Parameters)
{
	UHermesInventoryComponent* Inv = NewObject<UHermesInventoryComponent>(GetTransientPackage());

	Inv->Add(TEXT("gold"), MAX_int32);
	TestEqual(TEXT("at max"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	// 더 더해도 음수로 뒤집히지 않고 상한에서 멈춰야 한다.
	Inv->Add(TEXT("gold"), 1);
	TestEqual(TEXT("saturated, not negative"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	Inv->Add(TEXT("gold"), MAX_int32);
	TestEqual(TEXT("still saturated"), Inv->GetQuantity(TEXT("gold")), MAX_int32);

	return true;
}
```

- [ ] **Step 10: 전체 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공. 테스트 10종 통과 (기존 5 + Settings 1 + ActionParams 3 + Inventory 포화 1).

- [ ] **Step 11: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions Plugins/HermesAgentNPC/Source/HermesAgentNPC/Inventory
git commit -m "feat: 액션 파라미터 하드 바운드 및 인벤토리 오버플로 포화"
```

커밋 본문에 포함할 내용:

```
디스패처는 명령 이름만 화이트리스트 검사하고 파라미터는 핸들러
자율에 맡긴다. 하한은 이미 막혀 있었으나 상한과 유한성이 비어 있었다.

- move_to: FVector 생성 전에 3축 유한성과 범위를 검사한다.
  1e308 이 (float) 캐스트로 inf 가 되어 MoveToLocation 에 들어가던 경로를 막는다
- item_transfer: (int32) 캐스트 이전에 범위를 확정한다.
  Qd > INT32_MAX 는 캐스트 자체가 미정의 동작이었다
- item_id: 빈 문자열과 과도한 길이를 거부한다
- 인벤토리 Add(): 누적을 MAX_int32 에서 포화시킨다. 오버플로로 수량이
  음수가 되면 아이템 복제 버그가 된다
```

---

## Task 6: 액션 레이트 리밋과 타이머 회수

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesRateLimiter.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesRateLimiter.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesRateLimiter.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionDispatcher.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/HermesActionDispatcher.cpp:15-66`

**Interfaces:**
- Consumes: `UHermesSettings::MaxActionsPerSecond` / `ActionTimeoutSeconds` (Task 1)
- Produces:
  - `class FHermesRateLimiter` — `void Configure(int32 PerSecond)`, `bool TryConsume(double NowSeconds)`

**왜 이 태스크가 필요한가:** 틱 예산(Task 7)을 64로 두어도 60fps에서 초당 3,840개 액션이 처리 가능하고 각각이 타임아웃 타이머를 만든다 → 최대 약 57,600개 동시 타이머. 게다가 `HermesActionDispatcher.cpp:55`의 `FTimerHandle Th;`는 지역 변수로 버려져 취소가 불가능하다.

- [ ] **Step 1: 레이트 리미터 헤더 작성**

`Actions/HermesRateLimiter.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 토큰 버킷 레이트 리미터.
 * 시간을 인자로 주입받아 엔진·타이머에 의존하지 않으므로 단독 테스트가 가능하다.
 */
class FHermesRateLimiter
{
public:
	/** 용량과 초당 충전량을 PerSecond 로 함께 설정한다. */
	void Configure(int32 PerSecond);

	/** NowSeconds 기준으로 토큰을 채우고 1개를 소비한다. 소비 실패 시 false. */
	bool TryConsume(double NowSeconds);

private:
	double Tokens          = 0.0;
	double Capacity        = 0.0;
	double RefillPerSecond = 0.0;
	double LastRefillTime  = 0.0;
	bool   bInitialized    = false;
};
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Actions/HermesRateLimiter.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Actions/HermesRateLimiter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesRateLimiterTest,
	"Hermes.RateLimiter.TokenBucket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesRateLimiterTest::RunTest(const FString& Parameters)
{
	// 용량 내 연속 요청은 모두 통과한다.
	{
		FHermesRateLimiter L;
		L.Configure(5);
		for (int32 i = 0; i < 5; ++i)
		{
			TestTrue(FString::Printf(TEXT("burst %d passes"), i), L.TryConsume(100.0));
		}
		// 소진 후 같은 시각의 요청은 거부된다.
		TestFalse(TEXT("exhausted rejected"), L.TryConsume(100.0));
	}

	// 1초 경과 후 용량만큼 다시 충전된다.
	{
		FHermesRateLimiter L;
		L.Configure(5);
		for (int32 i = 0; i < 5; ++i)
		{
			L.TryConsume(100.0);
		}
		TestFalse(TEXT("still exhausted"), L.TryConsume(100.0));

		TestTrue(TEXT("refilled after 1s"), L.TryConsume(101.0));
		for (int32 i = 0; i < 4; ++i)
		{
			TestTrue(FString::Printf(TEXT("refill burst %d"), i), L.TryConsume(101.0));
		}
		TestFalse(TEXT("exhausted again"), L.TryConsume(101.0));
	}

	// 부분 충전: 0.5초면 절반만 찬다.
	{
		FHermesRateLimiter L;
		L.Configure(10);
		for (int32 i = 0; i < 10; ++i)
		{
			L.TryConsume(200.0);
		}
		// 0.5초 => 5개 충전
		for (int32 i = 0; i < 5; ++i)
		{
			TestTrue(FString::Printf(TEXT("half refill %d"), i), L.TryConsume(200.5));
		}
		TestFalse(TEXT("half refill exhausted"), L.TryConsume(200.5));
	}

	// 시간이 역행해도 토큰이 폭증하거나 음수가 되지 않는다.
	{
		FHermesRateLimiter L;
		L.Configure(3);
		TestTrue(TEXT("first ok"), L.TryConsume(500.0));
		TestTrue(TEXT("backward time ok"), L.TryConsume(400.0));
		TestTrue(TEXT("backward time ok 2"), L.TryConsume(400.0));
		// 용량 3 을 넘겨 소비할 수 없다.
		TestFalse(TEXT("no free tokens from time travel"), L.TryConsume(400.0));
	}

	// 충전은 용량을 넘지 않는다.
	{
		FHermesRateLimiter L;
		L.Configure(2);
		TestTrue(TEXT("t0 a"), L.TryConsume(0.0));
		TestTrue(TEXT("t0 b"), L.TryConsume(0.0));
		// 100초를 기다려도 용량 2 를 넘게 쌓이지 않는다.
		TestTrue(TEXT("t100 a"), L.TryConsume(100.0));
		TestTrue(TEXT("t100 b"), L.TryConsume(100.0));
		TestFalse(TEXT("capped at capacity"), L.TryConsume(100.0));
	}

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `FHermesRateLimiter::Configure` / `TryConsume` 미정의.

- [ ] **Step 4: 구현 작성**

`Actions/HermesRateLimiter.cpp` 전체 내용:

```cpp
#include "Actions/HermesRateLimiter.h"

void FHermesRateLimiter::Configure(int32 PerSecond)
{
	Capacity        = (double)FMath::Max(1, PerSecond);
	RefillPerSecond = Capacity;
	Tokens          = Capacity;   // 시작은 가득 찬 상태
	LastRefillTime  = 0.0;
	bInitialized    = false;
}

bool FHermesRateLimiter::TryConsume(double NowSeconds)
{
	if (!bInitialized)
	{
		LastRefillTime = NowSeconds;
		bInitialized   = true;
	}

	const double Elapsed = NowSeconds - LastRefillTime;
	if (Elapsed > 0.0)
	{
		Tokens = FMath::Min(Capacity, Tokens + Elapsed * RefillPerSecond);
	}
	// 시간이 역행해도 기준을 갱신해 둔다. 그래야 이후 정상 시각에서
	// 거대한 Elapsed 가 계산되어 토큰이 폭증하는 일이 없다.
	LastRefillTime = NowSeconds;

	if (Tokens >= 1.0)
	{
		Tokens -= 1.0;
		return true;
	}
	return false;
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.RateLimiter; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.RateLimiter.TokenBucket` PASS.

- [ ] **Step 6: 디스패처 헤더에 리미터 멤버 추가**

`Actions/HermesActionDispatcher.h`의 include에 추가:

```cpp
#include "Actions/HermesRateLimiter.h"
```

클래스의 `private:` 섹션(`Handlers` 멤버 근처)에 추가:

```cpp
	FHermesRateLimiter RateLimiter;
	bool bRateLimiterConfigured = false;
```

- [ ] **Step 7: `Dispatch()`에 레이트 리밋과 타이머 회수 적용**

`Actions/HermesActionDispatcher.cpp`의 include에 추가:

```cpp
#include "Settings/HermesSettings.h"
#include "HAL/PlatformTime.h"
```

`Dispatch()` 전체(`:15-66`)를 아래로 교체한다.

```cpp
void UHermesActionDispatcher::Dispatch(const FHermesActionPayload& Payload,
	TFunction<void(const FString&)> OnResult)
{
	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	if (!bRateLimiterConfigured)
	{
		RateLimiter.Configure(Settings->MaxActionsPerSecond);
		bRateLimiterConfigured = true;
	}

	// 정상 서버라면 이 선에 닿지 않는다. 도달했다는 것은 폭주이거나
	// 프롬프트 인젝션으로 액션이 남발되고 있다는 신호다.
	if (!RateLimiter.TryConsume(FPlatformTime::Seconds()))
	{
		OnResult(HermesJson::MakeActionResult(Payload.Id, false, nullptr, TEXT("rate limited")));
		return;
	}

	IHermesActionHandler* Chosen = nullptr;
	for (const TScriptInterface<IHermesActionHandler>& H : Handlers)
	{
		if (H && H->CanHandle(Payload.Command))
		{
			Chosen = H.GetInterface();
			break;
		}
	}

	if (!Chosen)
	{
		// 화이트리스트 미등록: 즉시 거부
		OnResult(HermesJson::MakeActionResult(Payload.Id, false, nullptr, TEXT("unsupported command")));
		return;
	}

	// 핸들러 응답과 타임아웃 중 하나만 회신하도록 가드
	TSharedRef<bool> bDone = MakeShared<bool>(false);
	TSharedRef<FTimerHandle> Th = MakeShared<FTimerHandle>();
	TWeakObjectPtr<UHermesActionDispatcher> WeakThis(this);
	const FString Id = Payload.Id;

	FHermesActionResultDelegate OnDone;
	OnDone.BindLambda([bDone, Th, WeakThis, Id, OnResult](bool bOk, TSharedPtr<FJsonObject> Result, FString Error)
	{
		if (*bDone)
		{
			return;
		}
		*bDone = true;

		// 즉시 완료되는 핸들러가 대부분이다. 타이머를 회수하지 않으면
		// 액션마다 만료까지 살아남아 자원이 누적된다.
		if (UHermesActionDispatcher* Self = WeakThis.Get())
		{
			if (UWorld* W = Self->GetWorld())
			{
				W->GetTimerManager().ClearTimer(*Th);
			}
		}

		OnResult(HermesJson::MakeActionResult(Id, bOk, Result, Error));
	});

	Chosen->Execute(Payload, OnDone);

	// 응답 타임아웃 폴백 (World 가 있을 때만; 즉시성 핸들러엔 bDone 가드로 무해)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(*Th, [bDone, Id, OnResult]()
		{
			if (*bDone)
			{
				return;
			}
			*bDone = true;
			OnResult(HermesJson::MakeActionResult(Id, false, nullptr, TEXT("timeout")));
		}, Settings->ActionTimeoutSeconds, false);
	}
}
```

> **주의:** `SetTimer`의 첫 인자가 지역 `FTimerHandle Th`에서 `*Th`(공유 핸들)로 바뀌었다. 이것이 회수를 가능하게 하는 핵심이다. 즉시 완료 핸들러는 `ClearTimer`가 아직 설정되지 않은 핸들에 대해 호출되지만 UE에서 이는 안전한 no-op이고, 뒤이어 `SetTimer`가 걸려도 `bDone` 가드 때문에 중복 회신이 나지 않는다.

- [ ] **Step 8: 전체 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 11종 통과.

> 기존 `HermesActionDispatcher.spec.cpp` 테스트가 레이트 리밋에 걸리지 않는지 확인한다. 기본값 20/초이고 그 테스트는 몇 건만 디스패치하므로 통과해야 한다. 만약 실패한다면 그 테스트가 20건을 넘겨 보내는 것이므로, 테스트 건수를 줄이지 말고 왜 그렇게 많은지 먼저 확인한다.

- [ ] **Step 9: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions
git commit -m "feat: 액션 레이트 리밋 도입 및 버려지던 타이머 핸들 회수"
```

커밋 본문에 포함할 내용:

```
틱 예산만으로는 지속 유입을 막지 못한다. 60fps 에서 예산 64 면
초당 3,840 건이 처리 가능하고 각각이 타임아웃 타이머를 만들어
최대 약 57,600 개가 동시에 살아 있을 수 있다.

- 토큰 버킷으로 초당 처리량을 제한하고 초과분은 즉시 거부 회신한다
- FTimerHandle 이 지역 변수로 버려져 취소가 불가능했다. 공유 핸들로
  올려 핸들러 완료 시 ClearTimer 로 회수한다
```

---

## Task 7: 인바운드·아웃바운드 큐 상한과 틱 예산

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp:32-33,115-140`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:108-113`

**Interfaces:**
- Consumes: `FHermesWorkerConfig::MaxInboundQueueSize` / `MaxOutboundQueueSize` (Task 2), `UHermesSettings::MaxInboundFramesPerTick` (Task 1)
- Produces: `FHermesSocketWorker::EnqueueOutbound()` 동작 변경 (상한 도달 시 새 프레임 폐기)

**왜 이 태스크가 필요한가:** `TQueue` 두 개에 크기 제한이 없다. `ReceiveAvailable()`이 계속 수신해 무한 적재하고, 게임 스레드가 한 틱에 큐 전체를 소비하며 각 프레임마다 JSON 파싱과 디스패치를 한다. 설정으로 임의 주소에 붙을 수 있게 되면서 신뢰 경계가 넓어졌으므로 함께 막는다.

- [ ] **Step 1: 워커 헤더에 카운터 추가**

`Transport/HermesSocketWorker.h`의 `TQueue` 멤버 아래에 추가한다.

```cpp
	// TQueue 는 크기 조회를 제공하지 않으므로 카운터를 따로 둔다.
	FThreadSafeCounter InboundCount;
	FThreadSafeCounter OutboundCount;
```

`#include "HAL/ThreadSafeBool.h"` 아래에 추가한다.

```cpp
#include "HAL/ThreadSafeCounter.h"
```

- [ ] **Step 2: 큐 조작부에 카운터 증감 적용**

`Transport/HermesSocketWorker.cpp:32-33`의 두 줄을 교체한다.

```cpp
void FHermesSocketWorker::EnqueueOutbound(const FString& Json)
{
	// 아웃바운드 적체는 피어의 악의가 아니라 연결 단절의 결과다. 끊어봐야
	// 나아지지 않으므로 연결은 유지하고 새 프레임만 버린다.
	// Outbound 는 SPSC 큐라 producer(게임 스레드)가 Dequeue 할 수 없어
	// "가장 오래된 것 버리기"는 성립하지 않는다.
	if (OutboundCount.GetValue() >= Config.MaxOutboundQueueSize)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] outbound queue full (%d), dropping frame"),
			Config.MaxOutboundQueueSize);
		return;
	}
	Outbound.Enqueue(Json);
	OutboundCount.Increment();
}

bool FHermesSocketWorker::DequeueInbound(FString& OutJson)
{
	if (Inbound.Dequeue(OutJson))
	{
		InboundCount.Decrement();
		return true;
	}
	return false;
}
```

`SendAllPending()`의 `while (Outbound.Dequeue(Json))` 루프 본문 **첫 줄**에 감소를 넣는다.

```cpp
	FString Json;
	while (Outbound.Dequeue(Json))
	{
		OutboundCount.Decrement();
		TArray<uint8> Bytes;
```

- [ ] **Step 3: 인바운드 상한 적용**

`ReceiveAvailable()`의 `while (Accumulator.TryPop(Json))` 블록을 교체한다.

```cpp
		FString Json;
		while (Accumulator.TryPop(Json))
		{
			// 정상 서버라면 게임 스레드가 매 틱 비우므로 이 선에 닿지 않는다.
			// 도달했다는 것은 피어가 소비 속도를 무시하고 밀어넣고 있다는 뜻이다.
			// 프레이밍 위반과 같은 경로로 연결을 끊고 백오프 재연결에 맡긴다.
			if (InboundCount.GetValue() >= Config.MaxInboundQueueSize)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Hermes] inbound queue overflow (%d), closing connection"),
					Config.MaxInboundQueueSize);
				return false;
			}
			Inbound.Enqueue(Json);
			InboundCount.Increment();
		}
```

- [ ] **Step 4: 재연결 시 카운터 리셋**

`CloseSocket()`의 `bConnected = false;` 다음 줄에 추가한다.

```cpp
	// 큐를 비우지는 않는다(게임 스레드가 소비 중일 수 있다). 다만 새 연결의
	// Accumulator 리셋과 짝이 맞도록 카운터는 실제 큐 상태를 계속 반영한다.
```

> 카운터는 `Dequeue` 시점에 감소하므로 별도 리셋이 필요 없다. 이 주석은 리셋하지 **않는** 이유를 남기기 위한 것이다.

- [ ] **Step 5: 게임 스레드 틱 예산 적용**

`Connection/HermesConnectionSubsystem.cpp`의 `Tick()`에서 인바운드 소비 루프를 교체한다.

```cpp
	// 한 틱이 무한정 길어지지 않게 예산을 둔다. 남은 프레임은 다음 틱에서
	// 처리되고, 유입이 예산을 계속 초과하면 워커의 큐 상한이 연결을 끊는다.
	int32 Budget = GetDefault<UHermesSettings>()->MaxInboundFramesPerTick;
	FString Json;
	while (Budget-- > 0 && Worker->DequeueInbound(Json))
	{
		TSharedPtr<FJsonObject> Obj;
		if (HermesJson::Parse(Json, Obj)) HandleFrame(Obj);
	}
```

- [ ] **Step 6: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 11종 통과.

> **큐 상한은 자동화 테스트를 만들지 않는다.** `FHermesSocketWorker`가 실제 `FSocket`과 전용 스레드에 묶여 있어 단위 테스트하려면 소켓 추상화 리팩터링이 선행되어야 하고, 그 리팩터링은 이 태스크의 목적과 무관하게 범위를 키운다. Task 26의 통합 검증에서 실제 폭주 시나리오로 확인한다.

- [ ] **Step 7: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection
git commit -m "feat: 인바운드·아웃바운드 큐 상한 및 틱 처리 예산 추가"
```

커밋 본문에 포함할 내용:

```
설정으로 임의 주소에 붙을 수 있게 되면서 신뢰 경계가 넓어졌다.
TQueue 두 개에 상한이 없어 악성 피어가 메모리를 고갈시키거나
한 틱을 통째로 잡아먹게 만들 수 있었다.

- 인바운드: 상한 초과 시 프레이밍 위반과 같은 경로로 연결을 끊고
  백오프 재연결에 맡긴다. 새 상태를 늘리지 않는다
- 아웃바운드: 연결은 유지하고 새 프레임만 버린다. 적체는 피어의
  악의가 아니라 연결 단절의 결과다. SPSC 큐라 producer 가 Dequeue 할
  수 없어 오래된 쪽을 버리는 동작은 성립하지 않는다
- 게임 스레드: 한 틱 소비량을 예산으로 제한하고 나머지는 다음 틱으로 미룬다
```

---

## Task 8: 보류 발화 상한

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesUtil.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesUtil.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesUtil.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:76-88`

**Interfaces:**
- Consumes: `UHermesSettings::MaxPendingChats` (Task 1)
- Produces: `int32 HermesUtil::PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum)`

**왜 이 태스크가 필요한가:** `bIdentified`는 서버가 `identified` 프레임을 보내야만 켜진다. TCP는 받아주면서 `identified`를 보내지 않는 피어에 붙으면 `PendingChats`에 발화가 무한 적재된다. 연결이 끊길 때마다 `bIdentified=false`로 돌아가므로 재연결 실패가 길어져도 같은 일이 생긴다.

- [ ] **Step 1: 헬퍼 헤더 작성**

`Connection/HermesUtil.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"

/** 연결 계층에서 쓰는 작은 순수 헬퍼. */
namespace HermesUtil
{
	/**
	 * Item 을 Array 뒤에 넣되 길이가 MaxNum 을 넘으면 앞에서부터 버린다.
	 * 버린 개수를 반환한다. MaxNum 이 1 미만이면 1로 취급한다.
	 */
	int32 PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum);
}
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Connection/HermesUtil.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Connection/HermesUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesUtilPushBoundedTest,
	"Hermes.Util.PushBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesUtilPushBoundedTest::RunTest(const FString& Parameters)
{
	// 상한 미만에서는 아무것도 버리지 않는다.
	{
		TArray<FString> A;
		TestEqual(TEXT("no drop 1"), HermesUtil::PushBounded(A, TEXT("a"), 3), 0);
		TestEqual(TEXT("no drop 2"), HermesUtil::PushBounded(A, TEXT("b"), 3), 0);
		TestEqual(TEXT("no drop 3"), HermesUtil::PushBounded(A, TEXT("c"), 3), 0);
		TestEqual(TEXT("size 3"), A.Num(), 3);
		TestEqual(TEXT("order kept 0"), A[0], TEXT("a"));
		TestEqual(TEXT("order kept 2"), A[2], TEXT("c"));
	}

	// 상한 도달 후에는 가장 오래된 것부터 버린다. 최신 발화가 살아남아야 한다.
	{
		TArray<FString> A;
		HermesUtil::PushBounded(A, TEXT("a"), 3);
		HermesUtil::PushBounded(A, TEXT("b"), 3);
		HermesUtil::PushBounded(A, TEXT("c"), 3);

		TestEqual(TEXT("drops one"), HermesUtil::PushBounded(A, TEXT("d"), 3), 1);
		TestEqual(TEXT("size still 3"), A.Num(), 3);
		TestEqual(TEXT("oldest dropped"), A[0], TEXT("b"));
		TestEqual(TEXT("newest kept"), A[2], TEXT("d"));
	}

	// 상한이 줄어들면 한 번에 여러 개가 버려지고 그 개수가 반환된다.
	{
		TArray<FString> A;
		for (int32 i = 0; i < 5; ++i)
		{
			HermesUtil::PushBounded(A, FString::Printf(TEXT("%d"), i), 10);
		}
		TestEqual(TEXT("size 5"), A.Num(), 5);

		// 이제 상한 2 로 넣으면 (5+1) - 2 = 4 개가 버려진다.
		TestEqual(TEXT("drops four"), HermesUtil::PushBounded(A, TEXT("new"), 2), 4);
		TestEqual(TEXT("size 2"), A.Num(), 2);
		TestEqual(TEXT("kept newest"), A[1], TEXT("new"));
	}

	// MaxNum 0 이하는 1 로 취급한다.
	{
		TArray<FString> A;
		HermesUtil::PushBounded(A, TEXT("a"), 0);
		HermesUtil::PushBounded(A, TEXT("b"), 0);
		TestEqual(TEXT("clamped to 1"), A.Num(), 1);
		TestEqual(TEXT("newest kept"), A[0], TEXT("b"));
	}

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `HermesUtil::PushBounded` 미정의.

- [ ] **Step 4: 구현 작성**

`Connection/HermesUtil.cpp` 전체 내용:

```cpp
#include "Connection/HermesUtil.h"

namespace HermesUtil
{
	int32 PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum)
	{
		Array.Add(Item);

		const int32 Cap = FMath::Max(1, MaxNum);
		int32 Dropped = 0;
		while (Array.Num() > Cap)
		{
			// 대화 맥락상 최신 발화가 살아남는 편이 자연스럽다.
			Array.RemoveAt(0, 1, EAllowShrinking::No);
			++Dropped;
		}
		return Dropped;
	}
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Util; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.Util.PushBounded` PASS.

- [ ] **Step 6: `SendChat()`에 상한 적용**

`Connection/HermesConnectionSubsystem.cpp`의 include에 추가:

```cpp
#include "Connection/HermesUtil.h"
```

`SendChat()`을 교체한다.

```cpp
void UHermesConnectionSubsystem::SendChat(const FString& Text)
{
	const FString Id = FString::Printf(TEXT("c-%04d"), ++ChatCounter);
	const FString Json = HermesJson::MakeChat(Id, Text);

	if (bIdentified)
	{
		SendJson(Json);
		return;
	}

	// identified 를 영영 보내지 않는 피어에 붙으면 여기가 무한히 자란다.
	const int32 Dropped = HermesUtil::PushBounded(
		PendingChats, Json, GetDefault<UHermesSettings>()->MaxPendingChats);
	if (Dropped > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] pending chat overflow, dropped %d oldest"), Dropped);
	}
}
```

- [ ] **Step 7: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 12종 통과.

- [ ] **Step 8: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection
git commit -m "feat: identified 이전 보류 발화에 상한 추가"
```

커밋 본문에 포함할 내용:

```
bIdentified 는 서버가 identified 프레임을 보내야만 켜진다. TCP 는
받아주면서 identified 를 보내지 않는 피어에 붙으면 PendingChats 가
무한히 자랐다. 연결이 끊길 때마다 bIdentified 가 false 로 돌아가므로
재연결 실패가 길어져도 같은 일이 생긴다.

가득 차면 가장 오래된 것부터 버린다. 어차피 서버에 닿지 못한
발화들이고 대화 맥락상 최신 발화가 살아남는 편이 자연스럽다.
PendingChats 는 게임 스레드 전용 TArray 라 이 동작이 가능하다.
```

---

# Phase 3 — 프로토콜 v2 (신원 발급과 세션 운영)

> **이 Phase부터 서버와의 계약이 바뀐다.** Task 9~13을 마치면 클라이언트는 v1 서버와 통신할 수 없다. 서버 재구축이 병행되지 않는 동안에는 이 Phase의 브랜치를 머지하지 말고, 6절 수동 검증은 v2 스텁 서버로 수행한다.

## Task 9: 프로토콜 v2 메시지 빌더와 파서

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol/HermesMessages.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol/HermesMessages.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol/HermesMessages.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesSaveGame.h`

**Interfaces:**
- Consumes: 없음 (순수 JSON 계층)
- Produces:
  - `HermesMsg::ChatDelta` — `TEXT("chat_delta")`
  - `FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& SessionToken, const FString& PlayerName)`
  - `FString HermesJson::MakePing(const FString& Id)`
  - `bool HermesJson::ParseIdentified(const TSharedPtr<FJsonObject>& Obj, FString& OutPlayerId, FString& OutToken, FString& OutChatId)`
  - `UHermesSaveGame::SessionToken`

**왜 이 태스크가 필요한가:** 현재 identify는 `player_id`만 보내면 즉시 그 플레이어로 인정된다. `player_id`는 클라이언트가 `FGuid::NewGuid()`로 직접 만들므로 세이브 파일을 훔칠 필요도 없이 **임의 UUID를 타이핑하면 남의 세션에 접근된다.** 신원을 서버가 발급하도록 뒤집는 첫 단계다.

- [ ] **Step 1: SaveGame에 토큰 필드 추가**

`HermesSaveGame.h`의 `PlayerId` 아래에 추가한다.

```cpp
	/**
	 * 서버가 발급한 세션 자격 증명을 난독화해 보관한다.
	 *
	 * FAES 로 감싸지만 키가 바이너리 안에 있으므로 이것은 암호화가 아니라
	 * 난독화다. 기기 소유자로부터는 보호되지 않으며 그럴 방법도 없다.
	 * 막는 것은 "세이브 파일을 그대로 복사해 남에게 넘기는" 수준까지다.
	 * 진짜 대책은 서버측 단기 토큰·세션 바인딩이며 설계 문서 10.2 에 있다.
	 *
	 * 평문 접근은 GetSessionToken()/SetSessionToken() 으로만 한다.
	 */
	UPROPERTY()
	TArray<uint8> ObfuscatedSessionToken;
```

`HermesSaveGame.h`의 `UPROPERTY` 아래에 접근자를 선언한다.

```cpp
	/** 난독화를 풀어 평문 토큰을 돌려준다. 없으면 빈 문자열. */
	FString GetSessionToken() const;

	/** 평문 토큰을 난독화해 보관한다. 빈 문자열이면 저장분을 비운다. */
	void SetSessionToken(const FString& PlainToken);
```

`HermesSaveGame.cpp` 를 신규 작성한다.

```cpp
#include "HermesSaveGame.h"
#include "Misc/AES.h"

namespace
{
	/**
	 * 난독화 키. 바이너리 안에 있으므로 비밀이 아니다 — 이 값이 노출되는 것은
	 * 설계상 전제이고 위협 모델에 변화를 주지 않는다.
	 * FAES::AESBlockSize 배수 길이여야 한다.
	 */
	const ANSICHAR* ObfuscationKey = "HermesAgentNPCLocalObfuscationKey";

	void MakeKey(FAES::FAESKey& OutKey)
	{
		FMemory::Memzero(OutKey.Key, FAES::FAESKey::KeySize);
		const int32 Len = FMath::Min<int32>(
			FCStringAnsi::Strlen(ObfuscationKey), FAES::FAESKey::KeySize);
		FMemory::Memcpy(OutKey.Key, ObfuscationKey, Len);
	}
}

FString UHermesSaveGame::GetSessionToken() const
{
	if (ObfuscatedSessionToken.Num() == 0)
	{
		return FString();
	}

	TArray<uint8> Buffer = ObfuscatedSessionToken;
	FAES::FAESKey Key;
	MakeKey(Key);
	FAES::DecryptData(Buffer.GetData(), Buffer.Num(), Key);

	// 앞 4바이트에 원본 길이를 담아 블록 패딩을 걷어낸다.
	if (Buffer.Num() < 4)
	{
		return FString();
	}
	const int32 PlainLen =
		(int32)Buffer[0] | ((int32)Buffer[1] << 8) | ((int32)Buffer[2] << 16) | ((int32)Buffer[3] << 24);
	if (PlainLen < 0 || PlainLen > Buffer.Num() - 4)
	{
		// 손상되었거나 다른 키로 쓰인 데이터. 새 신원을 발급받게 둔다.
		return FString();
	}

	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + 4), PlainLen);
	return FString(FStringView(Conv.Get(), Conv.Length()));
}

void UHermesSaveGame::SetSessionToken(const FString& PlainToken)
{
	ObfuscatedSessionToken.Reset();
	if (PlainToken.IsEmpty())
	{
		return;
	}

	FTCHARToUTF8 Utf8(*PlainToken);
	const int32 PlainLen = Utf8.Length();

	// [4바이트 길이][UTF-8 바디][블록 크기 패딩]
	const int32 Unpadded = 4 + PlainLen;
	const int32 Padded = ((Unpadded + FAES::AESBlockSize - 1) / FAES::AESBlockSize) * FAES::AESBlockSize;

	ObfuscatedSessionToken.SetNumZeroed(Padded);
	ObfuscatedSessionToken[0] = (uint8)(PlainLen & 0xFF);
	ObfuscatedSessionToken[1] = (uint8)((PlainLen >> 8) & 0xFF);
	ObfuscatedSessionToken[2] = (uint8)((PlainLen >> 16) & 0xFF);
	ObfuscatedSessionToken[3] = (uint8)((PlainLen >> 24) & 0xFF);
	FMemory::Memcpy(ObfuscatedSessionToken.GetData() + 4, Utf8.Get(), PlainLen);

	FAES::FAESKey Key;
	MakeKey(Key);
	FAES::EncryptData(ObfuscatedSessionToken.GetData(), ObfuscatedSessionToken.Num(), Key);
}
```

> **이것이 보안이 아님을 코드와 문서 양쪽에 남긴다.** 키가 바이너리에 있으므로 기기 소유자는 언제든 복호화할 수 있다. 이 조치의 유일한 효과는 세이브 파일을 통째로 복사해 남에게 넘기거나 텍스트 에디터로 토큰을 긁어가는 경로를 막는 것이다. Task 18의 README 보안 모델 표에 이 한계를 명시한다.

- [ ] **Step 2: 메시지 헤더 갱신**

`Protocol/HermesMessages.h`의 `HermesMsg` 네임스페이스에서 `Error` 아래에 추가한다.

```cpp
	inline const FString ChatDelta     = TEXT("chat_delta");
```

`HermesJson` 네임스페이스에서 기존 `MakeIdentify` 선언을 교체하고 두 함수를 추가한다.

```cpp
	/**
	 * v2 identify 프레임. PlayerId/SessionToken 이 비어 있으면 신규 발급 요청으로
	 * 해석되어 해당 필드를 싣지 않는다. protocol_version 은 항상 2로 보낸다.
	 */
	FString MakeIdentify(const FString& PlayerId, const FString& SessionToken,
	                     const FString& PlayerName);

	FString MakePing(const FString& Id);

	/**
	 * identified 프레임에서 자격 증명을 꺼낸다.
	 * session_token 이 없으면 v1 서버로 판단하고 false 를 반환한다.
	 * true 일 때만 Out 인자가 채워진다.
	 */
	bool ParseIdentified(const TSharedPtr<FJsonObject>& Obj,
	                     FString& OutPlayerId, FString& OutToken, FString& OutChatId);
```

- [ ] **Step 3: 실패하는 테스트 작성**

`Protocol/HermesMessages.spec.cpp` **끝에** 아래 테스트들을 덧붙인다. 기존 테스트에 `MakeIdentify(PlayerId, PlayerName)` 2인자 호출이 있다면 3인자 형태로 고친다 — 이는 회귀가 아니라 의도된 프로토콜 변경이다.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesIdentifyV2Test,
	"Hermes.Protocol.Messages.IdentifyV2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesIdentifyV2Test::RunTest(const FString& Parameters)
{
	// 자격 증명이 없으면 신규 발급 요청: player_id / session_token 을 싣지 않는다.
	{
		const FString Json = HermesJson::MakeIdentify(FString(), FString(), TEXT("Aria"));
		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

		double Ver = 0;
		TestTrue(TEXT("has version"), Obj->TryGetNumberField(TEXT("protocol_version"), Ver));
		TestEqual(TEXT("version is 2"), (int32)Ver, 2);

		TestFalse(TEXT("no player_id"), Obj->HasField(TEXT("player_id")));
		TestFalse(TEXT("no session_token"), Obj->HasField(TEXT("session_token")));

		FString Name;
		TestTrue(TEXT("has player_name"), Obj->TryGetStringField(TEXT("player_name"), Name));
		TestEqual(TEXT("player_name value"), Name, TEXT("Aria"));
	}

	// 자격 증명이 있으면 재접속 요청: 둘 다 싣는다.
	{
		const FString Json = HermesJson::MakeIdentify(TEXT("pid-1"), TEXT("tok-1"), FString());
		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

		FString Pid, Tok;
		TestTrue(TEXT("has player_id"), Obj->TryGetStringField(TEXT("player_id"), Pid));
		TestTrue(TEXT("has session_token"), Obj->TryGetStringField(TEXT("session_token"), Tok));
		TestEqual(TEXT("player_id value"), Pid, TEXT("pid-1"));
		TestEqual(TEXT("session_token value"), Tok, TEXT("tok-1"));

		// 이름이 비면 필드를 넣지 않는다 (기존 동작 유지).
		TestFalse(TEXT("no empty player_name"), Obj->HasField(TEXT("player_name")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesParseIdentifiedTest,
	"Hermes.Protocol.Messages.ParseIdentified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesParseIdentifiedTest::RunTest(const FString& Parameters)
{
	// v2 응답: 자격 증명이 정확히 추출된다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"player_id\":\"pid-9\",")
			TEXT("\"session_token\":\"tok-9\",\"chat_id\":\"chat-9\"}"), Obj);

		FString Pid, Tok, Chat;
		TestTrue(TEXT("v2 recognized"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
		TestEqual(TEXT("player_id"), Pid, TEXT("pid-9"));
		TestEqual(TEXT("session_token"), Tok, TEXT("tok-9"));
		TestEqual(TEXT("chat_id"), Chat, TEXT("chat-9"));
	}

	// v1 응답: session_token 이 없으므로 false. 이것이 조용한 불일치를 막는 판정이다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"chat_id\":\"chat-1\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("v1 rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// 토큰이 빈 문자열이어도 v1 과 동일하게 거부한다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"player_id\":\"p\",\"session_token\":\"\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("empty token rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// player_id 가 없으면 거부한다. 둘 다 있어야 재접속에 쓸 수 있다.
	{
		TSharedPtr<FJsonObject> Obj;
		HermesJson::Parse(
			TEXT("{\"type\":\"identified\",\"ok\":true,\"session_token\":\"t\"}"), Obj);

		FString Pid, Tok, Chat;
		TestFalse(TEXT("missing player_id rejected"), HermesJson::ParseIdentified(Obj, Pid, Tok, Chat));
	}

	// null 오브젝트에도 크래시하지 않는다.
	{
		TSharedPtr<FJsonObject> Null;
		FString Pid, Tok, Chat;
		TestFalse(TEXT("null safe"), HermesJson::ParseIdentified(Null, Pid, Tok, Chat));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesPingTest,
	"Hermes.Protocol.Messages.Ping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesPingTest::RunTest(const FString& Parameters)
{
	const FString Json = HermesJson::MakePing(TEXT("k-1"));
	TSharedPtr<FJsonObject> Obj;
	TestTrue(TEXT("parses"), HermesJson::Parse(Json, Obj));

	FString Type, Id;
	TestTrue(TEXT("has type"), Obj->TryGetStringField(TEXT("type"), Type));
	TestEqual(TEXT("type is ping"), Type, TEXT("ping"));
	TestTrue(TEXT("has id"), Obj->TryGetStringField(TEXT("id"), Id));
	TestEqual(TEXT("id value"), Id, TEXT("k-1"));

	return true;
}
```

- [ ] **Step 4: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 컴파일 실패. `MakeIdentify` 인자 개수 불일치, `MakePing` / `ParseIdentified` 미정의.

- [ ] **Step 5: 구현 작성**

`Protocol/HermesMessages.cpp`의 `MakeIdentify`를 교체하고 두 함수를 추가한다.

```cpp
FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& SessionToken,
	const FString& PlayerName)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Identify);
	// 버전을 항상 실어 v1 서버와의 조용한 불일치를 막는다.
	O->SetNumberField(TEXT("protocol_version"), 2);

	// 둘 중 하나라도 비면 신규 발급 요청으로 취급한다. 반쪽 자격 증명을
	// 보내면 서버가 not_authorized 로 끊어 재발급 경로가 막힌다.
	if (!PlayerId.IsEmpty() && !SessionToken.IsEmpty())
	{
		O->SetStringField(TEXT("player_id"), PlayerId);
		O->SetStringField(TEXT("session_token"), SessionToken);
	}
	if (!PlayerName.IsEmpty())
	{
		O->SetStringField(TEXT("player_name"), PlayerName);
	}
	return Serialize(O);
}

FString HermesJson::MakePing(const FString& Id)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Ping);
	if (!Id.IsEmpty())
	{
		O->SetStringField(TEXT("id"), Id);
	}
	return Serialize(O);
}

bool HermesJson::ParseIdentified(const TSharedPtr<FJsonObject>& Obj,
	FString& OutPlayerId, FString& OutToken, FString& OutChatId)
{
	if (!Obj.IsValid())
	{
		return false;
	}

	FString Pid, Tok;
	// 둘 다 있어야 재접속에 쓸 수 있다. 하나라도 없으면 v1 서버로 판단한다.
	if (!Obj->TryGetStringField(TEXT("player_id"), Pid) || Pid.IsEmpty())
	{
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("session_token"), Tok) || Tok.IsEmpty())
	{
		return false;
	}

	OutPlayerId = Pid;
	OutToken    = Tok;
	Obj->TryGetStringField(TEXT("chat_id"), OutChatId);
	return true;
}
```

- [ ] **Step 6: 기존 호출부 컴파일 오류 수정**

`Connection/HermesConnectionSubsystem.cpp`의 `SendIdentify()`가 2인자 `MakeIdentify`를 호출한다. Task 10에서 제대로 고치되, 지금은 빌드를 통과시키기 위해 임시로 3인자 형태로 바꾼다.

```cpp
void UHermesConnectionSubsystem::SendIdentify()
{
	// Task 10 에서 SessionToken 을 싣도록 완성한다.
	SendJson(HermesJson::MakeIdentify(PlayerId, FString(), FString()));
}
```

- [ ] **Step 7: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 15종 통과 (기존 12 + IdentifyV2 + ParseIdentified + Ping).

- [ ] **Step 8: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesSaveGame.h Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection
git commit -m "feat: 프로토콜 v2 identify/identified 메시지 계층"
```

커밋 본문에 포함할 내용:

```
identify 에 protocol_version 과 session_token 을 싣고, identified 에서
서버 발급 자격 증명을 꺼내는 파서를 추가한다. session_token 이 없으면
v1 서버로 판단해 false 를 반환한다 — 조용한 버전 불일치를 막는 판정이다.

자격 증명이 반쪽이면(한쪽만 있으면) 신규 발급 요청으로 취급한다.
반쪽을 보내면 서버가 not_authorized 로 끊어 재발급 경로가 막힌다.

chat_delta 타입 상수와 클라이언트 발신 ping 빌더도 함께 추가한다.
```

---

## Task 10: 서버 발급 신원 흐름

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp:20,45-55,71-74,117-127`

**Interfaces:**
- Consumes: `HermesJson::MakeIdentify` / `ParseIdentified` (Task 9), `UHermesSettings::SaveSlotName` (Task 1)
- Produces: `UHermesConnectionSubsystem::LoadCredentials()` / `SaveCredentials()` (private)

**왜 이 태스크가 필요한가:** `FGuid::NewGuid()`로 클라이언트가 자기 신원을 주장하는 구조를 제거한다. 이것을 하지 않으면 Task 9의 토큰은 형식만 갖춘 장식이 된다.

- [ ] **Step 1: 헤더 갱신**

`Connection/HermesConnectionSubsystem.h`의 `private:` 섹션에서 `FString LoadOrCreatePlayerId();` 를 교체한다.

```cpp
	/** SaveGame 에서 자격 증명을 읽는다. 없으면 PlayerId/SessionToken 이 빈 채로 남는다. */
	void LoadCredentials();

	/** 서버가 발급한 자격 증명을 SaveGame 에 저장한다. 실패해도 연결은 유지한다. */
	void SaveCredentials();
```

멤버 `FString PlayerId;` 아래에 추가한다.

```cpp
	FString SessionToken;
```

- [ ] **Step 2: 자격 증명 로드/저장 구현**

`Connection/HermesConnectionSubsystem.cpp`의 `LoadOrCreatePlayerId()` 전체를 아래 두 함수로 교체한다.

```cpp
void UHermesConnectionSubsystem::LoadCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	// 신원은 서버가 발급한다. 여기서 만들어내지 않는다 — 클라이언트가 자기
	// 신원을 주장하는 구조면 임의 UUID 로 남의 세션에 접근할 수 있다.
	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		PlayerId     = SG->PlayerId;
		SessionToken = SG->GetSessionToken();   // 난독화 해제

		// 토큰만 손상되었다면 반쪽 자격 증명으로 접속을 시도하지 않는다.
		// MakeIdentify 가 둘 다 있어야 실어 보내므로 자연히 재발급 경로가 된다.
		if (PlayerId.IsEmpty() || SessionToken.IsEmpty())
		{
			PlayerId.Reset();
			SessionToken.Reset();
		}
	}
}

void UHermesConnectionSubsystem::SaveCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	UHermesSaveGame* SG = Cast<UHermesSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	if (!SG)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] failed to create save game object"));
		return;
	}
	SG->PlayerId = PlayerId;
	SG->SetSessionToken(SessionToken);   // 난독화해 보관

	if (!UGameplayStatics::SaveGameToSlot(SG, SaveSlot, 0))
	{
		// 다음 실행에서 새 신원을 발급받게 된다. 데이터 손실이지 보안 결함은 아니다.
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] failed to save credentials to slot '%s'"), *SaveSlot);
	}
}
```

- [ ] **Step 3: `Initialize()`에서 호출 변경**

`Initialize()`의 `PlayerId = LoadOrCreatePlayerId();` 를 교체한다.

```cpp
	LoadCredentials();
```

- [ ] **Step 4: `SendIdentify()` 완성**

Task 9 Step 6에서 임시로 둔 구현을 교체한다.

```cpp
void UHermesConnectionSubsystem::SendIdentify()
{
	// 자격 증명이 없으면 MakeIdentify 가 신규 발급 요청 형태로 만든다.
	SendJson(HermesJson::MakeIdentify(PlayerId, SessionToken, FString()));
}
```

- [ ] **Step 5: 워커에 재연결 요청 진입점 추가**

v1 서버를 감지했을 때 연결을 끊고 재연결 루프로 돌려보낼 수단이 필요하다. Task 12의 사망 판정도 같은 진입점을 쓴다.

`Transport/HermesSocketWorker.h`의 `public:` 섹션에서 `void RequestStop();` 아래에 추가한다.

```cpp
	/** 현재 연결을 끊고 재연결 루프로 돌아가게 한다. 게임 스레드에서 호출. */
	void RequestReconnect();
```

`private:` 섹션의 `FThreadSafeBool bConnected = false;` 아래에 추가한다.

```cpp
	FThreadSafeBool bReconnectRequested = false;
```

`Transport/HermesSocketWorker.cpp`의 `void FHermesSocketWorker::RequestStop()` 아래에 정의를 추가한다.

```cpp
void FHermesSocketWorker::RequestReconnect() { bReconnectRequested = true; }
```

`Run()`의 메인 루프에서 송수신 직전에 플래그를 확인한다. `if (!SendAllPending() || !ReceiveAvailable())` **바로 앞**에 넣는다.

```cpp
		if (bReconnectRequested)
		{
			bReconnectRequested = false;
			CloseSocket();       // 다음 루프에서 재연결
			continue;
		}
```

- [ ] **Step 6: `identified` 처리에 발급·검증 분기 추가**

`HandleFrame()`의 `if (Type == HermesMsg::Identified)` 블록을 교체한다.

```cpp
	if (Type == HermesMsg::Identified)
	{
		FString NewPid, NewTok, ChatId;
		if (!HermesJson::ParseIdentified(Obj, NewPid, NewTok, ChatId))
		{
			// v1 호환 모드를 두지 않는다. 호환 모드는 곧 "인증 없이도 동작하는
			// 경로"라 신원 발급의 목적을 무력화한다. 조용히 동작하느니 크게 실패한다.
			UE_LOG(LogTemp, Error,
				TEXT("[Hermes] server did not return session credentials. "
				     "This client requires protocol v2. Closing connection."));
			if (Worker)
			{
				Worker->RequestReconnect();
			}
			return;
		}

		// 최초 발급이거나 서버가 값을 바꿔 준 경우에만 저장한다.
		if (NewPid != PlayerId || NewTok != SessionToken)
		{
			PlayerId     = NewPid;
			SessionToken = NewTok;
			SaveCredentials();
		}

		bIdentified = true;
		FlushPendingChats();
		OnConnectionStateChanged.Broadcast(true);
	}
```

- [ ] **Step 7: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 15종 통과.

- [ ] **Step 8: `FGuid::NewGuid()`가 사라졌는지 확인**

```powershell
Select-String -Path "Plugins\HermesAgentNPC\Source\HermesAgentNPC\Connection\*" -Pattern "NewGuid"
```

Expected: 출력 없음.

- [ ] **Step 9: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport
git commit -m "feat: 신원을 서버 발급으로 전환하고 클라이언트 UUID 생성 제거"
```

커밋 본문에 포함할 내용:

```
클라이언트가 FGuid::NewGuid() 로 player_id 를 만들어 주장하던 구조를
제거한다. 세이브 파일을 훔칠 필요도 없이 임의 UUID 를 보내면 남의
세션에 접근되던 경로가 닫힌다.

자격 증명이 없으면 빈 채로 identify 를 보내 서버 발급을 받고,
있으면 (player_id, session_token) 쌍으로 재접속을 증명한다.
저장 실패는 경고만 남기고 연결을 유지한다 — 다음 실행에서 새 신원을
발급받게 되며 이는 데이터 손실이지 보안 결함이 아니다.

v1 호환 모드는 두지 않는다. 호환 모드는 곧 인증 없이 동작하는
경로이므로 이 변경의 목적을 무력화한다.
```

---

## Task 11: 스트리밍 응답 (`chat_delta`)

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp` (`HandleFrame`)
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI/HermesDialogueWidget.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI/HermesDialogueWidget.cpp`

**Interfaces:**
- Consumes: `HermesMsg::ChatDelta` (Task 9)
- Produces:
  - `UHermesConnectionSubsystem::OnChatDelta` — `DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatDelta, const FString& /*Text*/, const FString& /*Id*/)`

**왜 이 태스크가 필요한가:** 프로토콜에 부분 응답 개념이 없어 `chat_response`가 통째로 도착한다. 로컬 GPU 추론은 정상적으로도 수 초가 걸리므로 NPC가 그동안 침묵한다. llama-server는 SSE 스트리밍을 지원하므로 병목은 추론이 아니라 이 구간의 프로토콜이다. 또한 Task 13의 대화 타임아웃이 "느린 것"과 "죽은 것"을 구분하려면 진행 신호가 필요하다.

- [ ] **Step 1: 델리게이트 선언 추가**

`Connection/HermesConnectionSubsystem.h`의 `DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, bool);` 아래에 추가한다.

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatDelta, const FString& /*Text*/, const FString& /*Id*/);
```

`public:` 섹션의 `FOnConnectionStateChanged OnConnectionStateChanged;` 아래에 추가한다.

```cpp
	/** 부분 응답. 표시용 힌트이며 정본은 OnChatResponse 가 전달하는 최종 텍스트다. */
	FOnChatDelta OnChatDelta;
```

- [ ] **Step 2: `HandleFrame()`에 `chat_delta` 분기 추가**

`Connection/HermesConnectionSubsystem.cpp`의 `HandleFrame()`에서 `else if (Type == HermesMsg::ChatResponse)` 블록 **바로 앞**에 추가한다.

```cpp
	else if (Type == HermesMsg::ChatDelta)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		OnChatDelta.Broadcast(Text, Id);
	}
```

> `seq` 필드는 읽지 않는다. 델타는 표시용이고 최종 텍스트가 정본이라 순번 검증이 없어도 화면이 자기 교정된다. 순번을 쓰기 시작하면 유실·재정렬 처리 로직이 따라붙는데 그만한 이득이 없다.

- [ ] **Step 3: 위젯 헤더에 누적 상태와 핸들러 추가**

`UI/HermesDialogueWidget.h`의 `private:` 섹션에서 `void HandleChatResponse(...)` 위에 추가한다.

```cpp
	void HandleChatDelta(const FString& Text, const FString& Id);
```

멤버 `FDelegateHandle ChatHandle;` 아래에 추가한다.

```cpp
	FDelegateHandle DeltaHandle;

	/** 현재 표시 중인 응답의 누적 텍스트. chat_response 가 오면 정본으로 교체된다. */
	FString StreamingText;
```

- [ ] **Step 4: 위젯 구현 — 구독/해제와 누적 표시**

`UI/HermesDialogueWidget.cpp`의 `OpenFor()`에서 기존 구독 아래에 델타 구독을 추가한다.

```cpp
	if (Connection)
	{
		ChatHandle  = Connection->OnChatResponse.AddUObject(this, &UHermesDialogueWidget::HandleChatResponse);
		DeltaHandle = Connection->OnChatDelta.AddUObject(this, &UHermesDialogueWidget::HandleChatDelta);
		StateHandle = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
```

`NativeDestruct()`에 해제를 추가한다.

```cpp
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
		Connection->OnChatDelta.Remove(DeltaHandle);
		Connection->OnConnectionStateChanged.Remove(StateHandle);
	}
```

`OnSendClicked()`에서 누적 버퍼를 비운다.

```cpp
	Connection->SendChat(Text);
	InputBox->SetText(FText::GetEmpty());
	StreamingText.Reset();
	if (DialogueText)
	{
		DialogueText->SetText(FText::FromString(TEXT("생각 중...")));
	}
```

델타 핸들러를 추가하고, 최종 응답 핸들러가 정본으로 덮도록 바꾼다.

```cpp
void UHermesDialogueWidget::HandleChatDelta(const FString& Text, const FString& Id)
{
	// 누적만 하고 위젯은 건드리지 않는다. 한 틱에 델타가 여러 개 들어오면
	// SetText 를 그 횟수만큼 부르게 되는데, 마지막 한 번만 화면에 의미가 있고
	// 나머지는 Slate 텍스트 레이아웃을 헛되이 무효화한다.
	// 실제 갱신은 NativeTick 에서 틱당 1회만 수행한다.
	StreamingText += Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)
{
	// 델타를 놓치거나 중복 처리했더라도 여기서 정본으로 교체되어 자기 교정된다.
	StreamingText = Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bStreamingDirty && DialogueText)
	{
		DialogueText->SetText(FText::FromString(StreamingText));
		bStreamingDirty = false;
	}
}
```

헤더에 추가할 것:

```cpp
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
```

```cpp
	/** 이번 틱에 표시를 갱신해야 하는지. 틱당 SetText 1회로 묶기 위한 플래그. */
	bool bStreamingDirty = false;
```

> **왜 틱당 1회로 묶는가.** 워커는 이미 `TQueue` 로 프레임을 넘기고 게임 스레드가 `Tick()` 에서 예산(기본 64)만큼 소비한다 — 프레임마다 `AsyncTask(GameThread)` 를 부르는 구조가 아니므로 스레드 왕복 비용은 없다. 남는 비용은 순전히 **위젯 갱신 횟수**다. 델타 64개가 한 틱에 들어오면 `SetText` 가 64번 불리고 그때마다 Slate 가 텍스트 레이아웃을 다시 계산한다. 누적은 문자열 연산이라 싸고, 화면 갱신만 마지막에 한 번 하면 된다.

> `Id` 인자는 이 태스크에서 아직 쓰지 않는다. Task 13에서 상관 규칙을 넣을 때 사용한다.

- [ ] **Step 5: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 15종 통과.

> `chat_delta` 처리 자체는 델리게이트 브로드캐스트뿐이라 별도 자동화 테스트를 만들지 않는다. 프레임 파싱은 이미 `HermesFrameCodec` / `HermesMessages` 테스트가 덮고 있고, 누적 표시는 위젯 상태라 Task 26의 수동 검증에서 확인한다.

- [ ] **Step 6: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI
git commit -m "feat: chat_delta 스트리밍 응답 수신 및 누적 표시"
```

커밋 본문에 포함할 내용:

```
프로토콜에 부분 응답 개념이 없어 응답이 통째로 도착했다. 로컬 GPU
추론은 정상적으로도 수 초가 걸려 그동안 NPC 가 침묵한다.

델타는 표시용 힌트이고 chat_response.text 가 정본이다. 델타를
놓치거나 중복 처리해도 최종 응답이 덮어써 자기 교정되며, 델타를
무시하는 구현도 그대로 동작한다. seq 는 읽지 않는다 — 순번을 쓰면
유실·재정렬 처리가 따라붙는데 그만한 이득이 없다.
```

---

## Task 12: 연결 유지와 죽은 연결 탐지

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesLiveness.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesLiveness.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesLiveness.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp` (`Tick`, `SendJson`, `HandleFrame`)
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp` (`ConnectSocket`)

**Interfaces:**
- Consumes: `FHermesSocketWorker::RequestReconnect()` (Task 10), `HermesJson::MakePing` (Task 9), `UHermesSettings::KeepAlivePingIntervalSeconds` / `PeerTimeoutSeconds` (Task 1)
- Produces:
  - `enum class HermesLiveness::EDecision : uint8 { Nothing, SendPing, DeclareDead }`
  - `EDecision HermesLiveness::Evaluate(double Now, double LastRecvTime, double LastSendTime, float PingInterval, float PeerTimeout)`

**왜 이 태스크가 필요한가:** 소켓에 keepalive 설정이 없고 클라이언트가 `ping`을 보내지 않는다 — 서버 ping에 pong으로 답하기만 한다. Wi-Fi 전환·NAT 타임아웃처럼 조용히 끊기면 `bConnected`가 계속 `true`로 남고, 실패가 **다음 송신 시점까지** 미뤄진다. 송신은 플레이어가 말을 걸 때 일어나므로 재연결이 가장 나쁜 타이밍에 시작된다.

- [ ] **Step 1: 판정 로직 헤더 작성**

`Connection/HermesLiveness.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 연결 생존 판정.
 * 시간을 인자로 주입받는 순수 로직이라 단독 테스트가 가능하다.
 * 프레임 종류를 아는 게임 스레드에서 호출하고, 워커는 프로토콜을 모르는 상태로 둔다.
 */
namespace HermesLiveness
{
	enum class EDecision : uint8
	{
		Nothing,
		SendPing,
		DeclareDead
	};

	/**
	 * LastRecvTime 은 종류를 가리지 않는 마지막 수신 시각이다. pong 뿐 아니라
	 * 모든 수신이 생존 신호이므로 대화가 활발하면 ping 없이도 판정이 성립한다.
	 *
	 * 경계는 모두 이상(>=)으로 판정한다. 정확히 PeerTimeout 이면 DeclareDead,
	 * 정확히 PingInterval 이면 SendPing 이다.
	 * 죽은 연결에 ping 을 보내지 않도록 DeclareDead 가 우선한다.
	 */
	EDecision Evaluate(double Now, double LastRecvTime, double LastSendTime,
	                   float PingInterval, float PeerTimeout);
}
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Connection/HermesLiveness.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Connection/HermesLiveness.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesLivenessTest,
	"Hermes.Liveness.Evaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesLivenessTest::RunTest(const FString& Parameters)
{
	using HermesLiveness::EDecision;
	const float PingInterval = 20.f;
	const float PeerTimeout  = 60.f;

	// 최근 수신·송신이 모두 있으면 아무것도 하지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("idle => Nothing"), D == EDecision::Nothing);
	}

	// 송신 침묵이 PingInterval 을 넘으면 ping 을 보낸다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 95.0, /*LastSend*/ 79.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("send silence => SendPing"), D == EDecision::SendPing);
	}

	// 수신 침묵이 PeerTimeout 을 넘으면 죽은 것으로 판정한다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 39.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("recv silence => DeclareDead"), D == EDecision::DeclareDead);
	}

	// 둘 다 넘었으면 DeclareDead 가 우선한다. 죽은 연결에 ping 을 보내지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 30.0, /*LastSend*/ 30.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("both => DeclareDead wins"), D == EDecision::DeclareDead);
	}

	// 경계: 정확히 PingInterval 이면 SendPing.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 80.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("exactly ping interval => SendPing"), D == EDecision::SendPing);
	}

	// 경계: PingInterval 직전이면 Nothing.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 99.0, /*LastSend*/ 80.1, PingInterval, PeerTimeout);
		TestTrue(TEXT("just under ping interval => Nothing"), D == EDecision::Nothing);
	}

	// 경계: 정확히 PeerTimeout 이면 DeclareDead.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 40.0, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("exactly peer timeout => DeclareDead"), D == EDecision::DeclareDead);
	}

	// 경계: PeerTimeout 직전이면 죽지 않는다.
	{
		const EDecision D = HermesLiveness::Evaluate(
			/*Now*/ 100.0, /*LastRecv*/ 40.1, /*LastSend*/ 99.0, PingInterval, PeerTimeout);
		TestTrue(TEXT("just under peer timeout => Nothing"), D == EDecision::Nothing);
	}

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `HermesLiveness::Evaluate` 미정의.

- [ ] **Step 4: 구현 작성**

`Connection/HermesLiveness.cpp` 전체 내용:

```cpp
#include "Connection/HermesLiveness.h"

namespace HermesLiveness
{
	EDecision Evaluate(double Now, double LastRecvTime, double LastSendTime,
		float PingInterval, float PeerTimeout)
	{
		// 죽은 연결에 ping 을 보내봐야 의미가 없으므로 사망 판정을 먼저 한다.
		if (Now - LastRecvTime >= (double)PeerTimeout)
		{
			return EDecision::DeclareDead;
		}
		if (Now - LastSendTime >= (double)PingInterval)
		{
			return EDecision::SendPing;
		}
		return EDecision::Nothing;
	}
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Liveness; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.Liveness.Evaluate` PASS.

- [ ] **Step 6: 구독 시스템에 시각 추적 멤버 추가**

`Connection/HermesConnectionSubsystem.h`의 `private:` 섹션에서 `int32 ChatCounter = 0;` 아래에 추가한다.

```cpp
	int32 PingCounter = 0;

	// 종류를 가리지 않는 마지막 수신/송신 시각. 연결 성립 시점에 초기화된다.
	double LastRecvTime = 0.0;
	double LastSendTime = 0.0;
```

- [ ] **Step 7: 송수신 시각 갱신**

`Connection/HermesConnectionSubsystem.cpp`의 include에 추가한다.

```cpp
#include "Connection/HermesLiveness.h"
#include "HAL/PlatformTime.h"
```

`SendJson()`을 교체한다.

```cpp
void UHermesConnectionSubsystem::SendJson(const FString& Json)
{
	if (Worker)
	{
		Worker->EnqueueOutbound(Json);
		LastSendTime = FPlatformTime::Seconds();
	}
}
```

`HandleFrame()`의 **첫 줄**에 수신 시각 갱신을 넣는다.

```cpp
void UHermesConnectionSubsystem::HandleFrame(const TSharedPtr<FJsonObject>& Obj)
{
	// 종류를 가리지 않고 모든 수신이 생존 신호다.
	LastRecvTime = FPlatformTime::Seconds();

	FString Type;
	if (!Obj->TryGetStringField(TEXT("type"), Type)) return;
```

- [ ] **Step 8: `Tick()`에 연결 엣지 초기화와 생존 판정 추가**

`Tick()`의 연결 엣지 감지 블록을 교체한다.

```cpp
	// 연결 엣지 감지: 새로 연결되면 재-identify 하고 생존 타이머를 초기화한다.
	const bool bNow = Worker->IsConnected();
	const double NowSeconds = FPlatformTime::Seconds();

	if (bNow && !bWasConnected)
	{
		bIdentified  = false;
		LastRecvTime = NowSeconds;   // 초기화하지 않으면 연결 직후 즉시 사망 판정이 난다
		LastSendTime = NowSeconds;
		SendIdentify();
	}
	if (!bNow && bWasConnected)
	{
		bIdentified = false;
		OnConnectionStateChanged.Broadcast(false);
	}
	bWasConnected = bNow;
```

인바운드 소비 루프 **뒤에** 생존 판정을 추가한다.

```cpp
	// 연결이 성립한 동안에만 평가한다. 재연결 대기 중에는 수신이 없는 것이 정상이다.
	if (bNow)
	{
		const UHermesSettings* Settings = GetDefault<UHermesSettings>();
		const HermesLiveness::EDecision D = HermesLiveness::Evaluate(
			NowSeconds, LastRecvTime, LastSendTime,
			Settings->KeepAlivePingIntervalSeconds, Settings->PeerTimeoutSeconds);

		if (D == HermesLiveness::EDecision::SendPing)
		{
			SendJson(HermesJson::MakePing(FString::Printf(TEXT("k-%04d"), ++PingCounter)));
		}
		else if (D == HermesLiveness::EDecision::DeclareDead)
		{
			// 조용히 끊긴 연결은 다음 송신 시점까지 드러나지 않는다. 송신은 플레이어가
			// 말을 걸 때 일어나므로, 그때 재연결이 시작되면 가장 나쁜 타이밍이 된다.
			UE_LOG(LogTemp, Warning,
				TEXT("[Hermes] peer silent for %.0fs, treating connection as dead"),
				Settings->PeerTimeoutSeconds);
			Worker->RequestReconnect();
		}
	}
```

- [ ] **Step 9: 설정값 정합성 경고 추가**

`Initialize()`의 `LoadCredentials();` 아래에 추가한다.

```cpp
	// PeerTimeout 이 PingInterval 에 비해 너무 짧으면 정상 연결이 죽은 것으로 오판된다.
	// 값을 강제로 교정하지는 않는다 — 설정자의 의도를 덮어쓰기보다 문제를 드러낸다.
	if (Settings->PeerTimeoutSeconds < Settings->KeepAlivePingIntervalSeconds * 2.f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Hermes] PeerTimeoutSeconds (%.1f) should be at least 2x "
			     "KeepAlivePingIntervalSeconds (%.1f); false disconnects are likely"),
			Settings->PeerTimeoutSeconds, Settings->KeepAlivePingIntervalSeconds);
	}
```

- [ ] **Step 10: 소켓 수준 keepalive 활성화**

`Transport/HermesSocketWorker.cpp`의 `ConnectSocket()`에서 `Socket->SetNonBlocking(true);` **앞에** 추가한다.

```cpp
	// OS 기본 keepalive 주기는 보통 2시간이라 단독으로는 쓸모가 없지만,
	// 애플리케이션 ping 이 놓치는 하위 계층 경로를 보완한다.
	Socket->SetKeepAlive(true);
```

- [ ] **Step 11: 전체 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 16종 통과.

- [ ] **Step 12: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport
git commit -m "feat: 클라이언트 keepalive ping 과 수신 침묵 기반 사망 판정"
```

커밋 본문에 포함할 내용:

```
소켓 keepalive 설정도 클라이언트 ping 도 없어 조용히 끊긴 연결이
다음 송신 시점까지 탐지되지 않았다. 송신은 플레이어가 말을 걸 때
일어나므로 재연결이 가장 나쁜 타이밍에 시작됐다.

- 송신 침묵이 KeepAlivePingIntervalSeconds 를 넘으면 ping 을 보낸다
- 종류를 가리지 않는 수신 침묵이 PeerTimeoutSeconds 를 넘으면
  죽은 것으로 판정하고 재연결한다. 대화가 활발하면 ping 없이도 성립한다
- 죽은 연결에 ping 을 보내지 않도록 사망 판정이 우선한다
- 소켓 SO_KEEPALIVE 도 켜서 하위 계층 경로를 보완한다

판정은 프레임 종류를 아는 게임 스레드에 두고 워커는 프로토콜을
모르는 상태로 유지한다. 결정 로직은 시간 주입형 순수 함수로 분리한다.
```

---

## Task 13: 대화 응답 타임아웃과 상관

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesPendingChats.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesPendingChats.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesPendingChats.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp` (`SendChat`, `HandleFrame`, `Tick`)
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI/HermesDialogueWidget.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI/HermesDialogueWidget.cpp`

**Interfaces:**
- Consumes: `UHermesSettings::ChatResponseTimeoutSeconds` (Task 1), `OnChatDelta` (Task 11)
- Produces:
  - `class FHermesPendingChats` — `Add` / `Touch` / `Remove` / `CollectTimedOut` / `Clear` / `Num`
  - `UHermesConnectionSubsystem::OnChatFailed` — `DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatFailed, const FString& /*Id*/, const FString& /*Reason*/)`

**왜 이 태스크가 필요한가:** 위젯이 `"생각 중..."`을 띄우지만 응답이 오지 않으면 그대로 남는다. 액션에는 타임아웃이 있으나 대화에는 없다. 또 `HandleChatResponse`가 `Id`를 쓰지 않아 늦게 도착한 이전 발화의 응답이 현재 화면을 덮어쓴다.

- [ ] **Step 1: 추적기 헤더 작성**

`Connection/HermesPendingChats.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 진행 중인 발화 추적.
 * 시간을 인자로 주입받는 순수 클래스라 단독 테스트가 가능하다.
 *
 * 델타 수신이 타이머를 갱신하므로 긴 생성은 살아남고 멈춘 생성만 잡힌다.
 * 스트리밍이 이 판정을 정확하게 만들어 주는 지점이다.
 */
class FHermesPendingChats
{
public:
	/** 발화를 추적 목록에 넣는다. 같은 Id 가 있으면 시각만 갱신된다. */
	void Add(const FString& Id, double Now);

	/** 진행 신호(델타 수신). 추적 중이 아닌 Id 는 무시한다. */
	void Touch(const FString& Id, double Now);

	/** 최종 응답 수신. 추적 목록에서 제거한다. */
	void Remove(const FString& Id);

	/**
	 * 마지막 진행으로부터 Timeout 이 지난 항목을 Out 에 담고 목록에서 제거한다.
	 * 제거까지 하는 이유는 같은 발화가 매 틱 반복 통지되는 것을 막기 위함이다.
	 */
	void CollectTimedOut(double Now, float Timeout, TArray<FString>& Out);

	/** 추적 목록을 비운다. 연결이 끊길 때 호출한다. */
	void Clear();

	int32 Num() const { return LastProgress.Num(); }

private:
	TMap<FString, double> LastProgress;
};
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Connection/HermesPendingChats.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Connection/HermesPendingChats.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesPendingChatsTest,
	"Hermes.PendingChats.Timeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesPendingChatsTest::RunTest(const FString& Parameters)
{
	const float Timeout = 60.f;

	// 타임아웃 전에는 수집되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TArray<FString> Out;
		P.CollectTimedOut(159.0, Timeout, Out);
		TestEqual(TEXT("not yet timed out"), Out.Num(), 0);
		TestEqual(TEXT("still tracked"), P.Num(), 1);
	}

	// 타임아웃 경과 후 수집되고 목록에서 제거된다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		TArray<FString> Out;
		P.CollectTimedOut(160.0, Timeout, Out);
		TestEqual(TEXT("timed out"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("correct id"), Out[0], TEXT("c-1"));
		}
		TestEqual(TEXT("removed after collect"), P.Num(), 0);

		// 두 번째 호출에서 다시 통지되지 않는다.
		TArray<FString> Out2;
		P.CollectTimedOut(300.0, Timeout, Out2);
		TestEqual(TEXT("not reported twice"), Out2.Num(), 0);
	}

	// Touch 가 타이머를 갱신해 긴 생성이 타임아웃되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);

		// 50초마다 델타가 오면 총 200초가 지나도 살아있어야 한다.
		P.Touch(TEXT("c-1"), 150.0);
		P.Touch(TEXT("c-1"), 200.0);
		P.Touch(TEXT("c-1"), 250.0);
		P.Touch(TEXT("c-1"), 300.0);

		TArray<FString> Out;
		P.CollectTimedOut(340.0, Timeout, Out);
		TestEqual(TEXT("kept alive by deltas"), Out.Num(), 0);

		// 델타가 끊기면 그때부터 다시 센다.
		TArray<FString> Out2;
		P.CollectTimedOut(360.0, Timeout, Out2);
		TestEqual(TEXT("dies after deltas stop"), Out2.Num(), 1);
	}

	// 추적 중이 아닌 Id 의 Touch 는 무시한다(항목이 생기지 않는다).
	{
		FHermesPendingChats P;
		P.Touch(TEXT("unknown"), 100.0);
		TestEqual(TEXT("touch does not create"), P.Num(), 0);
	}

	// Remove 후에는 수집되지 않는다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("c-1"), 100.0);
		P.Remove(TEXT("c-1"));

		TArray<FString> Out;
		P.CollectTimedOut(1000.0, Timeout, Out);
		TestEqual(TEXT("removed not collected"), Out.Num(), 0);
	}

	// 여러 건 중 만료된 것만 선별 수집된다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("old"), 100.0);
		P.Add(TEXT("new"), 150.0);

		TArray<FString> Out;
		P.CollectTimedOut(170.0, Timeout, Out);
		TestEqual(TEXT("only old collected"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("old id"), Out[0], TEXT("old"));
		}
		TestEqual(TEXT("new still tracked"), P.Num(), 1);
	}

	// Clear 는 전부 비운다.
	{
		FHermesPendingChats P;
		P.Add(TEXT("a"), 100.0);
		P.Add(TEXT("b"), 100.0);
		P.Clear();
		TestEqual(TEXT("cleared"), P.Num(), 0);
	}

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `FHermesPendingChats` 메서드들 미정의.

- [ ] **Step 4: 구현 작성**

`Connection/HermesPendingChats.cpp` 전체 내용:

```cpp
#include "Connection/HermesPendingChats.h"

void FHermesPendingChats::Add(const FString& Id, double Now)
{
	LastProgress.Add(Id, Now);
}

void FHermesPendingChats::Touch(const FString& Id, double Now)
{
	// 추적 중이 아닌 Id 로 항목을 만들지 않는다. 이미 완료·타임아웃된 발화의
	// 늦은 델타가 새 추적을 시작하면 영영 만료되지 않는 항목이 생긴다.
	if (double* Found = LastProgress.Find(Id))
	{
		*Found = Now;
	}
}

void FHermesPendingChats::Remove(const FString& Id)
{
	LastProgress.Remove(Id);
}

void FHermesPendingChats::CollectTimedOut(double Now, float Timeout, TArray<FString>& Out)
{
	for (auto It = LastProgress.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() >= (double)Timeout)
		{
			Out.Add(It.Key());
			// 같은 발화가 매 틱 반복 통지되지 않도록 수집과 동시에 제거한다.
			It.RemoveCurrent();
		}
	}
}

void FHermesPendingChats::Clear()
{
	LastProgress.Reset();
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.PendingChats; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.PendingChats.Timeout` PASS.

- [ ] **Step 6: 구독 시스템에 델리게이트와 추적기 추가**

`Connection/HermesConnectionSubsystem.h`의 include에 추가한다.

```cpp
#include "Connection/HermesPendingChats.h"
```

`FOnChatDelta` 선언 아래에 추가한다.

```cpp
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatFailed, const FString& /*Id*/, const FString& /*Reason*/);
```

`public:` 섹션의 `FOnChatDelta OnChatDelta;` 아래에 추가한다.

```cpp
	/** 발화가 응답 없이 만료되었거나 연결 단절로 폐기되었음을 알린다. */
	FOnChatFailed OnChatFailed;
```

`private:` 섹션의 `TArray<FString> PendingChats;` 아래에 추가한다.

```cpp
	FHermesPendingChats InFlightChats;
```

- [ ] **Step 7: `SendChat()`에서 추적 시작**

`Connection/HermesConnectionSubsystem.cpp`의 `SendChat()`을 교체한다. Task 8에서 넣은 보류 상한은 그대로 두고 추적만 덧붙인다.

```cpp
void UHermesConnectionSubsystem::SendChat(const FString& Text)
{
	const FString Id = FString::Printf(TEXT("c-%04d"), ++ChatCounter);
	const FString Json = HermesJson::MakeChat(Id, Text);

	// identified 이전이라 보류되더라도 추적은 시작한다. 서버에 닿지 못한
	// 발화도 타임아웃으로 사용자에게 알려야 "생각 중..." 이 남지 않는다.
	InFlightChats.Add(Id, FPlatformTime::Seconds());
	LastSentChatId = Id;

	if (bIdentified)
	{
		SendJson(Json);
		return;
	}

	const int32 Dropped = HermesUtil::PushBounded(
		PendingChats, Json, GetDefault<UHermesSettings>()->MaxPendingChats);
	if (Dropped > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] pending chat overflow, dropped %d oldest"), Dropped);
	}
}
```

`private:` 섹션에 `LastSentChatId`를 추가한다 (`InFlightChats` 아래).

```cpp
	/** 가장 최근에 보낸 발화 id. 위젯의 상관 규칙이 이 값을 기준으로 삼는다. */
	FString LastSentChatId;
```

`public:` 섹션에 접근자를 추가한다.

```cpp
	const FString& GetLastSentChatId() const { return LastSentChatId; }
```

- [ ] **Step 8: 델타·응답에서 추적 갱신·해제**

`HandleFrame()`의 `chat_delta` 분기를 교체한다.

```cpp
	else if (Type == HermesMsg::ChatDelta)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		// 진행 신호. 이것이 긴 생성을 타임아웃에서 살려준다.
		InFlightChats.Touch(Id, FPlatformTime::Seconds());
		OnChatDelta.Broadcast(Text, Id);
	}
```

`chat_response` 분기를 교체한다.

```cpp
	else if (Type == HermesMsg::ChatResponse)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		InFlightChats.Remove(Id);
		OnChatResponse.Broadcast(Text, Id);
	}
```

- [ ] **Step 9: `Tick()`에 만료 수집 추가**

Task 12에서 넣은 생존 판정 블록 **뒤에** 추가한다. 연결 여부와 무관하게 평가한다 — 연결이 끊긴 동안에도 발화는 만료되어야 한다.

```cpp
	// 응답 없는 발화를 만료시킨다. 델타가 오는 동안에는 Touch 로 갱신되므로
	// 긴 생성은 살아남고 멈춘 생성만 잡힌다.
	{
		TArray<FString> TimedOut;
		InFlightChats.CollectTimedOut(NowSeconds,
			GetDefault<UHermesSettings>()->ChatResponseTimeoutSeconds, TimedOut);
		for (const FString& Id : TimedOut)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Hermes] chat %s timed out"), *Id);
			OnChatFailed.Broadcast(Id, TEXT("timeout"));
		}
	}
```

- [ ] **Step 10: 연결 단절 시 진행 중 발화 정리**

`Tick()`의 연결 끊김 엣지 블록을 교체한다.

```cpp
	if (!bNow && bWasConnected)
	{
		bIdentified = false;

		// 진행 중이던 발화를 모두 실패 처리한다. 재연결 후 서버가 이전 발화를
		// 이어서 응답하더라도 그 id 는 추적 대상이 아니므로 위젯의 상관 규칙이 무시한다.
		TArray<FString> Abandoned;
		InFlightChats.CollectTimedOut(NowSeconds, 0.f, Abandoned);
		for (const FString& Id : Abandoned)
		{
			OnChatFailed.Broadcast(Id, TEXT("disconnected"));
		}
		InFlightChats.Clear();

		OnConnectionStateChanged.Broadcast(false);
	}
```

> `CollectTimedOut(Now, 0.f, ...)`은 `Now - LastProgress >= 0`이 항상 참이므로 전부를 수집한다. 이후 `Clear()`는 방어적 중복이다.

- [ ] **Step 11: 위젯에 상관 규칙과 실패 표시 적용**

`UI/HermesDialogueWidget.h`의 `private:` 섹션에 추가한다.

```cpp
	void HandleChatFailed(const FString& Id, const FString& Reason);
```

`FDelegateHandle DeltaHandle;` 아래에 추가한다.

```cpp
	FDelegateHandle FailedHandle;
```

`UI/HermesDialogueWidget.cpp`의 `OpenFor()`에 구독을 추가한다.

```cpp
		FailedHandle = Connection->OnChatFailed.AddUObject(this, &UHermesDialogueWidget::HandleChatFailed);
```

`NativeDestruct()`에 해제를 추가한다.

```cpp
		Connection->OnChatFailed.Remove(FailedHandle);
```

세 핸들러를 상관 규칙이 적용된 형태로 교체한다.

```cpp
void UHermesDialogueWidget::HandleChatDelta(const FString& Text, const FString& Id)
{
	// 가장 최근에 보낸 발화의 응답만 화면에 반영한다. 늦게 도착한 이전
	// 발화의 응답이 현재 화면을 덮어쓰지 않게 한다.
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Hermes] ignoring stale chat_delta for %s"), *Id);
		return;
	}

	// 갱신은 NativeTick 이 틱당 1회만 수행한다 (Task 11).
	StreamingText += Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)
{
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Hermes] ignoring stale chat_response for %s"), *Id);
		return;
	}

	// 델타를 놓치거나 중복 처리했더라도 여기서 정본으로 교체되어 자기 교정된다.
	StreamingText = Text;
	bStreamingDirty = true;
}

void UHermesDialogueWidget::HandleChatFailed(const FString& Id, const FString& Reason)
{
	if (!Connection || Id != Connection->GetLastSentChatId())
	{
		return;
	}

	StreamingText = TEXT("응답을 받지 못했습니다.");
	bStreamingDirty = true;
}
```

- [ ] **Step 12: 전체 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 17종 통과.

- [ ] **Step 13: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection Plugins/HermesAgentNPC/Source/HermesAgentNPC/UI
git commit -m "feat: 대화 응답 타임아웃과 발화 id 상관"
```

커밋 본문에 포함할 내용:

```
위젯이 "생각 중..." 을 띄우지만 응답이 오지 않으면 그대로 남았다.
액션에는 타임아웃이 있으나 대화에는 없었다.

- 델타 수신이 타이머를 갱신하므로 긴 생성은 살아남고 멈춘 생성만
  잡힌다. 스트리밍이 이 판정을 정확하게 만들어 주는 지점이다
- 만료 항목은 수집과 동시에 제거해 매 틱 반복 통지를 막는다
- 연결이 끊기면 진행 중 발화를 모두 실패 처리하고 목록을 비운다
- 위젯은 가장 최근 발화의 id 와 일치하는 델타·응답만 반영한다.
  늦게 도착한 이전 응답이 현재 화면을 덮어쓰던 문제가 사라진다
```

---

## Task 13b: `action_event` — 장기 실행 액션의 비동기 완료 통지

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol/HermesMessages.h` / `.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Protocol/HermesMessages.spec.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Actions/MoveToActionHandler.h` / `.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h` / `.cpp`

**Interfaces:**
- Consumes: `HermesJson::Serialize` (기존), `UHermesConnectionSubsystem::SendJson` (기존)
- Produces:
  - `HermesMsg::ActionEvent` — `TEXT("action_event")`
  - `FString HermesJson::MakeActionEvent(const FString& Id, bool bCompleted, const TSharedPtr<FJsonObject>& Result, const FString& Error)`
  - `void UHermesConnectionSubsystem::SendActionEvent(const FString& Id, bool bCompleted, const TSharedPtr<FJsonObject>& Result, const FString& Error)`

**왜 이 태스크가 필요한가:** `MoveToActionHandler.cpp:42-45`가 길찾기 요청 성공 시점에 `arrived: true`를 회신한다. **도착한 적이 없는데 도착했다고 서버에 알린다.** LLM은 이 거짓을 근거로 "언덕 위에 도착했어요"라고 말하는데 캐릭터는 아직 출발도 안 했을 수 있다. 프로토콜 §4.5/§4.10이 이를 접수(`action_result`)와 완료(`action_event`)로 분리했으므로 클라이언트가 따라가야 한다.

완료를 기다렸다 `action_result`를 보내는 대안은 쓸 수 없다 — 15초 예산을 넘기는 것이 정확히 이 액션이고, 서버는 맵 크기와 이동 속도를 알 수 없어 타임아웃을 늘려 해결할 수도 없다.

- [ ] **Step 1: 메시지 빌더 추가**

`Protocol/HermesMessages.h`의 `HermesMsg` 에 추가한다.

```cpp
	inline const FString ActionEvent   = TEXT("action_event");
```

`HermesJson` 에 추가한다.

```cpp
	/**
	 * 이미 접수(action_result)한 액션의 완료/실패를 뒤늦게 알린다.
	 * bCompleted=false 면 event="failed" 로 나가며 Error 가 실린다.
	 */
	FString MakeActionEvent(const FString& Id, bool bCompleted,
	                        const TSharedPtr<FJsonObject>& Result, const FString& Error);
```

`Protocol/HermesMessages.cpp` 에 정의를 추가한다.

```cpp
FString HermesJson::MakeActionEvent(const FString& Id, bool bCompleted,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::ActionEvent);
	O->SetStringField(TEXT("id"), Id);
	O->SetStringField(TEXT("event"), bCompleted ? TEXT("completed") : TEXT("failed"));
	if (Result.IsValid())
	{
		O->SetObjectField(TEXT("result"), Result);
	}
	if (!Error.IsEmpty())
	{
		O->SetStringField(TEXT("error"), Error);
	}
	return Serialize(O);
}
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Protocol/HermesMessages.spec.cpp` 끝에 추가한다.

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesActionEventTest,
	"Hermes.Protocol.Messages.ActionEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesActionEventTest::RunTest(const FString& Parameters)
{
	// 완료: event=completed, result 포함, error 없음
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetBoolField(TEXT("arrived"), true);

		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(
			HermesJson::MakeActionEvent(TEXT("act-1"), true, Res, FString()), Obj));

		FString Type, Id, Event;
		Obj->TryGetStringField(TEXT("type"), Type);
		Obj->TryGetStringField(TEXT("id"), Id);
		Obj->TryGetStringField(TEXT("event"), Event);
		TestEqual(TEXT("type"), Type, TEXT("action_event"));
		TestEqual(TEXT("id"), Id, TEXT("act-1"));
		TestEqual(TEXT("event"), Event, TEXT("completed"));
		TestTrue(TEXT("has result"), Obj->HasField(TEXT("result")));
		TestFalse(TEXT("no error"), Obj->HasField(TEXT("error")));
	}

	// 실패: event=failed, error 포함, result 없음
	{
		TSharedPtr<FJsonObject> Obj;
		TestTrue(TEXT("parses"), HermesJson::Parse(
			HermesJson::MakeActionEvent(TEXT("act-2"), false, nullptr, TEXT("path blocked")), Obj));

		FString Event, Error;
		Obj->TryGetStringField(TEXT("event"), Event);
		Obj->TryGetStringField(TEXT("error"), Error);
		TestEqual(TEXT("event"), Event, TEXT("failed"));
		TestEqual(TEXT("error"), Error, TEXT("path blocked"));
		TestFalse(TEXT("no result"), Obj->HasField(TEXT("result")));
	}

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패 → Step 1의 정의를 넣었다면 통과. 정의를 아직 안 넣었다면 `MakeActionEvent` 미정의.

- [ ] **Step 4: 구독 시스템에 송신 진입점 추가**

`Connection/HermesConnectionSubsystem.h` 의 `public:` 에 추가한다.

```cpp
	/** 장기 실행 액션의 완료/실패를 서버에 알린다. 액션 핸들러가 호출한다. */
	void SendActionEvent(const FString& Id, bool bCompleted,
	                     const TSharedPtr<class FJsonObject>& Result, const FString& Error);
```

`.cpp` 에 정의를 추가한다.

```cpp
void UHermesConnectionSubsystem::SendActionEvent(const FString& Id, bool bCompleted,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	SendJson(HermesJson::MakeActionEvent(Id, bCompleted, Result, Error));
}
```

- [ ] **Step 5: `MoveToActionHandler` 를 두 단계 응답으로 교체**

헤더에 이동 완료를 받을 상태를 추가한다. `Actions/MoveToActionHandler.h` 의 `private:` 에:

```cpp
	/** 진행 중인 이동의 action_request id. 완료 통지에 그대로 쓴다. */
	FString PendingMoveId;

	UFUNCTION()
	void OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result);
```

`Actions/MoveToActionHandler.cpp` 의 즉시 회신 부분을 교체한다. 기존의 아래 두 줄을 지운다.

```cpp
	// 최소 동작: 요청 성공을 도착으로 간주해 즉시 회신 (확장 시 OnMoveCompleted 델리게이트로 대체)
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("arrived"), true);
	OnDone.ExecuteIfBound(true, Res, FString());
```

대신 접수 응답을 보내고 완료 델리게이트를 건다.

```cpp
	// action_result 는 "접수했고 시작했다"까지만 말한다. 도착 여부는
	// 나중에 action_event 로 알린다 (프로토콜 §4.5, §4.10).
	// 완료를 기다렸다 회신하면 15초 예산을 넘겨 서버 타임아웃이 난다.
	PendingMoveId = Payload.Id;

	AI->ReceiveMoveCompleted.AddUniqueDynamic(this, &UMoveToActionHandler::OnMoveCompleted);

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("started"), true);

	// 남은 경로 길이 / 이동 속도로 대략의 도착 시간을 낸다. 없으면 필드를 뺀다.
	if (const UPathFollowingComponent* PFC = AI->GetPathFollowingComponent())
	{
		const float Speed = Npc->GetCharacterMovement() ? Npc->GetCharacterMovement()->GetMaxSpeed() : 0.f;
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const float Dist = FVector::Dist(Npc->GetActorLocation(), Dest);
			Res->SetNumberField(TEXT("eta_seconds"), Dist / Speed);
		}
	}

	OnDone.ExecuteIfBound(true, Res, FString());
```

완료 콜백을 추가한다.

```cpp
void UMoveToActionHandler::OnMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)
{
	if (PendingMoveId.IsEmpty())
	{
		return;
	}

	// 프로토콜상 id 당 event 는 최대 1회다. 먼저 비워 재진입을 막는다.
	const FString Id = PendingMoveId;
	PendingMoveId.Reset();

	if (AAIController* AI = Npc.IsValid() ? Cast<AAIController>(Npc->GetController()) : nullptr)
	{
		AI->ReceiveMoveCompleted.RemoveDynamic(this, &UMoveToActionHandler::OnMoveCompleted);
	}

	UHermesConnectionSubsystem* Conn = nullptr;
	if (Npc.IsValid())
	{
		if (UGameInstance* GI = Npc->GetGameInstance())
		{
			Conn = GI->GetSubsystem<UHermesConnectionSubsystem>();
		}
	}
	if (!Conn)
	{
		return; // 연결이 사라졌다. 프로토콜상 event 유실은 허용된다.
	}

	if (Result == EPathFollowingResult::Success)
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetBoolField(TEXT("arrived"), true);
		Conn->SendActionEvent(Id, true, Res, FString());
	}
	else
	{
		Conn->SendActionEvent(Id, false, nullptr, TEXT("path blocked"));
	}
}
```

필요한 include 를 `MoveToActionHandler.cpp` 상단에 추가한다.

```cpp
#include "Connection/HermesConnectionSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
```

> **새 이동이 이전 이동을 덮어쓰는 경우.** `PendingMoveId` 가 비어있지 않은 상태로 새 `move_to` 가 오면 이전 이동은 `EPathFollowingResult::Aborted` 로 콜백이 오고, 그 시점에 이전 id 로 `failed` 이벤트가 나간다. 그 후 새 id 가 `PendingMoveId` 에 들어간다. 순서가 뒤집히지 않도록 **접수 응답보다 델리게이트 등록을 먼저** 하지 않는다 — 위 코드의 순서를 지킨다.

- [ ] **Step 6: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 18종 통과 (Task 13까지 17 + ActionEvent 1).

- [ ] **Step 7: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC
git commit -m "feat: action_event 로 move_to 완료를 비동기 통지"
```

커밋 본문에 포함할 내용:

```
길찾기 요청 성공 시점에 arrived:true 를 보내고 있었다. 도착한 적이
없는데 도착했다고 서버에 알리는 것이라, LLM 이 그 거짓을 근거로
"도착했어요" 라고 말하는 동안 캐릭터는 출발도 안 했을 수 있다.

완료를 기다렸다 action_result 를 보내는 대안은 쓸 수 없다. 15초 예산을
넘기는 것이 정확히 이 액션이고, 서버는 맵 크기와 이동 속도를 알 수
없어 타임아웃을 늘려 해결할 수도 없다.

- action_result 는 { started, eta_seconds } 로 접수만 알린다
- ReceiveMoveCompleted 델리게이트로 실제 완료를 받아 action_event 를 보낸다
- id 당 event 는 최대 1회. 콜백 진입 시 PendingMoveId 를 먼저 비운다
- 연결이 끊겼으면 event 는 유실된다. 프로토콜이 허용하는 동작이다
```

---

# Phase 4 — TLS 전송

> **Phase 4 전체 원칙:** 프레이밍(`FHermesFrameCodec`, `FFrameAccumulator`)과 큐·백오프·상한 로직은 **한 줄도 바뀌지 않는다.** 바이트를 어디로 읽고 쓰는지만 달라진다. Task 14가 순수 리팩터링으로 그 경계를 만들고, Task 16이 그 뒤에 TLS 구현을 끼운다.

## Task 14: 전송 계층 추상화 (순수 리팩터링)

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/IHermesTransport.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesPlainTransport.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesPlainTransport.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp`

**Interfaces:**
- Consumes: `FHermesWorkerConfig` (Task 2)
- Produces:
  - `class IHermesTransport` — `bool Connect(const FHermesWorkerConfig&, const FInternetAddr&)`, `void Close()`, `int32 Recv(uint8*, int32)`, `int32 Send(const uint8*, int32)`, `bool HasPendingData(uint32&)`
  - `class FHermesPlainTransport : public IHermesTransport`

**이 태스크는 동작을 바꾸지 않는다.** 기존 테스트가 전부 통과해야 하고, 수동으로도 접속·대화·액션이 이전과 동일해야 한다. 동작 변경과 구조 변경을 한 커밋에 섞지 않는 것이 목적이다.

- [ ] **Step 1: 전송 인터페이스 작성**

`Transport/IHermesTransport.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Transport/HermesWorkerConfig.h"

class FInternetAddr;

/**
 * 연결 지향 바이트 스트림. 논블로킹 시맨틱을 따른다.
 *
 * 이 경계 덕분에 프레이밍·큐·백오프 로직은 평문과 TLS 를 구분하지 않는다.
 * 구현체는 워커 전용 스레드에서만 호출되므로 스레드 안전을 신경쓰지 않는다.
 */
class IHermesTransport
{
public:
	virtual ~IHermesTransport() = default;

	/** 연결하고, TLS 라면 핸드셰이크까지 끝낸다. 실패 시 내부 자원을 정리하고 false. */
	virtual bool Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) = 0;

	virtual void Close() = 0;

	/** 읽을 데이터가 대기 중인지 확인한다. */
	virtual bool HasPendingData(uint32& OutBytes) = 0;

	/** 반환: 읽은 바이트 수. 0 = 지금은 없음. 음수 = 치명적 오류(재연결). */
	virtual int32 Recv(uint8* Buf, int32 BufSize) = 0;

	/** 반환: 보낸 바이트 수. 0 = 지금은 불가(재시도). 음수 = 치명적 오류. */
	virtual int32 Send(const uint8* Buf, int32 Num) = 0;
};
```

- [ ] **Step 2: 평문 전송 헤더 작성**

`Transport/HermesPlainTransport.h` 전체 내용:

```cpp
#pragma once
#include "CoreMinimal.h"
#include "Transport/IHermesTransport.h"

class FSocket;

/** FSocket 기반 평문 TCP 전송. 기존 워커 코드를 그대로 옮긴 것이다. */
class FHermesPlainTransport : public IHermesTransport
{
public:
	virtual ~FHermesPlainTransport() override;

	virtual bool  Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr) override;
	virtual void  Close() override;
	virtual bool  HasPendingData(uint32& OutBytes) override;
	virtual int32 Recv(uint8* Buf, int32 BufSize) override;
	virtual int32 Send(const uint8* Buf, int32 Num) override;

private:
	FSocket* Socket = nullptr;
};
```

- [ ] **Step 3: 평문 전송 구현**

`Transport/HermesPlainTransport.cpp` 전체 내용:

```cpp
#include "Transport/HermesPlainTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"
#include "IPAddress.h"

FHermesPlainTransport::~FHermesPlainTransport()
{
	Close();
}

bool FHermesPlainTransport::Connect(const FHermesWorkerConfig& Config, const FInternetAddr& Addr)
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return false;
	}

	// FTcpSocketBuilder 는 FIPv4Endpoint 에서 프로토콜을 유도해 IPv4 전용이다.
	// Task 3 과 동일한 이유로 CreateSocket 을 직접 부른다.
	Socket = SS->CreateSocket(NAME_Stream, TEXT("HermesClient"), Addr.GetProtocolType());
	if (!Socket)
	{
		return false;
	}

	Socket->SetNonBlocking(false);   // 연결까지는 블로킹

	if (!Socket->Connect(Addr))
	{
		Close();
		return false;
	}

	// OS 기본 keepalive 주기는 길지만, 애플리케이션 ping 이 놓치는
	// 하위 계층 경로를 보완한다.
	Socket->SetKeepAlive(true);
	Socket->SetNonBlocking(true); // 연결 후 논블로킹 수신으로 전환
	return true;
}

void FHermesPlainTransport::Close()
{
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SS->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
}

bool FHermesPlainTransport::HasPendingData(uint32& OutBytes)
{
	return Socket && Socket->HasPendingData(OutBytes);
}

int32 FHermesPlainTransport::Recv(uint8* Buf, int32 BufSize)
{
	if (!Socket)
	{
		return -1;
	}
	int32 Read = 0;
	if (!Socket->Recv(Buf, BufSize, Read))
	{
		return -1; // 수신 에러 → 재연결
	}
	return FMath::Max(0, Read);
}

int32 FHermesPlainTransport::Send(const uint8* Buf, int32 Num)
{
	if (!Socket)
	{
		return -1;
	}
	int32 Sent = 0;
	if (!Socket->Send(Buf, Num, Sent) || Sent < 0)
	{
		return -1; // 송신 실패 → 재연결
	}
	return Sent;
}
```

- [ ] **Step 4: 워커 헤더를 전송 인터페이스 기반으로 변경**

`Transport/HermesSocketWorker.h`의 include에 추가한다.

```cpp
#include "Transport/IHermesTransport.h"
```

전방 선언 `class FSocket;` 을 **삭제**한다.

`private:` 섹션의 `bool ConnectSocket(); void CloseSocket();` 선언은 그대로 두고, 멤버 `FSocket* Socket = nullptr;` 을 교체한다.

```cpp
	TUniquePtr<IHermesTransport> Transport;
```

- [ ] **Step 5: 워커 구현을 전송 호출로 변경**

`Transport/HermesSocketWorker.cpp`의 include에서 `#include "Sockets.h"`, `#include "Common/TcpSocketBuilder.h"` 를 **삭제**하고 추가한다.

```cpp
#include "Transport/HermesPlainTransport.h"
#include "IPAddress.h"
```

`ConnectSocket()` 전체를 교체한다. 주소 해석(Task 3)은 그대로 두고 소켓 생성·연결만 전송에 위임한다.

```cpp
bool FHermesSocketWorker::ConnectSocket()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return false;
	}

	// 호스트명과 IP를 모두 처리한다. 컨테이너명·k8s 서비스명 같은 유동 주소 대응.
	FAddressInfoResult Result = SS->GetAddressInfo(*Config.Host, nullptr,
		EAddressInfoFlags::Default, NAME_None);
	if (Result.ReturnCode != SE_NO_ERROR || Result.Results.Num() == 0)
	{
		return false;
	}

	// IPv4 우선. 듀얼스택에서 OS가 IPv6를 먼저 주더라도 서버가 IPv4만 수신하면
	// 접속이 실패하므로, 기존 환경의 동작을 바꾸지 않기 위해 IPv4를 먼저 고른다.
	const FAddressInfoResultData* Chosen = &Result.Results[0];
	for (const FAddressInfoResultData& R : Result.Results)
	{
		if (R.Address->GetProtocolType() == FNetworkProtocolTypes::IPv4)
		{
			Chosen = &R;
			break;
		}
	}

	TSharedRef<FInternetAddr> InetAddr = Chosen->Address->Clone();
	InetAddr->SetPort(Config.Port);

	Transport = MakeUnique<FHermesPlainTransport>();
	if (!Transport->Connect(Config, *InetAddr))
	{
		Transport.Reset();
		return false;
	}

	Accumulator = FFrameAccumulator(); // 새 연결마다 파서 리셋
	return true;
}
```

`CloseSocket()`을 교체한다.

```cpp
void FHermesSocketWorker::CloseSocket()
{
	bConnected = false;
	if (Transport)
	{
		Transport->Close();
		Transport.Reset();
	}
}
```

`SendAllPending()`의 송신 루프를 교체한다.

```cpp
		int32 Total = 0;
		while (Total < Bytes.Num())
		{
			const int32 Sent = Transport->Send(Bytes.GetData() + Total, Bytes.Num() - Total);
			if (Sent < 0)
			{
				return false; // 송신 실패 → 재연결
			}
			if (Sent == 0)
			{
				FPlatformProcess::Sleep(0.001f); // 송신 버퍼 가득 참: 잠시 양보
				continue;
			}
			Total += Sent;
		}
```

`ReceiveAvailable()`의 수신 루프 머리를 교체한다.

```cpp
bool FHermesSocketWorker::ReceiveAvailable()
{
	uint8 Buf[4096];
	uint32 Pending = 0;
	while (Transport->HasPendingData(Pending))
	{
		const int32 Read = Transport->Recv(Buf, sizeof(Buf));
		if (Read < 0)
		{
			return false; // 수신 에러 → 재연결
		}
		if (Read == 0)
		{
			break;
		}
		Accumulator.Feed(Buf, Read);
```

`~FHermesSocketWorker()`의 `CloseSocket();` 은 그대로 두면 된다.

> **Task 12 Step 10에서 넣은 `Socket->SetKeepAlive(true)` 는 이 교체로 워커에서 사라진다.** 같은 호출이 Step 3의 `FHermesPlainTransport::Connect()` 안에 이미 들어 있으므로 동작은 유지된다. 워커에 남겨두면 `Socket` 멤버가 없어져 컴파일되지 않는다.

- [ ] **Step 6: 빌드 및 테스트 (동작 불변 확인)**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 17종 통과. **하나라도 깨지면 리팩터링에서 동작이 바뀐 것이므로 되돌려 원인을 찾는다.**

- [ ] **Step 7: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport
git commit -m "refactor: 전송 계층을 IHermesTransport 뒤로 추상화"
```

커밋 본문에 포함할 내용:

```
TLS 를 끼울 자리를 만든다. 프레이밍·큐·백오프·상한 로직은 바꾸지
않고 바이트를 읽고 쓰는 지점만 인터페이스 뒤로 옮겼다.

동작은 그대로다. 구조 변경과 동작 변경을 한 커밋에 섞지 않기 위해
평문 구현만 먼저 분리한다.
```

---

## Task 15: TLS 정책 결정 로직

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesTlsPolicy.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesTlsPolicy.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesTlsPolicy.spec.cpp`

**Interfaces:**
- Consumes: `FHermesTlsConfig` (Task 2)
- Produces:
  - `enum class HermesTls::EVerifyMode : uint8 { PinnedKey, PrivateCa, SystemCa }`
  - `FString HermesTls::ResolveServerName(const FString& Host, const FString& TlsServerName)`
  - `EVerifyMode HermesTls::ResolveVerifyMode(const TArray<FString>& Pins, const FString& PrivateCaPath)`
  - `bool HermesTls::ResolveUseTls(bool bConfigured, bool bShippingBuild)`

**왜 순수 로직만 따로 빼는가:** TLS 핸드셰이크와 인증서 검증은 실제 OpenSSL과 서버가 있어야 검증할 수 있어 자동화 테스트가 불가능하다. 반면 "어떤 이름으로 검증할지", "어떤 모드를 쓸지", "Shipping에서 강제할지"는 순수 결정이라 단독 테스트가 된다. 설정 실수로 검증이 약해지는 것이 가장 흔한 실패 경로이므로 이 부분만은 반드시 덮는다.

- [ ] **Step 1: 정책 헤더 작성**

`Transport/HermesTlsPolicy.h` 전체 내용:

```cpp
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
```

- [ ] **Step 2: 실패하는 테스트 작성**

`Transport/HermesTlsPolicy.spec.cpp` 전체 내용:

```cpp
#include "Misc/AutomationTest.h"
#include "Transport/HermesTlsPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsServerNameTest,
	"Hermes.TlsPolicy.ServerName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsServerNameTest::RunTest(const FString& Parameters)
{
	// 지정하지 않으면 Host 를 그대로 쓴다.
	TestEqual(TEXT("empty falls back to host"),
		HermesTls::ResolveServerName(TEXT("192.168.0.111"), FString()),
		TEXT("192.168.0.111"));

	// 지정하면 그 값을 쓴다. IP 로 접속하면서 인증서에 도메인명이 든 경우다.
	TestEqual(TEXT("override wins"),
		HermesTls::ResolveServerName(TEXT("192.168.0.111"), TEXT("hermes.local")),
		TEXT("hermes.local"));

	// 공백만 있는 값도 미지정으로 취급한다.
	TestEqual(TEXT("whitespace falls back"),
		HermesTls::ResolveServerName(TEXT("hermes.example.com"), TEXT("   ")),
		TEXT("hermes.example.com"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsVerifyModeTest,
	"Hermes.TlsPolicy.VerifyMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsVerifyModeTest::RunTest(const FString& Parameters)
{
	using HermesTls::EVerifyMode;

	// 둘 다 없으면 시스템 루트 CA.
	{
		TArray<FString> NoPins;
		TestTrue(TEXT("no pins, no ca => SystemCa"),
			HermesTls::ResolveVerifyMode(NoPins, FString()) == EVerifyMode::SystemCa);
	}

	// 사설 CA 만 있으면 PrivateCa.
	{
		TArray<FString> NoPins;
		TestTrue(TEXT("ca only => PrivateCa"),
			HermesTls::ResolveVerifyMode(NoPins, TEXT("Certs/private-ca.pem")) == EVerifyMode::PrivateCa);
	}

	// 핀이 있으면 핀이 우선한다 (사설 CA 가 함께 있어도).
	{
		TArray<FString> Pins;
		Pins.Add(TEXT("abc123="));
		TestTrue(TEXT("pins only => PinnedKey"),
			HermesTls::ResolveVerifyMode(Pins, FString()) == EVerifyMode::PinnedKey);
		TestTrue(TEXT("pins beat ca"),
			HermesTls::ResolveVerifyMode(Pins, TEXT("Certs/private-ca.pem")) == EVerifyMode::PinnedKey);
	}

	// 빈 문자열만 든 핀 배열은 핀 없음으로 취급한다. ini 편집 실수로
	// 검증이 통과해버리는 일이 없어야 한다.
	{
		TArray<FString> EmptyPins;
		EmptyPins.Add(FString());
		EmptyPins.Add(TEXT("  "));
		TestTrue(TEXT("blank pins are not pins"),
			HermesTls::ResolveVerifyMode(EmptyPins, FString()) == EVerifyMode::SystemCa);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesTlsUseTlsTest,
	"Hermes.TlsPolicy.UseTls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesTlsUseTlsTest::RunTest(const FString& Parameters)
{
	// 비-Shipping 에서는 설정을 따른다. 개발 중 평문 사용을 허용한다.
	TestTrue(TEXT("dev true => true"),  HermesTls::ResolveUseTls(true,  false));
	TestFalse(TEXT("dev false => false"), HermesTls::ResolveUseTls(false, false));

	// Shipping 에서는 false 여도 강제로 켠다.
	TestTrue(TEXT("shipping true => true"),  HermesTls::ResolveUseTls(true,  true));
	TestTrue(TEXT("shipping false => forced true"), HermesTls::ResolveUseTls(false, true));

	return true;
}
```

- [ ] **Step 3: 테스트가 실패하는지 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

Expected: 링크 실패. `HermesTls::ResolveServerName` 등 3개 미정의.

- [ ] **Step 4: 구현 작성**

`Transport/HermesTlsPolicy.cpp` 전체 내용:

```cpp
#include "Transport/HermesTlsPolicy.h"

namespace HermesTls
{
	FString ResolveServerName(const FString& Host, const FString& TlsServerName)
	{
		const FString Trimmed = TlsServerName.TrimStartAndEnd();
		return Trimmed.IsEmpty() ? Host : Trimmed;
	}

	EVerifyMode ResolveVerifyMode(const TArray<FString>& Pins, const FString& PrivateCaPath)
	{
		// 공백뿐인 항목은 핀으로 치지 않는다. ini 편집 실수로 핀 목록이
		// 비어 있는데 핀 모드로 들어가면 검증이 무력해진다.
		for (const FString& Pin : Pins)
		{
			if (!Pin.TrimStartAndEnd().IsEmpty())
			{
				return EVerifyMode::PinnedKey;
			}
		}

		if (!PrivateCaPath.TrimStartAndEnd().IsEmpty())
		{
			return EVerifyMode::PrivateCa;
		}
		return EVerifyMode::SystemCa;
	}

	bool ResolveUseTls(bool bConfigured, bool bShippingBuild)
	{
		return bShippingBuild ? true : bConfigured;
	}
}
```

- [ ] **Step 5: 테스트 통과 확인**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.TlsPolicy; Quit" -unattended -nopause -nullrhi
```

Expected: `Hermes.TlsPolicy.ServerName` / `.VerifyMode` / `.UseTls` 3종 PASS.

- [ ] **Step 6: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport
git commit -m "feat: TLS 검증 정책 결정 로직 및 테스트"
```

커밋 본문에 포함할 내용:

```
핸드셰이크와 인증서 검증은 실제 OpenSSL 과 서버가 있어야 확인되지만,
"어떤 이름으로 검증할지" "어떤 모드를 쓸지" "Shipping 에서 강제할지"는
순수 결정이라 단독 테스트가 된다. 설정 실수로 검증이 약해지는 것이
가장 흔한 실패 경로이므로 이 부분만은 자동화로 덮는다.

핀이 있으면 사설 CA 보다 우선한다. 핀은 자체 서명을 허용하는 대신
정확히 그 키만 신뢰하므로 더 좁고 강한 조건이다.
공백뿐인 핀 항목은 핀으로 치지 않는다. ini 편집 실수로 빈 핀 목록에
핀 모드로 들어가면 검증이 무력해진다.
```

---

## Task 16: OpenSSL 기반 TLS 전송

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesTlsTransport.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesTlsTransport.cpp`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesAgentNPC.Build.cs`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp` (`ConnectSocket`)
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp` (`Initialize`)

**Interfaces:**
- Consumes: `IHermesTransport` (Task 14), `HermesTls::*` (Task 15), `FHermesTlsConfig` (Task 2)
- Produces: `class FHermesTlsTransport : public IHermesTransport`

**API 확인 완료 (UE 5.8).** `Engine/Source/Runtime/Online/SSL/Public/Interfaces/` 를 확인한 결과 아래 시그니처가 실재한다.

```cpp
// ISslManager.h
virtual bool     InitializeSsl() = 0;                                  // 참조 계수
virtual void     ShutdownSsl() = 0;
virtual SSL_CTX* CreateSslContext(const FSslContextCreateOptions&) = 0;
virtual void     DestroySslContext(SSL_CTX*) = 0;

struct FSslContextCreateOptions {
    ESslTlsProtocol MinimumProtocol = ESslTlsProtocol::Minimum;  // TLSv1_2 지정 가능
    ESslTlsProtocol MaximumProtocol = ESslTlsProtocol::Maximum;
    bool bAllowCompression = true;
    bool bAddCertificates  = true;   // 플랫폼 루트 저장소 자동 주입
};

// ISslCertificateManager.h
virtual void AddCertificatesToSslContext(SSL_CTX*) const = 0;
virtual void SetPinnedPublicKeys(const FString& Domain, const FString& PinnedKeyDigests) = 0;
//   PinnedKeyDigests: "Semicolon separated base64 encoded SHA256 digests of pinned public keys"
//   Domain 이 '.' 로 시작하면 서브도메인 매칭
virtual bool IsDomainPinned(const FString& Domain) = 0;
virtual bool VerifySslCertificates(X509_STORE_CTX* Context, const FString& Domain) const = 0;
```

> **엔진이 SPKI 핀 고정을 내장하고 있다.** 핀 형식이 우리 설정(`TlsPinnedPublicKeyHashes`, base64 SHA-256)과 정확히 같은 단위라 `;` 로 join 해 넘기면 된다. `i2d_X509_PUBKEY` + `SHA256` + `FBase64::Encode` 를 손으로 짤 필요가 없고, 다이제스트 계산 실수 위험도 사라진다.

`FSslModule::Get()` 은 `Ssl.h` 하나만 include 하면 매니저 둘 다 얻을 수 있다. 나머지는 OpenSSL 원시 호출(`SSL_new`, `SSL_do_handshake`, `SSL_read/write`)이며 이는 엔진이 감싸주지 않는다.

**검증 정책과 다운그레이드 금지 규칙은 어떤 경우에도 바꾸지 않는다.**

- [ ] **Step 1: `Build.cs`에 SSL 의존 추가**

`HermesAgentNPC.Build.cs`의 `PublicDependencyModuleNames` 블록 **아래**에 추가한다.

```csharp
		// TLS 는 SSL 모듈(OpenSSL)로 구현한다. 플랫폼에 따라 없을 수 있으므로
		// 관련 코드는 전부 #if WITH_SSL 로 감싼다.
		PrivateDependencyModuleNames.Add("SSL");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
```

- [ ] **Step 2: TLS 전송 헤더 작성**

`Transport/HermesTlsTransport.h` 전체 내용:

```cpp
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
	/** SSL_CTX 를 만들고 검증 정책(6.4)을 적용한다. */
	bool CreateContext(const FHermesTlsConfig& Tls, const FString& ServerName);

	/** 논블로킹 핸드셰이크. bStop 신호와 타임아웃을 매 반복 확인한다. */
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
```

- [ ] **Step 3: TLS 전송 구현**

`Transport/HermesTlsTransport.cpp` 전체 내용:

```cpp
#include "Transport/HermesTlsTransport.h"
#include "Transport/HermesTlsPolicy.h"
#include "IPAddress.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#if WITH_SSL
#include "Ssl.h"   // ISslManager, ISslCertificateManager, FSslModule
THIRD_PARTY_INCLUDES_START
#include <openssl/ssl.h>
#include <openssl/x509.h>
THIRD_PARTY_INCLUDES_END
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <sys/socket.h>
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
	UE_LOG(LogTemp, Error,
		TEXT("[Hermes] TLS requested but SSL module is unavailable on this platform. "
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

		// FInternetAddr 에서 sockaddr 을 얻는다.
		// UE 5.8 에서는 GetRawIp()/GetPort() 로 재구성하거나 플랫폼별 접근자를 쓴다.
		// 구현 시 설치된 엔진의 IPAddress.h 를 확인해 정확한 경로를 택한다.
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

	// 호스트명 검증 대상. 핀 모드에서는 체인 검증을 우회하지만
	// 이름 설정 자체는 남겨 두어 모드 전환 시 누락되지 않게 한다.
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
			UE_LOG(LogTemp, Error,
				TEXT("[Hermes] TLS public key pin mismatch for '%s'. Refusing connection."),
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
			UE_LOG(LogTemp, Error,
				TEXT("[Hermes] TLS certificate verification failed for '%s' (code %ld). "
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
		UE_LOG(LogTemp, Error, TEXT("[Hermes] failed to initialize SSL"));
		return false;
	}
	bSslInitialized = true;

	const HermesTls::EVerifyMode Mode =
		HermesTls::ResolveVerifyMode(Tls.PinnedPublicKeyHashes, Tls.PrivateCaPath);

	FSslContextCreateOptions Options;
	Options.MinimumProtocol = ESslTlsProtocol::TLSv1_2;   // TLS 1.2 미만 비활성화
	Options.bAllowCompression = false;                    // CRIME 류 회피
	// 핀 모드에서는 시스템 루트를 신뢰 근거로 쓰지 않는다. 자체 서명 인증서가
	// 정상 경로이기 때문이며, 신뢰는 아래에서 등록하는 SPKI 핀이 담당한다.
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
		// 엔진이 SPKI 핀 고정을 내장하고 있다. 형식은 세미콜론으로 구분된
		// base64 SHA-256 다이제스트로, 우리 설정 배열과 정확히 같은 단위다.
		// 직접 i2d_X509_PUBKEY + SHA256 + base64 를 돌릴 필요가 없다.
		const FString Joined = FString::Join(Tls.PinnedPublicKeyHashes, TEXT(";"));
		Certs.SetPinnedPublicKeys(ServerName, Joined);

		// 검증은 핸드셰이크 후 VerifyPinnedKey() 가 수행한다.
		// SSL_VERIFY_NONE 은 검증을 "끄는" 것이 아니라 체인 검증을 핀으로
		// "대체"하는 것이며, 핀 불일치는 Connect() 에서 무조건 연결을 거부한다.
		SSL_CTX_set_verify(Ctx, SSL_VERIFY_NONE, nullptr);
		break;
	}

	case HermesTls::EVerifyMode::PrivateCa:
	{
		SSL_CTX_set_verify(Ctx, SSL_VERIFY_PEER, nullptr);
		const FString AbsPath = Tls.PrivateCaPath; // 게임 스레드에서 절대 경로로 변환됨
		if (SSL_CTX_load_verify_locations(Ctx, TCHAR_TO_ANSI(*AbsPath), nullptr) != 1)
		{
			UE_LOG(LogTemp, Error, TEXT("[Hermes] failed to load private CA: %s"), *AbsPath);
			return false;
		}
		break;
	}

	case HermesTls::EVerifyMode::SystemCa:
	default:
		// Options.bAddCertificates 가 true 라 CreateSslContext 가 이미
		// 플랫폼 루트 저장소를 주입했다. 별도 호출이 필요 없다.
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
			UE_LOG(LogTemp, Error, TEXT("[Hermes] TLS handshake failed (ssl error %d)"), Err);
			return false;
		}

		if (FPlatformTime::Seconds() >= Deadline)
		{
			UE_LOG(LogTemp, Error, TEXT("[Hermes] TLS handshake timed out after %.1fs"), TimeoutSeconds);
			return false;
		}

		// 워커 루프와 같은 양보 주기를 쓴다. 100ms 안에 중단에 반응할 수 있도록
		// 짧게 잔다 (호출자인 ConnectSocket 은 Run() 루프 안에 있다).
		FPlatformProcess::Sleep(0.01f);
	}
}

bool FHermesTlsTransport::VerifyPinnedKey(const FString& ServerName) const
{
	// 엔진의 인증서 관리자가 SPKI 다이제스트 비교를 수행한다.
	// CreateContext() 에서 SetPinnedPublicKeys(ServerName, ...) 로 등록해 두었다.
	ISslCertificateManager& Certs = FSslModule::Get().GetCertificateManager();

	if (!Certs.IsDomainPinned(ServerName))
	{
		// 등록에 실패했다면 검증할 근거가 없다. 통과시키지 않는다.
		UE_LOG(LogTemp, Error,
			TEXT("[Hermes] pins were configured but not registered for '%s'"), *ServerName);
		return false;
	}

	X509_STORE_CTX* StoreCtx = X509_STORE_CTX_new();
	if (!StoreCtx)
	{
		return false;
	}

	// 피어가 보낸 체인을 그대로 검증 컨텍스트에 얹는다.
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
		UE_LOG(LogTemp, Error,
			TEXT("[Hermes] server public key does not match any configured pin for '%s'"),
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
		// SSL_CTX_free 가 아니라 매니저에 돌려준다. 생성을 매니저가 했다.
		FSslModule::Get().GetSslManager().DestroySslContext(Ctx);
		Ctx = nullptr;
	}
	if (bSslInitialized)
	{
		// InitializeSsl 은 참조 계수라 짝을 맞춰야 한다.
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
	// SSL 내부 버퍼에 이미 복호화된 데이터가 있거나, 소켓에 읽을 것이 있으면 true.
	const int32 Pending = SSL_pending(Ssl);
	if (Pending > 0)
	{
		OutBytes = (uint32)Pending;
		return true;
	}

	// 소켓 레벨 확인: 논블로킹 recv 로 엿본다.
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
		return 0; // 지금은 없음. 오류가 아니다.
	}
	return -1; // 치명적 → 재연결
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
		return 0; // 지금은 불가. 호출자가 재시도한다.
	}
	return -1;
#else
	return -1;
#endif
}
```

- [ ] **Step 4: 워커에서 전송 구현 선택**

`Transport/HermesSocketWorker.cpp`의 include에 추가한다.

```cpp
#include "Transport/HermesTlsTransport.h"
```

`ConnectSocket()`의 전송 생성부를 교체한다.

```cpp
	// TLS 실패는 평문 폴백으로 이어지지 않는다. 연결을 닫고 백오프 재연결에 맡긴다.
	if (Config.Tls.bUseTLS)
	{
		Transport = MakeUnique<FHermesTlsTransport>();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Hermes] connecting WITHOUT TLS to %s:%d — development only"),
			*Config.Host, Config.Port);
		Transport = MakeUnique<FHermesPlainTransport>();
	}

	if (!Transport->Connect(Config, *InetAddr))
	{
		Transport.Reset();
		return false;
	}
```

- [ ] **Step 5: 구독 시스템에서 TLS 설정 주입**

`Connection/HermesConnectionSubsystem.cpp`의 include에 추가한다.

```cpp
#include "Transport/HermesTlsPolicy.h"
#include "Misc/Paths.h"
```

`Initialize()`의 `Cfg.MaxOutboundQueueSize = ...` 아래에 추가한다.

```cpp
	// Shipping 에서는 설정이 false 여도 TLS 를 강제한다. 배포된 게임이
	// 설정 실수로 평문 통신하는 상황을 만들 수 없게 한다.
#if UE_BUILD_SHIPPING
	constexpr bool bShipping = true;
#else
	constexpr bool bShipping = false;
#endif
	Cfg.Tls.bUseTLS = HermesTls::ResolveUseTls(Settings->bUseTLS, bShipping);
	if (Settings->bUseTLS != Cfg.Tls.bUseTLS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Hermes] bUseTLS=false is ignored in Shipping builds; TLS is enforced"));
	}

	Cfg.Tls.ServerName              = Settings->TlsServerName;
	Cfg.Tls.PinnedPublicKeyHashes   = Settings->TlsPinnedPublicKeyHashes;
	Cfg.Tls.HandshakeTimeoutSeconds = Settings->TlsHandshakeTimeoutSeconds;

	// 워커 스레드가 경로 API 를 쓰지 않도록 게임 스레드에서 절대 경로로 바꾼다.
	if (!Settings->TlsPrivateCaPath.IsEmpty())
	{
		Cfg.Tls.PrivateCaPath =
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Settings->TlsPrivateCaPath);
	}
```

- [ ] **Step 6: 빌드 및 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: 빌드 성공, 테스트 20종 통과.

> 빌드가 SSL API 이름으로 실패하면 `Engine/Source/Runtime/Online/SSL/Public`의 실제 헤더를 열어 대응 함수를 찾는다. **검증 정책(6.4)과 다운그레이드 금지(6.5) 규칙은 바꾸지 않는다** — API 호출 방법만 조정한다.

- [ ] **Step 7: 평문 경로 회귀 확인**

`Config/DefaultGame.ini`에 `bUseTLS=False`가 있는 상태로 서버에 접속해 대화와 액션이 이전과 동일하게 동작하는지 확인한다.

Expected: 정상 동작. 매 연결마다 `connecting WITHOUT TLS` 경고 로그가 남는다.

- [ ] **Step 8: 커밋**

```powershell
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC
git commit -m "feat: OpenSSL 기반 TLS 전송 구현"
```

커밋 본문에 포함할 내용:

```
FSocket 을 TLS 로 감싸지 않고 OpenSSL 이 소켓을 직접 소유한다.
FSocket 의 네이티브 핸들을 얻는 공개 API 가 없기 때문이다.
주소 해석은 여전히 ISocketSubsystem 이 담당하므로 IPv4 우선 정책과
DNS 동작이 평문 경로와 동일하게 유지된다.

검증 정책 3종:
- 핀: SPKI SHA-256 이 일치해야 한다. 자체 서명을 허용하되 정확히 그
  키만 신뢰한다. 검증을 끄는 것이 아니라 핀으로 대체하는 것이다
- 사설 CA: 지정된 PEM 을 신뢰 저장소에 추가하고 표준 체인+호스트명 검증
- 기본: 시스템 루트 CA 로 표준 검증

다운그레이드 금지:
- TLS 실패는 평문 폴백으로 이어지지 않고 백오프 재연결에 맡긴다
- WITH_SSL 이 없는 플랫폼에서는 연결을 시도하지 않는다
- Shipping 에서는 bUseTLS=False 를 무시하고 강제한다

핸드셰이크는 논블로킹 루프로 진행하며 타임아웃 상한을 둔다.
```

---

# Phase 5 — 문서와 통합 검증

## Task 17: 프로토콜 문서 v2 개정 — ✅ 완료 (앞당겨 실행)

> **이 태스크는 이미 실행되었다.** 커밋 `7e8e53d`(v2 개정)와 `10d0279`(`action_event` 추가).
>
> 서버를 병행 재구축하는 상황이라 Phase 5까지 미룰 수 없었다. 이 문서는 코드에 의존하지 않는 순수 계약이고, 없으면 서버 작업 자체가 시작될 수 없다. 실제 결과물이 아래 Step 들보다 넓다 — `action_event`(§4.10), 서버 체크리스트(§7b), 스텁 서버 목록(§8), "What changed from v1" 표가 추가로 들어갔다.
>
> 아래 Step 들은 **기록용으로 남긴다.** 다시 실행하지 말 것.

**Files:**
- Modify: `ue5-socket-protocol.md` (프로젝트 루트)

**Interfaces:**
- Consumes: Task 9~13, 16의 프로토콜 변경 전부
- Produces: 서버 재구축의 계약 문서

**왜 이 태스크가 필요한가:** 이 문서는 "서버 코드를 읽지 않고 클라이언트를 구현할 수 있다"고 선언한 자기완결적 계약이고 실제로 그 역할을 한다. 서버를 새로 짓는 쪽이 읽을 유일한 명세이므로 구현과 어긋나면 안 된다.

- [ ] **Step 1: 헤더 전송 정의 교체**

`ue5-socket-protocol.md:1-11`의 머리말을 교체한다.

```markdown
# UE5 ↔ Hermes Socket Protocol (v2)

This document fully specifies the protocol between an Unreal Engine 5
client (the NPC) and the Hermes agent server. It is **self-contained**: you can
implement either side from this document alone.

- **Port:** `8770` (TCP)
- **Transport:** **TLS 1.2+ over TCP**, carrying length-prefixed JSON frames
- **Text encoding:** UTF-8 everywhere
- **Protocol version:** 2

> **Server address is client configuration, not part of this protocol.**
> The UE5 client reads it from project settings; see the plugin README.

## 0. TLS requirements

- The server **must** present a certificate. TLS 1.2 is the minimum version.
- For LAN deployments a self-signed certificate is expected. In that case the
  server operator **must** publish the SPKI SHA-256 pin so clients can pin it:

  ```
  openssl x509 -in server.crt -pubkey -noout \
    | openssl pkey -pubin -outform der \
    | openssl dgst -sha256 -binary \
    | openssl enc -base64
  ```

- Pins are on the **public key (SPKI)**, not the certificate. Renewing the
  certificate with the same key pair keeps existing pins valid.
- Plaintext TCP is **development only** and must not be used in deployed
  configurations. Clients do not fall back to plaintext when TLS fails.
```

- [ ] **Step 2: §3 핸드셰이크 다이어그램 교체**

기존 시퀀스 다이어그램과 **Rules** 목록을 교체한다.

```markdown
### First connection — server issues the identity

```
UE5 client                              Hermes server
    |  TLS connect :8770                     |
    |--------------------------------------->|
    |  identify {protocol_version:2,         |
    |            player_name?}               |
    |--------------------------------------->|
    |        identified {ok, player_id,      |
    |                    session_token,      |
    |                    chat_id}            |
    |<---------------------------------------|
```

### Reconnection — client proves the identity

```
    |  identify {protocol_version:2,         |
    |            player_id, session_token,   |
    |            player_name?}               |
    |--------------------------------------->|
    |        identified {ok, chat_id}        |
    |<---------------------------------------|
```

**Rules**

- The client **must** send `identify` first. Any `chat` before it is rejected
  with an `error` frame.
- **The server issues the identity.** Clients must not invent a `player_id`.
  An `identify` without credentials is a request to issue new ones.
- On reconnect the client sends both `player_id` and `session_token`. The
  server validates the pair; a mismatch is `not_authorized` and closes the
  connection. Knowing a `player_id` alone must not grant access to that session.
- `session_token` must be high-entropy and **must not be derivable** from
  `player_id`. Leaking one must not reveal the other.
- The client sends `protocol_version: 2`. If `identified` arrives without a
  `session_token`, the client treats the peer as v1, logs an error, and
  reconnects. **There is no v1 compatibility mode** — a compatibility path is
  a path that works without authentication.
```

- [ ] **Step 3: §4.1 / §4.2 필드 표 갱신**

`identify` 표를 교체한다.

```markdown
| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `protocol_version` | int | yes | Always `2`. |
| `player_id` | string | no | Omit to request new credentials. |
| `session_token` | string | no | Required whenever `player_id` is present. |
| `player_name` | string | no | Display name. |
```

`identified` 표를 교체한다.

```markdown
| Field | Type | Notes |
|-------|------|-------|
| `ok` | bool | `true` on success. |
| `player_id` | string | Server-issued. Sent on issuance; may be echoed on reconnect. |
| `session_token` | string | Server-issued credential. **Required** — its absence marks a v1 server. |
| `chat_id` | string | Conversation id for this player. |
```

- [ ] **Step 4: §4.9 `chat_delta` 신규 절 추가**

`4.8 error` 절 **앞에** 삽입한다.

```markdown
### 4.9 `chat_delta` — Server → Client

Incremental piece of an in-progress reply.

```json
{ "type": "chat_delta", "id": "c-0001", "seq": 0, "text": "알겠어요, " }
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `id` | string | yes | Echoes the triggering `chat.id`. |
| `seq` | int | no | Ordering hint. Clients may ignore it. |
| `text` | string | yes | Text fragment to append. |

**`chat_response.text` is authoritative.** Deltas are a display hint: the
client accumulates them for immediate feedback and then replaces the display
with the final text. A client that ignores deltas entirely still works, and a
client that drops or double-processes one still ends up correct.

> **Servers must batch deltas.** One frame per token produces tens to hundreds
> of frames per second, which pressures the client's per-tick frame budget and
> inbound queue cap during *normal* operation. Flush every **~50 ms or every
> few tokens**. Clients do not raise their caps to accommodate unbatched
> streams — those caps exist to stop abusive peers, and a well-behaved server
> should never approach them.

Deltas also serve as progress signals: a client that has not seen a delta or a
final response within its chat timeout treats the request as failed.
```

- [ ] **Step 5: §4.7 ping/pong 절 교체**

```markdown
### 4.7 `ping` / `pong` — keepalive

Either side may send `ping`; the peer replies `pong` with the same `id`.

```json
{ "type": "ping", "id": "k-0012" }
{ "type": "pong", "id": "k-0012" }
```

**Both sides should treat receive-silence as death.** The UE5 client sends a
`ping` after a configurable idle period (default 20 s) and declares the
connection dead if **no frame of any type** arrives within its peer timeout
(default 60 s), then reconnects. Servers are recommended to apply the same
rule so half-open connections are reaped on both ends.

Any received frame counts as liveness, not just `pong` — an active
conversation keeps the connection alive without extra pings.
```

- [ ] **Step 6: §5 에러 코드 표 갱신**

`not_authorized` 행을 교체한다.

```markdown
| `not_authorized` | `player_id` / `session_token` pair did not validate. **Required in v2.** | closed |
```

- [ ] **Step 7: §6 액션 카탈로그에 클라이언트 제약 명시**

§6 도입 문단 뒤에 추가한다.

```markdown
> **The client enforces hard bounds before executing anything.** Values outside
> these ranges are rejected with `ok=false` and a descriptive `error`, and the
> action never reaches game code. Server implementers should constrain
> generation to the same ranges (see below) rather than relying on the client
> to reject bad output.
>
> | Constraint | Default | `error` on violation |
> |---|---|---|
> | `move_to` coordinates finite, `abs(v) <= MaxWorldCoordinate` | 1e7 cm | `coordinate out of range` |
> | `item_transfer.quantity` integer, `1 <= q <= MaxItemQuantity` | 999999 | `quantity out of range` |
> | `item_transfer.item_id` non-empty, length `<= MaxItemIdLength` | 64 | `invalid item_id` |
> | Action requests per second | 20 | `rate limited` |
> | Response deadline | 15 s | `timeout` |
>
> **Strongly recommended for servers:** constrain LLM output with a JSON schema
> or GBNF grammar so `command` can only be one of the whitelisted values and
> numeric parameters carry `minimum`/`maximum`. This makes out-of-catalog
> commands and out-of-range values *impossible to generate* rather than merely
> rejected afterwards, which is the only effective mitigation against prompt
> injection steering tool use.
```

- [ ] **Step 8: §7 체크리스트와 §8 참조 클라이언트 갱신**

§7의 1번 항목을 교체한다.

```markdown
1. Open a **TLS** connection to the configured endpoint (address comes from
   client configuration, not from this document).
2. Send `identify` with `protocol_version: 2`, including `player_id` and
   `session_token` if you have them stored. Wait for `identified`; persist any
   credentials it returns.
```

§8에 한 줄 추가한다.

```markdown
> The reference Python client must be updated for v2: TLS, `protocol_version`,
> and credential persistence. A v1 client will be rejected at the `identified`
> step.
```

- [ ] **Step 9: 문서 정합성 확인**

```powershell
Select-String -Path "ue5-socket-protocol.md" -Pattern "192\.168\.0\.111"
Select-String -Path "ue5-socket-protocol.md" -Pattern "Protocol version: 1"
```

Expected: 둘 다 출력 없음.

- [ ] **Step 10: 커밋**

```powershell
git add ue5-socket-protocol.md
git commit -m "docs: 프로토콜 문서를 v2로 개정"
```

커밋 본문에 포함할 내용:

```
서버 재구축이 읽을 유일한 계약 문서다. 구현과 어긋나면 안 된다.

- 전송을 TLS 1.2+ 위의 TCP 로 규정. 프레이밍 규격 자체는 불변이다
- 자체 서명 LAN 배포를 위한 SPKI 핀 발행 절차를 §0 에 명시
- identify/identified 를 서버 발급 + 재접속 검증 두 경로로 재정의
- chat_delta 신규 절. 델타 배치를 서버 요구사항으로 못박는다.
  토큰당 프레임 1개면 정상 동작 중에 클라이언트 상한을 압박한다
- ping/pong 을 양방향 keepalive 로 확장하고 수신 침묵 판정을 규정
- not_authorized 를 선택 사항에서 필수로 변경
- §6 에 클라이언트가 강제하는 범위와 레이트 리밋을 표로 명시하고,
  문법 제약 디코딩을 서버측 권고로 추가

하드코딩된 서버 주소를 문서에서 제거한다. 주소는 클라이언트 설정이지
프로토콜의 일부가 아니다.
```

---

## Task 18: README와 HTML 문서 갱신

**Files:**
- Modify: `README.md`
- Modify: `HermesAgentNPC_Documentation.html`

**Interfaces:**
- Consumes: `UHermesSettings` 전체 (Task 1), TLS 설정 (Task 16)
- Produces: 플러그인 도입 개발자용 설정 가이드

- [ ] **Step 1: README에 설정 절 추가**

`README.md`의 "🚀 빠른 시작 및 사용 방법" 절 **뒤에** 새 절을 삽입한다.

````markdown
---

## ⚙️ 서버 설정 (Server Configuration)

서버 주소는 플러그인이 아니라 **도입 프로젝트의 설정**입니다. 플러그인 소스에는 주소가 없습니다.

### 1. 에디터에서 설정

**Edit > Project Settings > Plugins > Hermes Agent NPC**

| 항목 | 기본값 | 설명 |
| :--- | :--- | :--- |
| `Host` | `127.0.0.1` | 백엔드 호스트명 또는 IP. 도메인명 사용 가능 |
| `Port` | `8770` | TCP 포트 |
| `Use TLS` | `true` | Shipping 빌드에서는 `false`여도 강제로 켜집니다 |
| `Tls Server Name` | (비움) | 인증서 검증에 쓸 이름. IP로 접속할 때 지정 |
| `Tls Pinned Public Key Hashes` | (비움) | 서버 공개키 SPKI SHA-256 (base64) |
| `Tls Private Ca Path` | (비움) | 사설 CA PEM 경로 (프로젝트 기준 상대 경로) |

값은 `Config/DefaultGame.ini`에 저장됩니다.

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=hermes.example.com
Port=8770
bUseTLS=True
+TlsPinnedPublicKeyHashes=YOUR_BASE64_SPKI_HASH_HERE
```

### 2. TLS 인증서 설정

LAN 서버(`192.168.x.x` 같은 사설 IP)는 공인 인증서를 발급받을 수 없으므로 **자체 서명 인증서 + 공개키 핀 고정**을 사용합니다.

서버 인증서에서 핀 값을 뽑습니다.

```bash
openssl x509 -in server.crt -pubkey -noout \
  | openssl pkey -pubin -outform der \
  | openssl dgst -sha256 -binary \
  | openssl enc -base64
```

출력된 base64 문자열을 `TlsPinnedPublicKeyHashes`에 넣습니다.

> **핀은 인증서가 아니라 공개키(SPKI)에 겁니다.** 인증서를 갱신해도 키쌍만 유지하면 클라이언트를 다시 배포할 필요가 없습니다.

사내 CA를 운영한다면 핀 대신 `TlsPrivateCaPath`에 CA PEM 경로를 지정해도 됩니다. 공인 인증서를 쓰는 경우 둘 다 비워두면 시스템 루트 CA로 검증합니다.

### 3. 빌드별 서버 전환

Development / Test 빌드에서는 실행 인자로 재컴파일 없이 서버를 바꿀 수 있습니다.

```cmd
YourGame.exe -HermesHost=10.0.0.5 -HermesPort=9000
```

> **Shipping 빌드에서는 이 인자가 무시됩니다.** 최종 사용자가 클라이언트를 임의 서버로 붙이지 못하게 하기 위함입니다. TLS 설정에는 커맨드라인 오버라이드가 아예 없습니다 — 검증 정책을 실행 인자로 낮출 수 있으면 그 자체가 취약점입니다.

### 4. 개발 중 평문 사용

서버에 아직 인증서가 없다면 `bUseTLS=False`로 두고 작업할 수 있습니다. 이 경우 **연결할 때마다 경고 로그**가 남으며, Shipping 빌드에서는 이 설정이 무시됩니다.

---

## 🔐 보안 모델 (Security Model)

| 위협 | 상태 |
| :--- | :--- |
| `player_id`를 알아낸 제3자의 사칭 | ✅ 차단 — 신원은 서버가 발급하고 `session_token`으로 검증 |
| 경로상 공격자의 도청 | ✅ 차단 — TLS |
| 경로상 공격자의 `action_request` 주입 | ✅ 차단 — TLS + 인증서 검증 |
| 가짜 서버로의 유인 | ✅ 차단 — 인증서 핀 고정 또는 CA 검증 |
| 악성 피어의 클라이언트 자원 고갈 | ✅ 차단 — 큐 상한, 틱 예산, 레이트 리밋 |
| 비정상 파라미터로 인한 크래시 | ✅ 차단 — 파라미터 하드 바운드 |
| **기기 소유자의 세션 토큰 추출** | ⚠️ **의도적으로 미해결** |
| 서버측 자원 고갈, 프롬프트 인젝션 | ❌ 서버 구현 책임 |

세션 토큰은 `SaveGame`에 평문으로 저장됩니다. 이를 숨기려는 시도(난독화·암호화)는 하지 않습니다 — 클라이언트에 심은 비밀은 그 기기 소유자에게 결국 노출되므로 obscurity에 불과합니다. 막는 대상은 **"내 `player_id`를 알아낸 다른 사람"**이지 **"내 PC를 뜯는 나 자신"**이 아닙니다.
````

- [ ] **Step 2: README의 액션 카탈로그 표에 검증 열 추가**

"📜 지원 액션 명령 스펙" 절의 표 **아래**에 추가한다.

```markdown
클라이언트는 실행 전에 파라미터를 검증하고, 범위를 벗어나면 게임 코드에 닿기 전에 `ok=false`로 거부합니다.

| 제약 | 기본값 | 위반 시 `error` |
| :--- | :--- | :--- |
| 좌표 유한성 및 `abs(v) <= MaxWorldCoordinate` | 1e7 cm | `coordinate out of range` |
| `quantity` 정수, `1 <= q <= MaxItemQuantity` | 999999 | `quantity out of range` |
| `item_id` 비어있지 않고 길이 제한 | 64자 | `invalid item_id` |
| 초당 액션 처리 수 | 20 | `rate limited` |
```

- [ ] **Step 3: README의 테스트 배지와 개수 갱신**

상단 배지를 교체한다.

```markdown
[![Automation Tests](https://img.shields.io/badge/Tests-20%2F20%20PASS-success.svg)]()
```

"🧪 빌드 & 자동화 테스트" 절의 제목 `(5/5 PASS)`를 `(20/20 PASS)`로 바꾼다.

- [ ] **Step 4: 플러그인 구조 트리 갱신**

"📁 플러그인 구조" 절의 트리에서 `Source/HermesAgentNPC/` 하위 목록을 교체한다.

```text
    │   └── HermesAgentNPC/                 <-- C++ 핵심 모듈
    │       ├── Actions/                    <-- 디스패처, 핸들러 4종, 파라미터 검증, 레이트 리밋
    │       ├── Connection/                 <-- 연결 서브시스템, 자격 증명, liveness, 발화 추적
    │       ├── Inventory/                  <-- 인벤토리 컴포넌트 & 아이템
    │       ├── NPC/                        <-- NPC 캐릭터 & AIController
    │       ├── Protocol/                   <-- 프레이밍 코덱 & 메시지 JSON (v2)
    │       ├── Settings/                   <-- UDeveloperSettings 설정 클래스
    │       ├── Transport/                  <-- 소켓 워커, 평문/TLS 전송, TLS 정책
    │       └── UI/                         <-- UMG 대화 위젯 C++
```

- [ ] **Step 5: HTML 문서 갱신**

`HermesAgentNPC_Documentation.html`에서 다음을 수정한다.

1. `192.168.0.111`이 나오는 모든 위치를 "프로젝트 설정에서 지정" 취지의 설명으로 교체
2. 전송 설명을 "TLS 1.2+ 위의 길이 프리픽스 TCP"로 갱신
3. 프로토콜 버전 표기를 v2로 갱신하고 `chat_delta`를 메시지 목록에 추가
4. 아키텍처 다이어그램에 `Settings` / `Transport(TLS)` 구성 요소 반영
5. 테스트 개수 5 → 20

```powershell
Select-String -Path "HermesAgentNPC_Documentation.html" -Pattern "192\.168\.0\.111"
```

Expected: 수정 후 출력 없음.

- [ ] **Step 6: 최종 하드코딩 확인**

```powershell
Select-String -Path "README.md","HermesAgentNPC_Documentation.html","ue5-socket-protocol.md" -Pattern "192\.168\.0\.111"
Select-String -Path "Plugins\HermesAgentNPC\Source\HermesAgentNPC\*" -Pattern "192\.168\.0\.111" -Recurse
```

Expected: 둘 다 출력 없음. `Config/DefaultGame.ini`에만 남아 있어야 한다.

- [ ] **Step 7: 커밋**

```powershell
git add README.md HermesAgentNPC_Documentation.html
git commit -m "docs: README·HTML 문서를 설정 기반 및 v2 사양으로 갱신"
```

커밋 본문에 포함할 내용:

```
서버 주소가 플러그인 사양처럼 기술된 부분을 걷어낸다. 주소는 도입
프로젝트의 설정이지 플러그인의 일부가 아니다.

- Project Settings 경로, ini 예시, 커맨드라인 인자 사용법
- TLS 설정 절: SPKI 핀 생성 명령, 사설 CA, 개발 중 평문 사용 주의
- 보안 모델 표: 무엇이 차단되고 무엇이 의도적으로 미해결인지 명시.
  기기 소유자의 토큰 추출은 막지 않으며 막을 방법도 없다
- 액션 파라미터 하드 바운드 표
```

---

## Task 19: 통합 검증

**Files:** 없음 (검증 전용)

**Interfaces:**
- Consumes: Task 1~18 전부
- Produces: 완료 판정

**이 태스크는 코드를 바꾸지 않는다.** 실패 항목이 나오면 해당 Task로 돌아가 고치고 다시 이 태스크를 수행한다.

- [ ] **Step 1: 전체 자동화 테스트**

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

Expected: **20종 전부 PASS.**

| 테스트 | 출처 |
|---|---|
| `Hermes.Protocol.FrameCodec.Encode` | 기존 |
| `Hermes.Protocol.FrameAccumulator.Parse` | 기존 |
| `Hermes.Protocol.Messages.*` (기존분) | 기존 |
| `Hermes.Actions.Dispatcher.*` | 기존 |
| `Hermes.Inventory.AddRemove` | 기존 |
| `Hermes.Settings.CommandLineOverride` | Task 1 |
| `Hermes.ActionParams.Coordinate` / `.Quantity` / `.ItemId` | Task 5 |
| `Hermes.Inventory.AddSaturates` | Task 5 |
| `Hermes.RateLimiter.TokenBucket` | Task 6 |
| `Hermes.Util.PushBounded` | Task 8 |
| `Hermes.Protocol.Messages.IdentifyV2` / `.ParseIdentified` / `.Ping` | Task 9 |
| `Hermes.Liveness.Evaluate` | Task 12 |
| `Hermes.PendingChats.Timeout` | Task 13 |
| `Hermes.TlsPolicy.ServerName` / `.VerifyMode` / `.UseTls` | Task 15 |

- [ ] **Step 2: 설정 경로 수동 검증**

- [ ] Project Settings > Plugins > Hermes Agent NPC 화면에 모든 항목이 보인다
- [ ] 값을 바꾸면 `Config/DefaultGame.ini`에 기록된다
- [ ] `Host=localhost`로 바꾸면 호스트명이 해석된다
- [ ] `-HermesHost=<다른IP>`로 실행하면 ini를 무시하고 해당 IP로 접속을 시도한다

- [ ] **Step 3: 강건성 수동 검증**

- [ ] `MaxReconnectDelay=300`에서 서버를 끈 채 1분 이상 방치 후 PIE 정지 → **즉시 정지된다**
- [ ] 도달 불가 호스트명 → 게임 스레드가 멈추지 않고 백오프 재연결이 반복된다
- [ ] 테스트 스크립트로 프레임을 대량 전송 → 메모리가 무한 증가하지 않고, 상한 초과 시 연결이 끊긴 뒤 재연결된다
- [ ] `action_request`를 초당 100건 전송 → 20건만 처리되고 나머지는 `"rate limited"`로 회신되며 프레임이 유지된다
- [ ] TCP는 수락하되 `identified`를 보내지 않는 스텁 서버에 접속 후 발화 반복 → 메모리가 무한 증가하지 않는다
- [ ] `move_to`에 `x: 1e308` → `"coordinate out of range"` 회신, 크래시 없음
- [ ] `item_transfer`에 `quantity: 2e9` → `ok=false`, 인벤토리 수량이 음수가 되지 않는다

- [ ] **Step 4: 신원 수동 검증**

- [ ] 세이브 파일이 없는 상태로 최초 접속 → 서버가 `player_id`/`session_token`을 발급하고 `.sav`에 저장된다
- [ ] 재시작 후 접속 → 저장된 자격 증명으로 재접속하며 **이전 대화 맥락이 유지된다**
- [ ] `.sav`의 `SessionToken`을 변조 후 접속 → `not_authorized`로 거부되고 연결이 닫힌다
- [ ] 다른 플레이어의 `player_id`만 넣고 토큰은 자기 것으로 접속 → 거부된다
- [ ] v1 서버(구버전)에 접속 → 명확한 에러 로그와 함께 실패하고 **조용히 동작하지 않는다**

- [ ] **Step 5: 세션 운영 수동 검증**

- [ ] 응답이 한 번에 나오지 않고 **이어서 출력된다**
- [ ] 델타를 보내다 중단하는 스텁 서버 → 타임아웃 후 실패로 표시되고 `"생각 중..."`이 남지 않는다
- [ ] 60초 이상 걸리는 긴 응답이 델타를 계속 보내는 동안 → **타임아웃되지 않는다**
- [ ] 연결된 상태에서 서버 프로세스를 강제 종료(정상 FIN 없이) → `PeerTimeoutSeconds` 안에 죽은 연결로 판정하고 재연결한다. **플레이어가 말을 걸 때까지 기다리지 않는다**
- [ ] 유휴 상태를 `KeepAlivePingIntervalSeconds` 이상 유지 → 클라이언트가 ping을 보내고 pong을 받는다
- [ ] 이전 발화의 늦은 응답이 도착 → 현재 화면을 덮어쓰지 않는다

- [ ] **Step 6: TLS 수동 검증**

- [ ] 올바른 인증서와 일치하는 핀 → 접속 성공, 대화·액션이 정상 동작한다
- [ ] **핀을 한 글자 바꿈** → 연결이 거부되고 사유가 로그에 남는다
- [ ] 핀 미설정 + 사설 CA 지정 + 그 CA로 서명된 인증서 → 접속 성공
- [ ] 핀 미설정 + 사설 CA 미지정 + 자체 서명 인증서 → 연결 거부 (기본 CA로 검증 불가)
- [ ] `TlsServerName`을 인증서와 다른 이름으로 설정 → 호스트명 불일치로 거부
- [ ] **평문 서버에 `bUseTLS=true`로 접속** → 핸드셰이크 실패, **평문으로 폴백하지 않고** 백오프 재연결만 반복
- [ ] 핸드셰이크 도중 PIE 정지 → 즉시 종료된다
- [ ] Shipping 구성에서 `bUseTLS=False` 설정 → 강제로 TLS가 사용되고 에러 로그가 남는다
- [ ] Wireshark 등으로 패킷 캡처 → `session_token`과 대화 내용이 **평문으로 보이지 않는다**

- [ ] **Step 7: 완료 조건 대조**

설계 문서 9절의 완료 조건을 하나씩 대조한다. 미충족 항목이 있으면 해당 Task로 돌아간다.

```powershell
Select-String -Path "docs\superpowers\specs\2026-07-28-hermes-settings-globalization-design.md" -Pattern "^- \[ \]"
```

- [ ] **Step 8: 프로젝트 ini를 최종 상태로**

`Config/DefaultGame.ini`에서 Task 2 Step 6의 임시값 `bUseTLS=False`를 실제 배포 값으로 바꾼다.

```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=192.168.0.111
Port=8770
bUseTLS=True
TlsServerName=hermes.local
+TlsPinnedPublicKeyHashes=<실제 서버 SPKI 해시>
```

- [ ] **Step 9: 최종 커밋**

```powershell
git add Config/DefaultGame.ini
git commit -m "chore: 프로젝트 설정을 TLS 활성화 상태로 전환"
```

커밋 본문에 포함할 내용:

```
Phase 4 완료 전까지 임시로 두었던 bUseTLS=False 를 해제하고 서버
인증서의 SPKI 핀을 등록한다. 20종 자동화 테스트와 통합 검증 항목을
모두 통과한 상태다.
```

---

# 실행 순서 요약

각 Phase는 그 지점에서 빌드와 테스트가 통과하는 상태를 만든다. Phase 3 이후로는 v1 서버와 통신할 수 없으므로, 서버 재구축과 일정을 맞춰 머지한다.

| Phase | Task | 결과 | 테스트 수 |
|---|---|---|---|
| 1 — 설정 전역화 | 1~3 | 주소가 ini로 이동, DNS 지원 | 6 |
| 2 — 입력 강건성 | 4~8 | 자원 고갈·크래시 경로 차단 | 12 |
| 3 — 프로토콜 v2 | 9~13b | 서버 발급 신원, 스트리밍, liveness, action_event | 18 |
| 4 — TLS | 14~16 | 경로상 공격자 차단 | 21 |
| 5 — 문서·검증 | 18~19 | README·HTML 갱신, 통합 검증 | 21 |

**Phase 1과 2는 프로토콜과 무관하므로 단독으로 머지 가능하다.** 서버 재구축이 지연되면 여기까지만 먼저 반영해도 된다.
