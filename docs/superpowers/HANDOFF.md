# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-28
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1 완료. Phase 2도 Task 7까지 완료. 남은 것은 Task 8 하나.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | 🔶 Task 4~7 완료, **Task 8만 남음** |
| 3 — 프로토콜 v2 코드 | Task 9~13b | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | ⬜ 대기 |

**이번 세션 목표는 Phase 1~2 (Task 1~8)까지.** Phase 3부터는 클라이언트가 v1 서버와 통신 불가가 되므로 서버 재구축과 일정을 맞춰야 한다.

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드·테스트 모두 통과 상태에서 중단했다.

**다음 할 일: 계획서 `## Task 8: 보류 발화 상한`** (계획서 1719행 부근).

요약하면:
1. `Connection/HermesUtil.h` / `.cpp` 신규 — `HermesUtil::PushBounded(TArray<FString>&, const FString&, int32)`
2. `Connection/HermesUtil.spec.cpp` 신규 — 4케이스 (상한 미만/도달/여러 개 폐기/MaxNum 0 클램프)
3. `HermesConnectionSubsystem::SendChat()` 에 `MaxPendingChats` 적용

계획서에 코드 전문이 있다. 완료하면 **테스트 12종**이 되고 Phase 2가 끝난다.

그 뒤로는 Phase 3~4가 서버 대기 상태이므로, **Task 18(README·HTML 문서 갱신)을 먼저 하는 것도 가능**하다. 프로토콜과 무관하고 빌드도 필요 없다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 설계 스펙 | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |
| 프로토콜 v2 계약 | `ue5-socket-protocol.md` |

**계획서가 정본이다.** Task별 Step이 실행 가능한 단위로 쪼개져 있고 코드 전문이 들어 있다. 이어받을 때는 계획서의 해당 Task부터 읽으면 된다.

## 커밋 히스토리

```
30087c1  feat: 인바운드·아웃바운드 큐 상한 및 틱 처리 예산        (Task 7 ✅)
da0cd40  feat: 액션 레이트 리밋 도입 및 타이머 핸들 회수          (Task 6 ✅)
72b0b32  feat: 액션 파라미터 하드 바운드 및 인벤토리 오버플로 포화 (Task 5 ✅)
f4e6d8f  fix: 백오프 대기를 조각 sleep 으로 바꿔 종료 응답성 확보  (Task 4 ✅)
eee6622  feat: GetAddressInfo 기반 DNS 해석으로 호스트명 지원      (Task 3 ✅)
d59587f  docs: 인계 문서
47c404b  refactor: 하드코딩 상수를 UHermesSettings 소비로 전환     (Task 2 ✅)
b55528b  feat: UHermesSettings 설정 클래스 및 커맨드라인 오버라이드 (Task 1 ✅)
f6c1a6e  docs: 검토 결과를 구현 계획서에 반영
10d0279  docs: action_event 프레임 추가 — 15초 딜레마 해소
7e8e53d  docs: 프로토콜 문서를 v2로 개정 (서버 재구축 계약)
7186dd5  docs: 구현 계획서 작성            (master)
```

## 미커밋 작업

**없다.** 코드 변경은 전부 커밋되었다.

`claude_code_prompt_ue5_client.md` 하나가 untracked 로 남아 있는데 원래부터 그랬던 파일이다(이번 작업과 무관).

## 테스트 기준선

현재 **11종 전부 통과**:

```
Hermes.ActionParams.Coordinate          ← Task 5
Hermes.ActionParams.ItemId              ← Task 5
Hermes.ActionParams.Quantity            ← Task 5
Hermes.Actions.Dispatcher.Route
Hermes.Inventory.AddRemove
Hermes.Inventory.AddSaturates           ← Task 5
Hermes.Protocol.FrameAccumulator.Parse
Hermes.Protocol.FrameCodec.Encode
Hermes.Protocol.Messages.Build
Hermes.RateLimiter.TokenBucket          ← Task 6
Hermes.Settings.CommandLineOverride     ← Task 1
```

Task 8 완료 시 `Hermes.Util.PushBounded` 가 더해져 **12종**이 되고 Phase 2가 끝난다.

### 테스트 결과 확인 방법

`-log=` 옵션은 동작하지 않았다. 기본 로그를 읽는다:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi | Out-Null
$r = Get-Content "C:\Work\HermesAgentNPC\Saved\Logs\HermesAgentNPC.log" -Encoding utf8 | Select-String "Test Completed. Result="
Write-Output "총 $($r.Count)건"
$r | ForEach-Object { ($_ -split "Result=|Path=")[1,2] -join " " }
```

## 알려진 블로커와 해법

**언리얼 에디터가 열려 있으면 빌드가 실패한다.**
```
Unable to build while Live Coding is active.
```
→ 에디터를 종료하고 빌드한다. 확인:
```powershell
Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

기존 테스트 5종이 기준선이다. Task마다 늘어나며 Phase 2 종료 시 12종이 되어야 한다.

## 서버 쪽 병행 작업

`ue5-socket-protocol.md`가 v2로 완성되어 **서버 재구축을 지금 시작할 수 있다.**
- §7b 서버 체크리스트 (9단계)
- §8 스텁 서버 목록 (실패 경로 검증용 8종)
- §6 문법 제약 디코딩 권고 (프롬프트 인젝션 완화의 핵심)

## 실행 중 발견한 것 (계획서에 없던 사항)

**1. `TUniquePtr` 전방 선언 문제 (Task 2에서 발견·수정 완료).**
`HermesConnectionSubsystem.h` 가 `FHermesSocketWorker` 를 전방 선언만 하고 `TUniquePtr` 로 들고 있었다. UHT가 생성하는 생성자가 멤버 소멸자를 인스턴스화하므로 완전한 타입이 필요하다. 지금까지는 unity 빌드에 묻혀 있다가, 파일이 unity에서 제외되면서 드러났다. 전체 헤더 include로 해결했다.

> **교훈:** adaptive unity 빌드는 `git status` 로 변경 파일을 판단해 unity에서 뺀다. 즉 **파일을 건드리는 순간 숨어 있던 include 누락이 드러난다.** 앞으로도 비슷한 에러가 나올 수 있다.

**2. `FTcpSocketBuilder::WithProtocol` 부재 (Task 3에서 발견·수정 완료).**
계획서가 `FTcpSocketBuilder(...).WithProtocol(...)` 를 쓰라고 했으나 그런 메서드는 없다. 더 근본적으로 **이 빌더는 `FIPv4Endpoint` 에서 프로토콜을 유도하므로 구조적으로 IPv4 전용**이라 IPv6 폴백을 지원할 수 없다. `ISocketSubsystem::CreateSocket(NAME_Stream, Desc, ProtocolType)` 직접 호출로 교체하고 빌더의 `AsBlocking()` 에 해당하는 `SetNonBlocking(false)` 를 명시했다. 계획서의 같은 오류(Task 14)도 함께 정정했다.

**3. `TimeoutSeconds` 지역 변수 소실 (Task 6).** 편집 중 선언이 사라져 컴파일 실패. `Settings->ActionTimeoutSeconds` 직접 참조로 해결.

**4. 계획서의 코드는 검증되지 않았다.** 위 셋 다 계획서를 그대로 따랐다가 실패한 경우다. 계획서는 방향과 근거로 쓰고, **API 시그니처는 엔진 소스로 확인**하는 편이 빠르다.

## 자주 쓴 명령

```powershell
# 에디터 실행 확인 (실행 중이면 빌드 불가)
Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue

# 빌드 (에러만 보기)
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex 2>&1 | Select-String -Pattern "error|Result:" | Select-Object -First 6
```

## 결정 기록 (되돌리지 말 것)

- **v1 호환 모드 없음** — 호환 경로는 곧 인증 없이 동작하는 경로다.
- **`identified`는 재접속 시에도 자격 증명을 항상 싣는다** — 그래야 "`session_token` 없음"이 v1 서버 신호로 성립한다.
- **TLS 실패 시 평문 폴백 없음**, Shipping에서 `bUseTLS=False` 무시.
- **`MaxBodySize`(1 MiB)는 설정화하지 않는다** — 프로토콜 불변식.
- **세션 토큰 난독화는 보안이 아니다** — 키가 바이너리에 있다. 기기 소유자로부터 보호하지 않으며 문서에 명시한다.
- **`action_result`는 접수, `action_event`는 완료** — `move_to`가 15초를 넘길 수 있고 서버는 맵 크기를 모른다.
