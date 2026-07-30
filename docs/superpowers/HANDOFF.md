# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-30
- 브랜치: `master`

## 지금 상태

**Phase 1(Task 1~3), Phase 2(Task 4~8), Task 18 완료. Phase 3 진행 중 (Task 9~13 완료).**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | 🔶 **Task 9~13 완료**, Task 13b 진행 예정 |
| 4 — TLS | Task 14~16 | ⛔ 대기 |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19 통합 검증 대기 |

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드(exit 0) 및 테스트 **18종** 전원 PASS를 확인했다.

**다음 할 일:**
- **Task 13b (`action_event` — 장기 실행 액션의 비동기 완료 통지)**, 이어서 13c(에러 코드 반응 정책), 13d(종료성 에러 시 재연결 루프 정지), 13e(`error.id` 기반 진행 중 턴 즉시 실패). 13e는 Task 13이 만든 `FHermesPendingChats`를 소비한다.

### ⚠️ Task 12 에서 생략된 항목 — 다시 시도하지 말 것

**계획서 Task 12 Step 10(소켓 수준 `SO_KEEPALIVE`)은 UE 5.8 에 수단이 없어 생략했다.**
`FSocket::SetKeepAlive` 는 존재하지 않으며 `Runtime/Sockets`·`Runtime/Networking` 어디에도
`SO_KEEPALIVE` 가 노출되지 않는다. `ReleaseNativeSocket()` 은 public 이지만 소유권을 놓아
소켓 정리 경로를 깨뜨리므로 우회로가 못 된다. **기능 공백은 없다** — OS 기본 keepalive 유휴
시간은 7200초인데 수신 침묵 판정은 60초에 발동하므로 플래그가 첫 탐지자가 되는 일은 없었다.
평문 경로 한정이며, Task 16 의 TLS 전송은 디스크립터를 직접 소유하므로 계획서에 `setsockopt`
호출을 명시해 두었다. 관련 문서 6곳(계획서 Step 10·Task 14 두 곳·Task 16, 설계 스펙 §5.7,
프로토콜 §9 Status, 서버 프롬프트 §8)을 이미 정정했다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 서버 연동 가이드 | `plugin-integration-guide.md` |
| 플러그인 가이드 문서 | `README.md` |
| 기술 사양 HTML 문서 | `HermesAgentNPC_Documentation.html` |
| 프로토콜 계약 | `ue5-socket-protocol.md` |
| 설계 스펙 (내부) | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 (내부) | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |

## 커밋 히스토리

```
2ec580e  feat: 대화 응답 타임아웃과 발화 id 상관 (Task 13 ✅)
3fbd55e  docs: SO_KEEPALIVE 미지원 사실 반영 및 Task 12 인계 기록 갱신
d3f8dab  feat: 클라이언트 keepalive ping 과 수신 침묵 기반 사망 판정 (Task 12 ✅)
ea7f21f  docs: Task 11 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
3edc7be  feat: chat_delta 스트리밍 응답 수신 및 누적 표시 (Task 11 ✅)
c3b7039  docs: Task 10 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
1ea234f  feat: 신원을 서버 발급으로 전환하고 클라이언트 UUID 생성 제거 (Task 10 ✅)
889b487  docs: 서버 연동 가이드 문서 plugin-integration-guide.md 추가
6c74cb7  docs: Task 9 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
008b052  feat: 프로토콜 v2 identify/identified 메시지 계층 (Task 9 ✅)
```

## 미커밋 작업

**없다.** 코드 변경은 전부 커밋되었다.

## 테스트 기준선

**2026-07-30 확인: 18종 전부 통과 (exit code 0).** Task 11은 자동화 테스트를 추가하지
않는다 — `chat_delta` 처리는 델리게이트 브로드캐스트뿐이고 누적 표시는 위젯 상태라
Task 19의 수동 검증에서 확인한다. Task 12가 `Hermes.Liveness.Evaluate`, Task 13이
`Hermes.PendingChats.Timeout` 을 추가했다.

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
Hermes.Inventory.AddRemove
Hermes.Inventory.AddSaturates
Hermes.Liveness.Evaluate                ← Task 12
Hermes.PendingChats.Timeout             ← Task 13
Hermes.Protocol.FrameAccumulator.Parse
Hermes.Protocol.FrameCodec.Encode
Hermes.Protocol.Messages.Build
Hermes.Protocol.Messages.IdentifyV2     ← Task 9
Hermes.Protocol.Messages.ParseIdentified← Task 9
Hermes.Protocol.Messages.Ping            ← Task 9
Hermes.RateLimiter.TokenBucket
Hermes.Settings.CommandLineOverride
Hermes.Util.PushBounded
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```
