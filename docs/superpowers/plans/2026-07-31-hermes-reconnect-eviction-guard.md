# 재접속 노출과 eviction war 방지 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `Reconnect()` 를 블루프린트에 노출하되, 반복 호출이 eviction war 로 번지지 않도록 플러그인이 직접 막는다.

**Architecture:** 재개 대기 시간 판정을 `HermesResumePolicy` 순수 함수로 분리하고, 서브시스템이 연속 정지 횟수와 정지 시각을 들고 있으면서 `Reconnect()` 호출을 승인/거부한다. 사다리는 정지 없이 끝난 건강한 연결에서만 초기화된다. 정지 사실은 새 dynamic 델리게이트로 블루프린트에 알린다.

**Tech Stack:** UE 5.8, C++, Unreal Automation Test

## Global Constraints

- 설계 문서: `docs/superpowers/specs/2026-07-31-hermes-reconnect-eviction-guard-design.md`
- 순수 판정 로직은 `HermesLiveness` / `HermesErrorPolicy` / `HermesConnectionEdge` / `HermesBackoff` 와 같은 방식으로 분리한다 — 시간도 상태도 인자로 받고, 전역을 읽지 않는다
- 주석과 테스트 이름은 한국어. 기존 파일들의 밀도와 어투를 따른다
- 기존 C++ 델리게이트(`FOnChatDelta` 등)의 dynamic 전환은 **범위 밖**이다. 건드리지 않는다
- 테스트 실행:
  `& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi`
- 빌드:
  `& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex`
- 현재 기준선: 자동화 테스트 28종 전원 PASS. 계획 완료 시 30종이 된다

## 파일 구조

| 파일 | 책임 |
|---|---|
| `Transport/HermesResumePolicy.h/.cpp` (신규) | 연속 정지 횟수 → 재개 대기 시간. 순수 함수 하나 |
| `Transport/HermesResumePolicy.spec.cpp` (신규) | 위 함수의 스펙 |
| `Connection/HermesSuspendState.h/.cpp` (신규) | 연속 정지 횟수와 연결별 플래그를 들고 있는 값 타입. 시각을 인자로 받는다 |
| `Connection/HermesSuspendState.spec.cpp` (신규) | 상태 전이 스펙 |
| `Settings/HermesSettings.h` (수정) | `ReconnectCooldownSeconds`, `MaxReconnectCooldownSeconds` 추가 |
| `Connection/HermesConnectionSubsystem.h/.cpp` (수정) | 블루프린트 표면 4개, `HermesSuspendState` 배선 |
| `HermesDebugCommands.cpp` (수정) | `Hermes.Reconnect` 콘솔 명령 |

판정을 두 조각으로 나눈 이유: `HermesResumePolicy` 는 숫자 하나를 계산하는 함수고,
`HermesSuspendState` 는 "언제 올리고 언제 내리는가"라는 상태 전이다. 둘을 한 곳에
두면 전이 테스트가 대기 시간 계산까지 함께 검증하게 되어 실패 원인이 흐려진다.

---

### Task 1: 재개 대기 시간 계산

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.spec.cpp`

**Interfaces:**
- Consumes: 없음
- Produces: `float HermesResumePolicy::RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max)`

- [ ] **Step 1: 헤더를 만든다**

`HermesResumePolicy.h`:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 의도적 재개(Reconnect)에 필요한 대기 시간.
 *
 * 종료성 에러로 재연결이 정지될 때마다 다음 재개까지의 대기가 커진다.
 * eviction war — 같은 신원의 두 인스턴스가 서로를 영원히 걷어내는 상황 — 을
 * 수렴시키기 위한 것이다. 되받아칠수록 느려지므로 한쪽이 먼저 포기한다.
 *
 * **첫 정지는 대기가 없다.** 정당하게 한 번 밀려난 사람을 벌하면 안 된다.
 * 대가는 되받아치기를 반복할 때만 발생한다.
 *
 * 워커의 자동 재연결 백오프(HermesBackoff)와 합치지 않는다. 사다리 모양은
 * 닮았지만 대상이 다르다 — 저쪽은 워커가 스스로 도는 것이고 이쪽은 게임이
 * 의도적으로 부르는 것이라, 초기값·상한·증가 시점이 모두 다르다.
 *
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesResumePolicy
{
	/**
	 * ConsecutiveSuspends 는 1부터 센다(첫 정지가 1).
	 * 1이면 0, 2면 Initial, 3이면 2배… 상한은 Max.
	 */
	float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max);
}
```

- [ ] **Step 2: 실패하는 스펙을 쓴다**

`HermesResumePolicy.spec.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Transport/HermesResumePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesResumePolicyTest,
	"Hermes.Reconnect.Cooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesResumePolicyTest::RunTest(const FString& Parameters)
{
	using HermesResumePolicy::RequiredCooldown;

	constexpr float Initial = 5.f;
	constexpr float Max     = 300.f;

	// 첫 정지는 대가가 없다. 정당하게 밀려난 사람을 벌하지 않는다.
	TestEqual(TEXT("1회는 즉시"), RequiredCooldown(1, Initial, Max), 0.f);

	// 두 번째부터 사다리가 선다.
	TestEqual(TEXT("2회는 초기값"), RequiredCooldown(2, Initial, Max), 5.f);
	TestEqual(TEXT("3회는 2배"), RequiredCooldown(3, Initial, Max), 10.f);
	TestEqual(TEXT("4회는 4배"), RequiredCooldown(4, Initial, Max), 20.f);

	// 상한을 넘지 않는다.
	TestEqual(TEXT("상한에서 멈춘다"), RequiredCooldown(20, Initial, Max), Max);

	// 0 이나 음수는 정지가 없었다는 뜻이다. 기다릴 것이 없다.
	TestEqual(TEXT("0회는 즉시"), RequiredCooldown(0, Initial, Max), 0.f);
	TestEqual(TEXT("음수도 즉시"), RequiredCooldown(-3, Initial, Max), 0.f);

	// Initial 이 Max 보다 크게 설정되어도 Max 를 넘지 않는다.
	TestEqual(TEXT("잘못된 설정에서도 상한을 지킨다"),
		RequiredCooldown(2, 1000.f, Max), Max);

	return true;
}
```

- [ ] **Step 3: 컴파일만 되는 구현을 넣는다**

`HermesResumePolicy.cpp`:

```cpp
#include "Transport/HermesResumePolicy.h"

namespace HermesResumePolicy
{
	float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max)
	{
		return 0.f;
	}
}
```

- [ ] **Step 4: 빌드하고 스펙이 실패하는지 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Reconnect.Cooldown; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\resume-red.log"
```

기대: `Result={Fail}`. "2회는 초기값", "3회는 2배", "4회는 4배", "상한에서 멈춘다",
"잘못된 설정에서도 상한을 지킨다" 가 실패한다. "1회는 즉시", "0회는 즉시",
"음수도 즉시" 는 통과한다 — 스텁이 항상 0 을 돌려주기 때문이다.

- [ ] **Step 5: 구현한다**

`HermesResumePolicy.cpp`:

```cpp
#include "Transport/HermesResumePolicy.h"
#include "Math/UnrealMathUtility.h"

namespace HermesResumePolicy
{
	float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max)
	{
		// 정지가 없었거나 첫 정지면 기다릴 것이 없다.
		if (ConsecutiveSuspends <= 1)
		{
			return 0.f;
		}

		// 2회째가 Initial, 그 뒤로 2배씩. 지수가 커지면 float 이 넘치므로
		// 곱하기 전에 상한을 걸어 둔다.
		const int32 Steps = FMath::Min(ConsecutiveSuspends - 2, 30);
		const float Scaled = Initial * FMath::Pow(2.f, static_cast<float>(Steps));
		return FMath::Min(Scaled, Max);
	}
}
```

- [ ] **Step 6: 스펙이 통과하는지 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Reconnect.Cooldown; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\resume-green.log"
```

기대: `Result={Success}`, `EXIT CODE: 0`.

- [ ] **Step 7: 커밋한다**

```bash
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.h Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.cpp Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesResumePolicy.spec.cpp
git commit -m "feat: 의도적 재개의 대기 시간 계산 분리"
```

---

### Task 2: 정지 상태 전이

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.h`
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.cpp`
- Test: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.spec.cpp`

**Interfaces:**
- Consumes: `HermesResumePolicy::RequiredCooldown` (Task 1)
- Produces: `struct FHermesSuspendState` 와 그 멤버 함수
  - `void NoteConnectionOpened()`
  - `void NoteSuspended(double NowSeconds)`
  - `void NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds)`
  - `bool IsSuspended() const`
  - `float CooldownRemaining(double NowSeconds, float Initial, float Max) const`
  - `bool TryResume(double NowSeconds, float Initial, float Max)`

- [ ] **Step 1: 헤더를 만든다**

`HermesSuspendState.h`:

```cpp
#pragma once
#include "CoreMinimal.h"

/**
 * 재연결 정지 상태와 재개 승인 판정.
 *
 * 사다리를 올리는 것과 내리는 것을 한곳에서 다룬다. 핵심 규칙은 하나다 —
 * **탈취로 끝난 연결은 사다리를 되돌리지 않는다.** 되돌리는 것은 정지 없이
 * 끝난 건강한 연결뿐이다.
 *
 * 그래야 eviction war 가 수렴한다. 싸우는 동안 이쪽 연결은 항상 탈취로
 * 끝나므로 사다리가 계속 오르고, 결국 한쪽이 오래 기다리게 되어 다른 쪽이
 * 이긴다. 의미도 분명하다 — 걷어차인 것은 재시도 권한을 벌어주지 않는다.
 *
 * 현재 시각을 인자로 받는다. 전역 시계를 읽지 않으므로 단독 테스트가 가능하다.
 */
struct FHermesSuspendState
{
	/** 새 연결이 성립했다. 이 연결에서 정지가 있었는지를 다시 센다. */
	void NoteConnectionOpened();

	/** 종료성 에러로 정지되었다. 사다리를 한 칸 올린다. */
	void NoteSuspended(double NowSeconds);

	/**
	 * 연결이 끝났다. 정지 없이 끝났고 충분히 살아 있었으면 사다리를 되돌린다.
	 * LifetimeSeconds 는 그 연결이 유지된 시간이다.
	 */
	void NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds);

	bool IsSuspended() const { return bSuspended; }

	/** 지금 재개가 거부될 때 남은 대기(초). 정지 상태가 아니면 0. */
	float CooldownRemaining(double NowSeconds, float Initial, float Max) const;

	/**
	 * 재개를 시도한다. 승인되면 정지를 풀고 true.
	 * 정지 상태가 아니거나 쿨다운 중이면 아무것도 하지 않고 false.
	 */
	bool TryResume(double NowSeconds, float Initial, float Max);

private:
	/** 연속 정지 횟수. 첫 정지가 1. */
	int32 ConsecutiveSuspends = 0;
	/** 마지막으로 정지된 시각. 대기는 이 시점부터 잰다. */
	double SuspendedAt = 0.0;
	bool bSuspended = false;
	/** 지금 연결에서 정지가 있었는가. 사다리를 되돌릴지 가르는 값이다. */
	bool bSuspendedDuringThisConnection = false;
};
```

- [ ] **Step 2: 실패하는 스펙을 쓴다**

`HermesSuspendState.spec.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Connection/HermesSuspendState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesSuspendStateTest,
	"Hermes.Reconnect.SuspendState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesSuspendStateTest::RunTest(const FString& Parameters)
{
	constexpr float Initial = 5.f;
	constexpr float Max     = 300.f;
	constexpr float Healthy = 5.f;

	// 첫 정지는 즉시 재개할 수 있다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		TestTrue(TEXT("정지됨"), S.IsSuspended());
		TestEqual(TEXT("첫 정지는 대기 없음"),
			S.CooldownRemaining(100.0, Initial, Max), 0.f);
		TestTrue(TEXT("즉시 재개된다"), S.TryResume(100.0, Initial, Max));
		TestFalse(TEXT("재개 후 정지 해제"), S.IsSuspended());
	}

    // 두 번째 정지부터 대기가 생긴다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);
		S.NoteConnectionOpened();
		S.NoteSuspended(110.0);

		TestEqual(TEXT("2회째는 5초 대기"),
			S.CooldownRemaining(110.0, Initial, Max), 5.f);
		TestFalse(TEXT("대기 중에는 거부"), S.TryResume(112.0, Initial, Max));
		TestTrue(TEXT("여전히 정지 상태"), S.IsSuspended());
		TestTrue(TEXT("대기가 지나면 승인"), S.TryResume(115.0, Initial, Max));
	}

	// 핵심: 탈취로 끝난 연결은 사다리를 되돌리지 않는다.
	// 5초를 넘게 살아 있었어도 그 연결에서 정지가 있었으면 되돌리지 않는다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.NoteConnectionClosed(/*Lifetime*/ 30.0, Healthy);
		S.TryResume(100.0, Initial, Max);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("탈취는 사다리를 되돌리지 않는다"),
			S.CooldownRemaining(200.0, Initial, Max), 5.f);
	}

	// 정지 없이 끝난 건강한 연결은 사다리를 되돌린다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);

		// 정지 없이 30초 살다 끊긴 연결
		S.NoteConnectionOpened();
		S.NoteConnectionClosed(30.0, Healthy);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("건강한 연결 뒤에는 다시 첫 정지"),
			S.CooldownRemaining(200.0, Initial, Max), 0.f);
	}

	// 짧게 끝난 연결은 정지가 없었더라도 되돌리지 않는다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		S.NoteSuspended(100.0);
		S.TryResume(100.0, Initial, Max);

		S.NoteConnectionOpened();
		S.NoteConnectionClosed(/*Lifetime*/ 0.5, Healthy);

		S.NoteConnectionOpened();
		S.NoteSuspended(200.0);
		TestEqual(TEXT("짧은 연결은 되돌리지 않는다"),
			S.CooldownRemaining(200.0, Initial, Max), 5.f);
	}

	// 정지 상태가 아니면 재개할 것이 없다.
	{
		FHermesSuspendState S;
		S.NoteConnectionOpened();
		TestFalse(TEXT("정지가 아니면 false"), S.TryResume(100.0, Initial, Max));
		TestEqual(TEXT("정지가 아니면 대기 0"),
			S.CooldownRemaining(100.0, Initial, Max), 0.f);
	}

	// 되받아치기를 반복하면 사다리가 계속 오른다 — 이것이 싸움을 수렴시킨다.
	{
		FHermesSuspendState S;
		double Now = 0.0;
		for (int32 i = 0; i < 4; ++i)
		{
			S.NoteConnectionOpened();
			S.NoteSuspended(Now);
			Now += 1000.0;                     // 충분히 기다렸다고 본다
			S.TryResume(Now, Initial, Max);
			S.NoteConnectionClosed(0.2, Healthy);
		}
		S.NoteConnectionOpened();
		S.NoteSuspended(Now);
		TestEqual(TEXT("5회째는 40초"),
			S.CooldownRemaining(Now, Initial, Max), 40.f);
	}

	return true;
}
```

- [ ] **Step 3: 컴파일만 되는 구현을 넣는다**

`HermesSuspendState.cpp`:

```cpp
#include "Connection/HermesSuspendState.h"
#include "Transport/HermesResumePolicy.h"

void FHermesSuspendState::NoteConnectionOpened()
{
}

void FHermesSuspendState::NoteSuspended(double NowSeconds)
{
}

void FHermesSuspendState::NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds)
{
}

float FHermesSuspendState::CooldownRemaining(double NowSeconds, float Initial, float Max) const
{
	return 0.f;
}

bool FHermesSuspendState::TryResume(double NowSeconds, float Initial, float Max)
{
	return false;
}
```

- [ ] **Step 4: 빌드하고 스펙이 실패하는지 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Reconnect.SuspendState; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\suspend-red.log"
```

기대: `Result={Fail}`. "정지됨", "즉시 재개된다", "2회째는 5초 대기" 등이 실패한다.

- [ ] **Step 5: 구현한다**

`HermesSuspendState.cpp`:

```cpp
#include "Connection/HermesSuspendState.h"
#include "Transport/HermesResumePolicy.h"
#include "Math/UnrealMathUtility.h"

void FHermesSuspendState::NoteConnectionOpened()
{
	bSuspendedDuringThisConnection = false;
}

void FHermesSuspendState::NoteSuspended(double NowSeconds)
{
	++ConsecutiveSuspends;
	SuspendedAt = NowSeconds;
	bSuspended = true;
	bSuspendedDuringThisConnection = true;
}

void FHermesSuspendState::NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds)
{
	// 탈취로 끝난 연결은 되돌리지 않는다. 걷어차인 것은 재시도 권한을
	// 벌어주지 않는다 — 이 한 줄이 eviction war 를 수렴시킨다.
	if (bSuspendedDuringThisConnection)
	{
		return;
	}
	if (LifetimeSeconds >= static_cast<double>(HealthySeconds))
	{
		ConsecutiveSuspends = 0;
	}
}

float FHermesSuspendState::CooldownRemaining(double NowSeconds, float Initial, float Max) const
{
	if (!bSuspended)
	{
		return 0.f;
	}
	const float Required = HermesResumePolicy::RequiredCooldown(
		ConsecutiveSuspends, Initial, Max);
	const double Elapsed = NowSeconds - SuspendedAt;
	return FMath::Max(0.f, Required - static_cast<float>(Elapsed));
}

bool FHermesSuspendState::TryResume(double NowSeconds, float Initial, float Max)
{
	if (!bSuspended)
	{
		return false;
	}
	if (CooldownRemaining(NowSeconds, Initial, Max) > 0.f)
	{
		return false;
	}
	bSuspended = false;
	return true;
}
```

- [ ] **Step 6: 스펙이 통과하는지 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Reconnect; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\suspend-green.log"
```

기대: `Hermes.Reconnect.Cooldown` 과 `Hermes.Reconnect.SuspendState` 둘 다
`Result={Success}`, `EXIT CODE: 0`.

- [ ] **Step 7: 커밋한다**

```bash
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.h Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.cpp Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesSuspendState.spec.cpp
git commit -m "feat: 재연결 정지 상태 전이 분리"
```

---

### Task 3: 설정 항목 추가

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.h`

**Interfaces:**
- Consumes: 없음
- Produces: `UHermesSettings::ReconnectCooldownSeconds`, `UHermesSettings::MaxReconnectCooldownSeconds`

- [ ] **Step 1: 설정을 추가한다**

`HermesSettings.h` 의 `HealthyConnectionSeconds` 선언 **바로 뒤**에 넣는다.

```cpp
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
```

- [ ] **Step 2: 빌드가 통과하는지 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

기대: `Result: Succeeded`.

- [ ] **Step 3: 커밋한다**

```bash
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Settings/HermesSettings.h
git commit -m "feat: 재개 쿨다운 설정 추가"
```

---

### Task 4: 블루프린트 표면과 배선

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp`

**Interfaces:**
- Consumes: `FHermesSuspendState` (Task 2), `UHermesSettings::ReconnectCooldownSeconds` / `MaxReconnectCooldownSeconds` (Task 3)
- Produces: `UHermesConnectionSubsystem::Reconnect()` (반환형이 `void` → `bool` 로 바뀐다), `IsReconnectSuspended()`, `GetReconnectCooldownRemaining()`, `OnReconnectSuspended`

- [ ] **Step 1: 델리게이트를 선언한다**

`HermesConnectionSubsystem.h` 의 기존 `DECLARE_MULTICAST_DELEGATE_*` 묶음 **바로 뒤**에
추가한다. 기존 것들은 건드리지 않는다.

```cpp
/**
 * 재연결 루프가 정지되었다. 게임이 재접속 UI 를 띄울 시점이다.
 * 기존 델리게이트들과 달리 dynamic 이다 — 블루프린트가 구독해야 하기 때문이다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReconnectSuspended, const FString&, Reason);
```

- [ ] **Step 2: 헤더에 표면을 추가한다**

`HermesConnectionSubsystem.h` 의 기존 `void Reconnect();` 선언과 그 위 주석을 아래로
**교체**한다.

```cpp
	/**
	 * 종료성 에러로 멈춘 재연결 루프를 다시 돌린다.
	 *
	 * 재개했으면 true. 정지 상태가 아니거나 쿨다운 중이면 false 다.
	 * 쿨다운은 플러그인이 강제한다 — 게임이 이 함수를 Tick 에 걸어도
	 * eviction war 로 번지지 않는다. 남은 대기는
	 * GetReconnectCooldownRemaining() 으로 알 수 있다.
	 */
	UFUNCTION(BlueprintCallable, Category="Hermes")
	bool Reconnect();

	/** 종료성 에러로 정지되어 Reconnect() 를 기다리는 상태인가. */
	UFUNCTION(BlueprintPure, Category="Hermes")
	bool IsReconnectSuspended() const;

	/**
	 * 지금 Reconnect() 가 거부될 때 남은 대기(초). 0 이면 즉시 가능.
	 * 정지 상태가 아니면 0 이다 — 기다릴 것이 없다.
	 */
	UFUNCTION(BlueprintPure, Category="Hermes")
	float GetReconnectCooldownRemaining() const;
```

같은 헤더의 델리게이트 멤버들이 모인 곳(`FOnChatFailed OnChatFailed;` 아래)에 추가한다.

```cpp
	/** 재연결 루프가 정지되는 순간. Reason 은 사용자에게 보여줄 수 있는 사유. */
	UPROPERTY(BlueprintAssignable, Category="Hermes")
	FOnReconnectSuspended OnReconnectSuspended;
```

`private:` 구역의 `uint32 SeenConnectionGeneration = 0;` **바로 뒤**에 상태를 추가한다.

```cpp
	FHermesSuspendState SuspendState;
	/** 현재 연결이 성립한 시각. 끝날 때 얼마나 살았는지 재는 데 쓴다. */
	double ConnectionOpenedAt = 0.0;
```

같은 헤더 상단의 인클루드에 추가한다.

```cpp
#include "Connection/HermesSuspendState.h"
```

- [ ] **Step 3: 연결 전이에 배선한다**

`HermesConnectionSubsystem.cpp` 의 `HandleConnectionLost` 와
`HandleConnectionEstablished` 를 아래로 교체한다.

```cpp
void UHermesConnectionSubsystem::HandleConnectionLost(double NowSeconds)
{
	bIdentified = false;

	// 이 연결이 얼마나 살았는지가 사다리를 되돌릴지 가른다.
	SuspendState.NoteConnectionClosed(
		NowSeconds - ConnectionOpenedAt,
		GetDefault<UHermesSettings>()->HealthyConnectionSeconds);

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

void UHermesConnectionSubsystem::HandleConnectionEstablished(double NowSeconds)
{
	bIdentified  = false;
	LastRecvTime = NowSeconds;   // 초기화하지 않으면 연결 직후 즉시 사망 판정이 난다
	LastSendTime = NowSeconds;
	ConnectionOpenedAt = NowSeconds;
	SuspendState.NoteConnectionOpened();
	SendIdentify();
}
```

`Tick` 의 전이 처리는 이미 종료를 먼저 부르고 성립을 나중에 부른다. 그대로 둔다 —
`Reopened` 에서 순서가 뒤집히면 방금 시작한 연결의 플래그를 지운다.

- [ ] **Step 4: 정지 처리에 배선한다**

`HermesConnectionSubsystem.cpp` 의 `ApplyErrorReaction` 안
`case EHermesErrorReaction::StopReconnect:` 블록을 아래로 교체한다.

```cpp
	case EHermesErrorReaction::StopReconnect:
		// 조용히 멈추면 원인 추적이 불가능하다. 사유와 재개 방법을 같이 남긴다.
		UE_LOG(LogHermes, Error,
			TEXT("fatal server error %s: %s -- stopping the reconnect loop. "
			     "Retrying does not heal this; call Reconnect() to attempt it deliberately."),
			*Code, *Message);
		if (Worker)
		{
			Worker->SuspendReconnect();
		}
		SuspendState.NoteSuspended(FPlatformTime::Seconds());
		// 게임이 재접속 UI 를 띄울 시점이다. 이것이 없으면 게임은 정지 사실을
		// 알 방법이 없어 버튼을 언제 보여야 할지 모른다.
		OnReconnectSuspended.Broadcast(Message);
		break;
```

- [ ] **Step 5: Reconnect 를 구현한다**

`HermesConnectionSubsystem.cpp` 의 기존 `void UHermesConnectionSubsystem::Reconnect()`
정의를 아래로 교체한다.

```cpp
bool UHermesConnectionSubsystem::Reconnect()
{
	if (!Worker)
	{
		return false;
	}

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();
	const double Now = FPlatformTime::Seconds();

	if (!SuspendState.IsSuspended())
	{
		UE_LOG(LogHermes, Verbose,
			TEXT("Reconnect() ignored: the reconnect loop is not suspended"));
		return false;
	}

	if (!SuspendState.TryResume(Now, Settings->ReconnectCooldownSeconds,
	                            Settings->MaxReconnectCooldownSeconds))
	{
		// 거부를 조용히 하면 게임은 버튼이 먹통이 된 것으로 보인다.
		UE_LOG(LogHermes, Warning,
			TEXT("Reconnect() refused: %.1fs of cooldown remaining"),
			GetReconnectCooldownRemaining());
		return false;
	}

	UE_LOG(LogHermes, Log, TEXT("deliberate reconnect requested by game code"));
	Worker->ResumeReconnect();
	return true;
}

bool UHermesConnectionSubsystem::IsReconnectSuspended() const
{
	return SuspendState.IsSuspended();
}

float UHermesConnectionSubsystem::GetReconnectCooldownRemaining() const
{
	const UHermesSettings* Settings = GetDefault<UHermesSettings>();
	return SuspendState.CooldownRemaining(
		FPlatformTime::Seconds(),
		Settings->ReconnectCooldownSeconds,
		Settings->MaxReconnectCooldownSeconds);
}
```

- [ ] **Step 6: 빌드하고 전체 테스트를 돌린다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\all.log"
```

기대: 30종 전원 `Result={Success}`, `EXIT CODE: 0`.
(기존 28종 + `Hermes.Reconnect.Cooldown` + `Hermes.Reconnect.SuspendState`)

- [ ] **Step 7: 커밋한다**

```bash
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h Plugins/HermesAgentNPC/Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp
git commit -m "feat: Reconnect 를 블루프린트에 노출하고 쿨다운을 플러그인이 강제"
```

---

### Task 5: 콘솔 명령과 실행 검증

**Files:**
- Modify: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesDebugCommands.cpp`

**Interfaces:**
- Consumes: `UHermesConnectionSubsystem::Reconnect()`, `IsReconnectSuspended()`, `GetReconnectCooldownRemaining()` (Task 4)
- Produces: `Hermes.Reconnect` 콘솔 명령

- [ ] **Step 1: 콘솔 명령을 추가한다**

`HermesDebugCommands.cpp` 의 `GHermesStatusCmd` 정의 **뒤**, `#endif` **앞**에 추가한다.

```cpp
static FAutoConsoleCommandWithWorldAndArgs GHermesReconnectCmd(
	TEXT("Hermes.Reconnect"),
	TEXT("정지된 재연결 루프를 재개한다. 사용: Hermes.Reconnect [@지연초]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			const float Delay = HermesDebugCmd::TakeDelay(Args);
			TWeakObjectPtr<UWorld> WeakWorld(World);

			HermesDebugCmd::RunAfter(Delay, [WeakWorld]()
			{
				UHermesConnectionSubsystem* Conn =
					HermesDebugCmd::GetConnection(WeakWorld.Get());
				if (!Conn)
				{
					UE_LOG(LogHermes, Error, TEXT("Hermes.Reconnect: 연결 서브시스템이 없다"));
					return;
				}
				const bool bResumed = Conn->Reconnect();
				UE_LOG(LogHermes, Display,
					TEXT("Hermes.Reconnect: %s (정지=%s, 남은대기=%.1fs)"),
					bResumed ? TEXT("재개함") : TEXT("거부됨"),
					Conn->IsReconnectSuspended() ? TEXT("예") : TEXT("아니오"),
					Conn->GetReconnectCooldownRemaining());
			});
		}));
```

- [ ] **Step 2: 빌드한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex
```

기대: `Result: Succeeded`.

- [ ] **Step 3: 스텁으로 사다리가 오르는지 확인한다**

세 번 재개를 시도한다. 스텁은 매번 `session_taken_over` 로 답하므로 연결이 즉시
끊기고, 사다리가 올라야 한다.

```
.\docs\testing\run-headless-verification.ps1 -Scenario session_taken_over -Seconds 60 -ResetSave `
    -Exec "Hermes.Reconnect @8, Hermes.Reconnect @14, Hermes.Reconnect @20"
```

기대하는 로그:

- `fatal server error session_taken_over` (최초 정지)
- 첫 `Hermes.Reconnect`: `재개함` — 첫 정지는 대기가 없다
- 재개 직후 다시 정지되고, 두 번째 호출은 `거부됨 (정지=예, 남은대기=…)`
  또는 5초가 지났으면 `재개함`
- 세 번째 정지 뒤의 남은 대기는 첫 번째보다 크다

정확한 승인/거부는 타이밍에 달리므로, **확인할 것은 "거부될 때 남은 대기가 0 이
아니고, 정지가 반복될수록 커진다"** 이다.

- [ ] **Step 4: 정상 경로가 망가지지 않았는지 확인한다**

```
.\docs\testing\run-headless-verification.ps1 -Scenario happy -Seconds 40 -ResetSave `
    -Exec "Hermes.Interact @3, Hermes.Chat @5 안녕"
```

기대: 연결 1 / identify 1 / 에러 0. `Reconnect` 관련 로그가 하나도 나오지 않는다 —
정지될 일이 없기 때문이다.

- [ ] **Step 5: 커밋한다**

```bash
git add Plugins/HermesAgentNPC/Source/HermesAgentNPC/HermesDebugCommands.cpp
git commit -m "feat: Hermes.Reconnect 콘솔 명령"
```

---

### Task 6: 문서 갱신

**Files:**
- Modify: `README.md`
- Modify: `docs/testing/manual-verification-setup.md`
- Modify: `docs/superpowers/HANDOFF.md`
- Modify: `docs/superpowers/plans/PROGRESS.md`

**Interfaces:**
- Consumes: Task 1~5 의 결과
- Produces: 없음

- [ ] **Step 1: README 에 재접속 절을 추가한다**

`README.md` 의 "⚠️ NPC가 말만 하고 움직이지 않는다면" 절 **바로 앞**에 넣는다.

```markdown
## 🔄 재접속 (Reconnect)

다른 기기에서 같은 계정으로 접속하면 서버가 이 연결을 걷어냅니다
(`session_taken_over`). 이때 플러그인은 **재연결을 자동으로 시도하지 않습니다.**
자동으로 되붙으면 두 기기가 서로를 영원히 걷어내기 때문입니다.

게임이 재접속 UI 를 붙이면 됩니다.

| 함수 | 용도 |
| :--- | :--- |
| `On Reconnect Suspended` (이벤트) | 정지되는 순간. 재접속 버튼을 띄울 시점 |
| `Is Reconnect Suspended` | 지금 정지 상태인가 |
| `Get Reconnect Cooldown Remaining` | 남은 대기(초). 0 이면 즉시 가능 |
| `Reconnect` | 재개 시도. 성공하면 true |

**반복 호출은 플러그인이 막습니다.** 첫 정지는 즉시 재개되지만, 되받아치기를
반복하면 대기가 5 → 10 → 20초로 늘어 최대 300초까지 갑니다. `Tick` 에 걸어도
안전하지만, 그러면 버튼이 계속 거부되므로 **플레이어의 조작에 연결하는 것이
맞습니다.**

정지 없이 5초 이상 유지된 연결이 끝나면 대기가 초기화됩니다. 네트워크가 잠깐
끊긴 경우는 벌하지 않기 위함입니다.
```

- [ ] **Step 2: 검증 가이드에 §4.3 을 갱신한다**

`docs/testing/manual-verification-setup.md` 의 `### 4.3 재연결 재개` 절 전체를
아래로 교체한다.

```markdown
### 4.3 재연결 재개

`Reconnect()` 는 블루프린트에서 부를 수 있다(`BlueprintCallable`). 헤드리스에서는
콘솔 명령을 쓴다.

```powershell
.\docs\testing\run-headless-verification.ps1 -Scenario session_taken_over -Seconds 60 -ResetSave `
    -Exec "Hermes.Reconnect @8, Hermes.Reconnect @14"
```

로그에 `Hermes.Reconnect: 재개함` 또는
`Hermes.Reconnect: 거부됨 (정지=예, 남은대기=4.2s)` 이 찍힌다.

**반복 호출은 플러그인이 막는다.** 첫 정지는 즉시 재개되고, 되받아치기를 반복하면
대기가 배로 늘어난다(5 → 10 → 20 … 상한 300초). 정지 없이 5초 이상 유지된 연결이
끝나면 초기화된다. 근거는
`docs/superpowers/specs/2026-07-31-hermes-reconnect-eviction-guard-design.md`.

PIE 를 다시 시작해도 정지 상태는 초기화된다 — 세션마다 새로 시작하기 때문이다.
```

- [ ] **Step 3: 인계 기록을 갱신한다**

`docs/superpowers/HANDOFF.md` 의 `### (2) Reconnect() 블루프린트 노출 — 게임 재접속 UI 설계 시`
절 전체를 아래로 교체한다.

```markdown
### (2) `Reconnect()` 블루프린트 노출 — ✅ 완료 (Task 28)

`BlueprintCallable` 로 노출했고, eviction war 는 **플러그인이 직접 막는다.**
게임이 `Tick` 에 걸어도 무너지지 않는다 — 첫 정지는 즉시 재개되지만 되받아치기를
반복하면 대기가 배로 늘고, 정지 없이 끝난 건강한 연결에서만 초기화된다.

정지 사실은 `OnReconnectSuspended` 로 알린다. 그 전에는 게임이 정지 사실 자체를
알 수 없어 버튼을 언제 띄울지 몰랐다.

설계 근거: `docs/superpowers/specs/2026-07-31-hermes-reconnect-eviction-guard-design.md`
```

같은 파일의 테스트 기준선을 30종으로 고치고, 목록에
`Hermes.Reconnect.Cooldown` 과 `Hermes.Reconnect.SuspendState` 를
알파벳 순서에 맞게 넣는다(`Hermes.RateLimiter.TokenBucket` 뒤).

- [ ] **Step 4: 진행 기록을 갱신한다**

`docs/superpowers/plans/PROGRESS.md` 의 "⬜ 아직 남은 것" 에서
`Reconnect() 블루프린트 노출` 항목을 지우고, 완료 목록 끝에 추가한다.

```markdown
- [x] **Task 28** `Reconnect()` 블루프린트 노출 및 eviction war 방지 — 쿨다운
      사다리를 플러그인이 강제한다 (`Hermes.Reconnect.Cooldown`,
      `Hermes.Reconnect.SuspendState` PASS). 설계는
      `specs/2026-07-31-hermes-reconnect-eviction-guard-design.md`
```

테스트 결과 목록도 30종으로 고치고 두 항목을 추가한다.

- [ ] **Step 5: 전체 테스트로 기준선을 확인한다**

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi -abslog="%TEMP%\final.log"
```

기대: 30종 전원 `Result={Success}`, `EXIT CODE: 0`.

- [ ] **Step 6: 커밋한다**

```bash
git add README.md docs/testing/manual-verification-setup.md docs/superpowers/HANDOFF.md docs/superpowers/plans/PROGRESS.md
git commit -m "docs: 재접속 노출과 eviction war 방지 반영"
```
