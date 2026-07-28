# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-28
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1 완료. Phase 2(Task 4~8) 전체 완료.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** (Task 4~8 전원 완료) |
| 3 — 프로토콜 v2 코드 | Task 9~13b | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | ⬜ 대기 |

**이번 세션 목표(Phase 1~2, Task 1~8)를 모두 달성했습니다.** Phase 3부터는 클라이언트가 v1 서버와 통신 불가가 되므로 서버 재구축과 일정을 맞춰야 합니다.

## ✅ 재개 지점 — 깨끗한 상태

**작업 트리에 미커밋 코드 변경이 없다.** 빌드·테스트 12종 모두 통과 상태이다.

**다음 할 일:**
- 서버 v2 준비 완료 전까지 Phase 3~4는 보류.
- **Task 18(README·HTML 문서 갱신)**을 먼저 진행하는 것을 추천 (프로토콜 호환성과 무관하며, Phase 1~2에서 반영된 설정화 및 안전 장치를 문서화).

## 문서 위치

| 문서 | 경로 |
|---|---|
| 설계 스펙 | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |
| 프로토콜 v2 계약 | `ue5-socket-protocol.md` |

## 커밋 히스토리

```
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

현재 **12종 전부 통과**:

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

### 테스트 결과 확인 방법

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi | Out-Null
$r = Get-Content "C:\Work\HermesAgentNPC\Saved\Logs\HermesAgentNPC.log" -Encoding utf8 | Select-String "Test Completed. Result="
Write-Output "총 $($r.Count)건"
$r | ForEach-Object { ($_ -split "Result=|Path=")[1,2] -join " " }
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

## 결정 기록 (되돌리지 말 것)

- **v1 호환 모드 없음** — 호환 경로는 곧 인증 없이 동작하는 경로다.
- **`identified`는 재접속 시에도 자격 증명을 항상 싣는다** — 그래야 "`session_token` 없음"이 v1 서버 신호로 성립한다.
- **TLS 실패 시 평문 폴백 없음**, Shipping에서 `bUseTLS=False` 무시.
- **`MaxBodySize`(1 MiB)는 설정화하지 않는다** — 프로토콜 불변식.
- **세션 토큰 난독화는 보안이 아니다** — 키가 바이너리에 있다. 기기 소유자로부터 보호하지 않으며 문서에 명시한다.
- **`action_result`는 접수, `action_event`는 완료** — `move_to`가 15초를 넘길 수 있고 서버는 맵 크기를 모른다.
