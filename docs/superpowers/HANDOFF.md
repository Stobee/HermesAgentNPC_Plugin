# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-30
- 브랜치: `master`

## 지금 상태

**Phase 1~3 및 Task 18 완료. 다음은 Phase 4(TLS, Task 14~16).**
Phase 3의 코드는 전부 들어갔고, 남은 것은 Task 13d의 수동 확인 1건이다(아래 참조).

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | ✅ **완료** (Task 13d 수동 확인 1건 미완) |
| 4 — TLS | Task 14~16 | ⛔ **다음 차례** |
| 5 — 문서·검증 | Task 18~19 | 🔶 **Task 18 완료**, Task 19 통합 검증 대기 |

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드(exit 0) 및 테스트 **21종** 전원 PASS를 확인했다.

**다음 할 일:**
- **Task 14 (전송 계층 추상화 — 순수 리팩터링)**. Phase 4 원칙: 프레이밍(`FHermesFrameCodec`, `FFrameAccumulator`)과 큐·백오프·상한 로직은 **한 줄도 바뀌지 않는다.** 바이트를 어디로 읽고 쓰는지만 달라진다. Task 14가 경계를 만들고 Task 16이 그 뒤에 TLS를 끼운다.
- **Task 14 착수 전 확인**: 계획서 Task 14 Step 3의 `FHermesPlainTransport::Connect()` 에서 `SetKeepAlive` 호출은 이미 제거했다(2026-07-30 정정). Task 16에는 raw 디스크립터에 `setsockopt(SO_KEEPALIVE)` 를 넣도록 명시해 두었다.

### ⚠️ Task 13d 미완 항목 — 수동 확인 (계획서 Step 5)

**스텁 서버 수동 확인을 수행하지 못했다.** `session_taken_over` 를 보내고 닫는 스텁 서버에
접속해 (1) 로그에 사유가 남고 재연결 시도가 멈추는지, (2) 두 클라이언트를 동시에 띄워
eviction war 가 없는지 확인하는 항목이다.

**막힌 이유: `Content/` 에 `.gitkeep` 하나뿐이고 맵·에셋이 없어 PIE 를 띄울 수 없다.**
(에디터 기동 로그의 `Can't find file '.../Content/Maps/SampleMap.umap'` 경고가 같은 원인이다.)
**이 제약은 Task 19 통합 검증 전체에도 그대로 걸린다.**

**➡ 구축 가이드: `docs/testing/manual-verification-setup.md`** (2026-07-30 작성)
만들어야 할 에셋 4개(위젯·NPC·플레이어·레벨), 위젯 `BindWidget` 이름 요구사항, 설정 권장값,
검증 절차와 기대 로그 문자열이 들어 있다. **사용자가 에셋을 직접 만들기로 했다.**

스텁 서버는 `docs/testing/hermes_stub_server.py` 로 준비되어 있다(표준 라이브러리만, 시나리오
13종). `--scenario session_taken_over --once` 가 Task 13d Step 5 (a) 항목에 대응한다.

코드 경로(Step 1~4, 6)는 완료·빌드·회귀 테스트를 통과했다.

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
| 수동 검증 환경 구축 가이드 | `docs/testing/manual-verification-setup.md` |
| 수동 검증용 스텁 서버 | `docs/testing/hermes_stub_server.py` |
| 설계 스펙 (내부) | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 (내부) | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |

## 커밋 히스토리

```
6b3cf8c  feat: error.id 기반 진행 중 턴 즉시 실패 (Task 13e ✅) — Phase 3 완료
5f1b518  docs: Task 13d 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
532f6e3  feat: 종료성 에러 시 재연결 루프 정지 (Task 13d — 수동 확인 미완)
e116f20  docs: Task 13c 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
c29d46c  feat: 에러 코드 반응 정책을 순수 함수로 분리 (Task 13c ✅)
ad5fee2  docs: Task 13b 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
1522937  feat: action_event 로 move_to 완료를 비동기 통지 (Task 13b ✅)
83183f1  docs: Task 13 완료에 따른 HANDOFF.md 및 PROGRESS.md 인계 기록 갱신
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

**2026-07-30 확인: 21종 전부 통과 (exit code 0).** Task 11은 자동화 테스트를 추가하지
않는다 — `chat_delta` 처리는 델리게이트 브로드캐스트뿐이고 누적 표시는 위젯 상태라
Task 19의 수동 검증에서 확인한다. Task 12가 `Hermes.Liveness.Evaluate`, Task 13이
`Hermes.PendingChats.Timeout`, Task 13b가 `Hermes.Protocol.Messages.ActionEvent` 를
추가했다. Task 13b의 `MoveToActionHandler` 두 단계 응답은 AI 컨트롤러와 네비게이션이
필요해 자동화 테스트가 없다 — Task 19의 수동 검증 대상이다. Task 13c가
`Hermes.Connection.ErrorPolicy` 를 추가했다. Task 13d는 새 테스트를 추가하지 않는다 —
정지 플래그는 워커 스레드와 실제 소켓이 얽혀 순수 판정으로 떼어낼 부분이 없고, 검증은
계획서가 수동 확인으로 규정했다. Task 13e가 `Hermes.PendingChats.FailById` 를 추가했다.

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
Hermes.Connection.ErrorPolicy           ← Task 13c
Hermes.Inventory.AddRemove
Hermes.Inventory.AddSaturates
Hermes.Liveness.Evaluate                ← Task 12
Hermes.PendingChats.FailById            ← Task 13e
Hermes.PendingChats.Timeout             ← Task 13
Hermes.Protocol.FrameAccumulator.Parse
Hermes.Protocol.FrameCodec.Encode
Hermes.Protocol.Messages.ActionEvent    ← Task 13b
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
