# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-30
- 브랜치: `master`

## 지금 상태

**Phase 1~3 및 Task 18 완료. Phase 4 진행 중 (Task 14, Task 15 완료).**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | ✅ **완료** (Task 13d 수동 확인 1건 미완) |
| 4 — TLS | Task 14~16 | 🔶 **Task 14, Task 15 완료**, Task 16 진행 예정 |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19 통합 검증 대기 |

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드(exit 0) 및 테스트 **24종** 전원 PASS를 확인했다.

**다음 할 일:**
- **Task 16 (OpenSSL 기반 TLS 전송)**: `FHermesTlsTransport : IHermesTransport`. Task 14가 만든 경계에 끼운다. `Build.cs` 에 `SSL` + OpenSSL 의존 추가가 필요하다.
- **Task 16 착수 시 확인**: raw 디스크립터를 직접 소유하므로 `setsockopt(SO_KEEPALIVE)` 를 넣도록 계획서에 명시해 두었다.

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
a425829  feat: TLS 검증 정책 결정 순수 로직 분리 (Task 15 ✅)
50823dc  refactor: 전송 계층을 IHermesTransport 로 추상화 (Task 14 ✅)
0c14a80  docs: 수동 검증 환경 구축 가이드와 스텁 서버 추가
```

## 미커밋 작업

**없다.** 코드 변경은 전부 커밋되었다.

## 테스트 기준선

**2026-07-30 확인: 24종 전부 통과 (exit code 0).**

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
Hermes.Connection.ErrorPolicy
Hermes.Inventory.AddRemove
Hermes.Inventory.AddSaturates
Hermes.Liveness.Evaluate
Hermes.PendingChats.FailById
Hermes.PendingChats.Timeout
Hermes.Protocol.FrameAccumulator.Parse
Hermes.Protocol.FrameCodec.Encode
Hermes.Protocol.Messages.ActionEvent
Hermes.Protocol.Messages.Build
Hermes.Protocol.Messages.IdentifyV2
Hermes.Protocol.Messages.ParseIdentified
Hermes.Protocol.Messages.Ping
Hermes.RateLimiter.TokenBucket
Hermes.Settings.CommandLineOverride
Hermes.TlsPolicy.ServerName             ← Task 15
Hermes.TlsPolicy.UseTls                 ← Task 15
Hermes.TlsPolicy.VerifyMode              ← Task 15
Hermes.Util.PushBounded
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```
