# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-29
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1(Task 1~3), Phase 2(Task 4~8), Task 18 완료. 프로토콜 계약 확정 및 단일 NPC 소유권 구현 완료.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 (2026-07-29 계약 3건 추가 확정) |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** (Task 4~8 전원 완료) |
| 3 — 프로토콜 v2 코드 | Task 9~13b **+ 13c~13e** | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19는 v2 서버 후 통합 검증 |

**현재 비-v2 서버 상태에서 진행 가능한 모든 클라이언트 작업이 완성되었다.**

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 2026-07-29 빌드(exit 0) 및 테스트 **13종** 전원 PASS를 확인했다.

**다음 할 일:**
- 백엔드 v2 서버 재구축이 완료되면 Phase 3(Task 9~13b, **13c~13e**) 및 Phase 4(Task 14~16) 진행.
- 서버 준비 전까지 현 브랜치 `feat/settings-globalization-protocol-v2`의 준비 완료 상태 유지.

## 📌 2026-07-29에 확정된 프로토콜 계약

사용자 결정으로 미정 사항이 확정되었다. **이미 문서에는 반영됐고 클라이언트 구현은 남아 있다.**

| 확정 사항 | 프로토콜 | 클라이언트 구현 |
|---|---|---|
| 동시 접속 = 세션 탈취, 기존 접속자에 통지 후 종료 | §3.4 | **Task 13d** (미구현) |
| 끊긴 연결의 발화·응답은 자동 재전송 없음 | §3.5 | 클라 측 추가 작업 없음 |
| 에러 코드 10종 + `error.id` 선택 필드 | §5 | **Task 13c·13e** (미구현) |
| 하나의 연결 = 하나의 NPC | "Scope" 절 | ✅ 완료 (`ff7394f`) |
| 턴 직렬화 — 서버는 한 번에 한 턴, 교차 전송 금지 | §3.6 | **Task 11** (델타 `id` 변경 시 버퍼 리셋) |

> **Task 13d를 빠뜨리면 실제로 깨진다.** 현 클라이언트는 어떤 이유로 끊기든 무한
> 재연결하므로, `session_taken_over`를 받고도 재접속해 **두 게임 인스턴스가 서로를
> 영원히 걷어차는 상태**가 된다. 서버 v2가 세션 탈취를 구현하는 시점과 맞물려야 한다.

## ⚠️ v2 코드 작업 시 함께 고칠 문서 지점

- `README.md:113` — `move_to` 결과가 `{ "arrived": true }`로 적혀 있다. 프로토콜 v2(§6)는
  이를 무효 shape으로 규정하지만 **2단계 응답이 Task 13b라 현재 클라이언트 실제 동작은
  README 쪽이 맞다.** 의도적으로 남겨둔 것이므로 Task 13b 완료 시 반드시 함께 고칠 것.
- `ue5-socket-protocol.md` §9의 **구현 상태 주석 2개**(TLS 키 / keepalive·chat 타임아웃 키)는
  해당 Task(12·13·14~16) 완료 시점에 제거해야 한다. 방치하면 문서가 거짓말을 한다.
- §9 "Reconnect backoff"의 **"두 코드가 루프를 정지시킨다"는 서술은 현재 구현이 아니다.**
  Task 13d가 이를 실제로 만든다. 그 전까지 문서가 앞서 있는 상태다.

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
ff7394f  feat: 활성 NPC 소유권 명시화 + 프로토콜 범위를 단일 NPC로 확정
3a4673c  docs: 프로토콜 문서 동기화에 따른 인계 기록 갱신
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

`claude_code_prompt_ue5_client.md`(v1 시절 작업 브리프)는 2026-07-29에 삭제되었다. 애초에
untracked 였으므로 git 이력에는 남지 않는다. 이 파일을 근거로 참조하던
`docs/superpowers/specs/2026-07-24-hermes-ue5-client-design.md`는 최초 설계 시점의
기록이므로 그대로 둔다 — 지금의 근거 문서는 `ue5-socket-protocol.md` 하나다.

## 테스트 기준선

**2026-07-29 확인: 13종 전부 통과 (exit code 0).**

```
Hermes.ActionParams.Coordinate          ← Task 5
Hermes.ActionParams.ItemId              ← Task 5
Hermes.ActionParams.Quantity            ← Task 5
Hermes.Actions.Dispatcher.Rebind        ← 단일 NPC 확정 (2026-07-29)
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
