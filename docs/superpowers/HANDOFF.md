# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-29
- 브랜치: `master`

## 지금 상태

**Phase 1(Task 1~3), Phase 2(Task 4~8), Task 18 완료. Phase 3 진행 중 (Task 9 완료).**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | 🔶 **Task 9 완료**, Task 10 진행 예정 |
| 4 — TLS | Task 14~16 | ⛔ 대기 |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19 통합 검증 대기 |

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드(exit 0) 및 테스트 **16종** 전원 PASS를 확인했다.

**다음 할 일:**
- **Task 10 (서버 발급 신원 흐름 구현)**: `HermesConnectionSubsystem`에서 `session_token`을 SaveGame에 보관하고, 재접속 시 `player_id` + `session_token`을 동시 전송하도록 구현.

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
008b052  feat: 프로토콜 v2 identify/identified 메시지 계층 (Task 9 ✅)
53b7084  docs: 서버 구현자용 문서에서 내부 설계 문서 참조 제거
9581910  docs: 서버 프롬프트를 스펙과 함께 2종만으로 자족하게 정리
19f7656  docs: 서버 구현 준비물 체크리스트 HTML 추가
428dc76  docs: 서버 프롬프트를 실제 아키텍처에 맞게 수정 (Hermes/llama-server 고정)
f9a70cc  docs: 서버 프롬프트에서 클라이언트 프롬프트 참조 제거
b0d332b  feat: 전용 로그 카테고리 LogHermes 도입
bfd1539  docs: 프로토콜 문서에서 v1 프레이밍 제거 + Windows 서버 지원
478e473  docs: 턴 직렬화 확정 및 프로토콜 공백 3건 해소
6ae4841  docs: 확정된 계약을 Task 13c~13e 로 계획서에 반영
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
