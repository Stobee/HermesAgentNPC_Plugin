# 재접속 노출과 eviction war 방지 — 설계

- 작성: 2026-07-31
- 대상: `UHermesConnectionSubsystem`, 신규 `HermesResumePolicy`
- 관련: 프로토콜 §3.4, `manual-verification-setup.md` §4.1

## 문제

`Reconnect()` 는 C++ `public` 이지만 `UFUNCTION` 이 아니라 **블루프린트에서 부를 수
없다.** 종료성 에러(`session_taken_over`, `unsupported_version`)로 재연결 루프가
정지되면 다시 붙는 방법이 이것뿐인데, 게임이 그 방법을 쓸 수 없다.

노출하지 않은 이유는 eviction war 였다. 같은 신원으로 두 인스턴스가 뜨면 서로를
영원히 걷어낸다.

```
A 접속 → B 접속 → 서버가 A 를 걷어냄 (A: session_taken_over)
A 재접속 → 서버가 B 를 걷어냄
B 재접속 → 서버가 A 를 걷어냄
...
```

그래서 프로토콜 §3.4 는 재접속을 *의도적 행위*로 규정했고, 플러그인은 "게임이
알아서 조심하라"는 계약으로 남겨 두었다.

**이것이 잘못이다.** 문제를 만드는 것은 플러그인이므로 막는 것도 플러그인이어야
한다. 게임이 `Tick` 에 걸어도 무너지지 않아야 한다.

두 번째 결함도 같은 뿌리에서 나온다. **정지되었다는 사실이 게임에 전달되지 않는다.**
`OnConnectionStateChanged(bool)` 은 "끊김"만 알리고 "정지되어 사람이 개입해야 함"을
구분하지 못한다. 게다가 이 델리게이트는 `AddUObject` 로 붙이는 C++ 전용이라
블루프린트에서 구독할 수도 없다. 즉 게임은 **언제 재접속 버튼을 보여야 하는지 모른다.**

## 설계

### 블루프린트 표면

```cpp
/**
 * 정지된 재연결 루프를 다시 돌린다.
 * 재개했으면 true. 정지 상태가 아니거나 쿨다운 중이면 false.
 */
UFUNCTION(BlueprintCallable, Category="Hermes")
bool Reconnect();

/** 종료성 에러로 정지되어 Reconnect() 를 기다리는 상태인가. */
UFUNCTION(BlueprintPure, Category="Hermes")
bool IsReconnectSuspended() const;

/**
 * 지금 Reconnect() 가 거부될 때 남은 대기(초). 0 이면 즉시 가능.
 * 정지 상태가 아니면 0 을 돌려준다 — 기다릴 것이 없다.
 */
UFUNCTION(BlueprintPure, Category="Hermes")
float GetReconnectCooldownRemaining() const;

/** 재연결 루프가 정지되는 순간. Reason 은 사용자에게 보여줄 수 있는 사유. */
UPROPERTY(BlueprintAssignable, Category="Hermes")
FOnReconnectSuspended OnReconnectSuspended;   // (const FString& Reason)
```

`bool` 반환과 남은 시간 질의가 함께 있어야 UI 가 **버튼을 비활성화하고 "12초 후 다시
시도"를 표시**할 수 있다. 거부만 하고 사유를 못 알려주면 게임은 버튼이 먹통이 된
것처럼 보이게 만들 수밖에 없다.

`OnReconnectSuspended` 는 신규 dynamic multicast 다. 기존 C++ 델리게이트 배선
(`OnChatDelta` 등)은 건드리지 않는다 — 이번 문제와 무관한 범위다.

### 쿨다운 사다리

정지될 때마다 다음 재개에 필요한 대기가 커진다.

| 연속 정지 | 재개 전 필요 대기 |
|---|---|
| 1회 | **0초** |
| 2회 | 5초 |
| 3회 | 10초 |
| 4회 | 20초 |
| … | 상한 300초 |

**첫 정지에 대기를 두지 않는 것이 핵심이다.** 정당하게 한 번 밀려난 사람을 벌하면
안 된다. 대가는 되받아치기를 반복할 때만 발생한다.

대기는 **정지된 시점**부터 잰다. 걷어차인 순간부터 기다림이 시작된다.

```
CooldownUntil = SuspendedAt + RequiredCooldown(ConsecutiveSuspends)
```

### 사다리가 내려가는 조건

**탈취로 끝난 연결은 사다리를 되돌리지 않는다.** 되돌리는 것은 *정지 없이* 끝난
건강한 연결뿐이다.

연결마다 플래그를 하나 둔다.

- 연결 성립 시 : `bSuspendedDuringThisConnection = false`
- 정지 발생 시 : `++ConsecutiveSuspends`, `bSuspendedDuringThisConnection = true`
- 연결 종료 시 : `!bSuspendedDuringThisConnection && Lifetime >= HealthyConnectionSeconds`
  이면 `ConsecutiveSuspends = 0`

"연결 성립/종료"는 `HermesConnectionEdge` 의 판정을 그대로 쓴다. `Reopened` 는 종료와
성립이 한 틱에 함께 일어난 것이므로 **종료 처리를 먼저 하고 성립 처리를 한다** —
순서가 뒤집히면 방금 시작한 연결의 플래그를 지워 버린다.

이것이 eviction war 를 **수렴시킨다.** 싸우는 동안 A 의 연결은 항상 탈취로 끝나므로
사다리가 계속 오르고, 한쪽이 5분을 기다리게 되면 사실상 다른 쪽이 이긴다.

의미도 분명하다 — **"걷어차인 것은 재시도 권한을 벌어주지 않는다."** 정당한 경우도
맞게 동작한다. 플레이어가 실제로 다른 기기로 옮겼다면 옛 기기는 사다리를 올라가는
것이 옳다. 계속 훔쳐오면 안 된다.

네트워크 단절이나 서버 재시작으로 끝난 연결은 정지를 동반하지 않으므로 정상적으로
사다리를 되돌린다.

`unsupported_version` 으로 인한 정지도 같은 사다리를 쓴다. 재시도로 절대 낫지 않으니
오르기만 하는 것이 맞다.

임계값은 백오프가 쓰는 `HealthyConnectionSeconds`(기본 5초)를 재사용한다.
"쓸 만큼 살아 있었는가"라는 같은 질문이므로 숫자를 새로 만들 이유가 없다.

### 판정 분리

```cpp
namespace HermesResumePolicy
{
    /** 연속 정지 횟수에 대한 재개 대기(초). 1회째는 0. */
    float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max);
}
```

`HermesLiveness`, `HermesErrorPolicy`, `HermesConnectionEdge`, `HermesBackoff` 와 같은
방식이다. 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.

`HermesBackoff` 와 합치지 않는다. 사다리 모양은 닮았지만 다루는 대상이 다르다 —
백오프는 워커의 자동 재연결, 이쪽은 게임이 의도적으로 부르는 재개다. 초기값·상한·
증가 시점이 모두 다르므로 한 함수에 밀어 넣으면 인자만 늘고 뜻이 흐려진다.

### 설정

| 항목 | 기본값 | 설명 |
|---|---|---|
| `ReconnectCooldownSeconds` | 5 | 두 번째 정지부터의 기준 대기 |
| `MaxReconnectCooldownSeconds` | 300 | 상한 |
| `HealthyConnectionSeconds` | 5 | 기존 항목 재사용 |

## 테스트

**순수 로직** — `Hermes.Reconnect.Cooldown`

- 1회째는 0
- 2회째부터 Initial, 이후 2배씩
- 상한을 넘지 않는다
- 음수·0 입력에서도 무너지지 않는다

**상태 전이** — 서브시스템 레벨. 기존 `Hermes.Connection.*` 과 같은 결로 둔다.

- 정지 없이 끝난 건강한 연결은 사다리를 되돌린다
- 탈취로 끝난 연결은 되돌리지 않는다
- 짧게 끝난 연결은 정지가 없었더라도 되돌리지 않는다
- 쿨다운 중 `Reconnect()` 는 false 를 돌려주고 재개하지 않는다
- 정지 상태가 아닐 때 `Reconnect()` 는 false 를 돌려준다

**실행 검증** — 스텁 `session_taken_over` 시나리오에 `Hermes.Reconnect` 콘솔 명령을
더해, 반복 호출이 사다리를 타는지 로그로 확인한다.

## 범위 밖

- 기존 C++ 델리게이트의 dynamic 전환 (`OnConnectionStateChanged` 등) — 별건
- 서버 측 세션 탈취 정책 — 서버의 몫이다
- 샘플 프로젝트의 재접속 UI 위젯 — 플러그인은 표면만 제공하고, 실제 버튼은
  게임이 만든다
