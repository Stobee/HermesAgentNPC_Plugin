# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-29
- 브랜치: `master`

## 지금 상태

**Phase 1(Task 1~3), Phase 2(Task 4~8), Task 18 완료. Phase 3 진행 중 (Task 9, Task 10 완료).**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | 🔶 **Task 9, Task 10 완료**, Task 11 진행 예정 |
| 4 — TLS | Task 14~16 | ⛔ 대기 |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19 통합 검증 대기 |

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드(exit 0) 및 테스트 **16종** 전원 PASS를 확인했다.

**다음 할 일:**
- **Task 11 (스트리밍 응답 `chat_delta` 처리 및 UI 누적)**: `chat_delta` 수신 시 `id`별 텍스트 누적 렌더링 및 `chat_response` 시 최종 전문 동기화/턴 직렬화 구현.

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
1ea234f  feat: 신원을 서버 발급으로 전환하고 클라이언트 UUID 생성 제거 (Task 10 ✅)
889b487  docs: 서버 연동 가이드 문서 plugin-integration-guide.md 추가
6c74cb7  docs: Task 9 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
008b052  feat: 프로토콜 v2 identify/identified 메시지 계층 (Task 9 ✅)
53b7084  docs: 서버 구현자용 문서에서 내부 설계 문서 참조 제거
9581910  docs: 서버 프롬프트를 스펙과 함께 2종만으로 자족하게 정리
```

## 미커밋 작업

**없다.** 코드 변경은 전부 커밋되었다.

## 테스트 기준선

**2026-07-29 확인: 16종 전부 통과 (exit code 0).**

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
Hermes.Inventory.AddRemove
Hermes.Inventory.AddSaturates
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
