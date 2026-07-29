# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-29
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1(Task 1~3), Phase 2(Task 4~8), Task 18 완료. 프로토콜 문서 동기화 완료.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** (Task 4~8 전원 완료) |
| 3 — 프로토콜 v2 코드 | Task 9~13b | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19는 v2 서버 후 통합 검증 |

**현재 비-v2 서버 상태에서 진행 가능한 모든 클라이언트 작업(Phase 1, Phase 2, Task 18)이 완성되었습니다.**

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 2026-07-29 빌드(exit 0) 및 테스트 12종 전원 PASS를 재검증했다.

**다음 할 일:**
- 백엔드 v2 서버 재구축이 완료되면 Phase 3(Task 9~13b) 및 Phase 4(Task 14~16) 진행.
- 서버 준비 전까지 현 브랜치 `feat/settings-globalization-protocol-v2`의 준비 완료 상태 유지.

## ⚠️ v2 코드 작업 시 함께 고칠 문서 지점

- `README.md:113` — `move_to` 결과가 `{ "arrived": true }`로 적혀 있다. 프로토콜 v2(§6)는
  이를 무효 shape으로 규정하지만 **2단계 응답이 Task 13b라 현재 클라이언트 실제 동작은
  README 쪽이 맞다.** 의도적으로 남겨둔 것이므로 Task 13b 완료 시 반드시 함께 고칠 것.
- `ue5-socket-protocol.md` §9의 **구현 상태 주석 2개**(TLS 키 / keepalive·chat 타임아웃 키)는
  해당 Task(12·13·14~16) 완료 시점에 제거해야 한다. 방치하면 문서가 거짓말을 한다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 플러그인 가이드 문서 | `README.md` |
| 기술 사양 HTML 문서 | `HermesAgentNPC_Documentation.html` |
| 설계 스펙 | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |
| 프로토콜 v2 계약 | `ue5-socket-protocol.md` |

## 커밋 히스토리

```
ad8e852  docs: 프로토콜 문서를 구현된 설정 기본값과 동기화
c24c41a  docs: Task 18 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
1643ab9  docs: README·HTML 문서를 설정 기반 및 안전 검증 사양으로 갱신 (Task 18 ✅)
728a185  docs: Task 8 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
b15b8a0  feat: identified 이전 보류 발화에 상한 추가            (Task 8 ✅)
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

**2026-07-29 재실행 확인: 12종 전부 통과 (exit code 0).**

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
Hermes.Util.PushBounded                 ← Task 8
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```
