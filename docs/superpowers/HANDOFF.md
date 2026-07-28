# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-28
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1 진행 중 — Task 1 Step 4.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | 🔶 Task 1 진행 중 |
| 2 — 입력 강건성 | Task 4~8 | ⬜ 대기 |
| 3 — 프로토콜 v2 코드 | Task 9~13b | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | ⬜ 대기 |

**이번 세션 목표는 Phase 1~2 (Task 1~8)까지.** Phase 3부터는 클라이언트가 v1 서버와 통신 불가가 되므로 서버 재구축과 일정을 맞춰야 한다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 설계 스펙 | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |
| 프로토콜 v2 계약 | `ue5-socket-protocol.md` |

**계획서가 정본이다.** Task별 Step이 실행 가능한 단위로 쪼개져 있고 코드 전문이 들어 있다. 이어받을 때는 계획서의 해당 Task부터 읽으면 된다.

## 커밋 히스토리

```
f6c1a6e  docs: 검토 결과를 구현 계획서에 반영
10d0279  docs: action_event 프레임 추가 — 15초 딜레마 해소
7e8e53d  docs: 프로토콜 문서를 v2로 개정 (서버 재구축 계약)
7186dd5  docs: 구현 계획서 작성            (master)
239c70f  docs: 프로토콜 v2에 TLS 포함      (master)
```

## 미커밋 작업 (Task 1, Step 1~3까지)

빌드 미검증 상태의 파일들:

| 파일 | 내용 |
|---|---|
| `Plugins/.../HermesAgentNPC.Build.cs` | `DeveloperSettings` 의존 추가 |
| `Plugins/.../Settings/HermesSettings.h` | 신규 — 설정 프로퍼티 전체 |
| `Plugins/.../Settings/HermesSettings.spec.cpp` | 신규 — 오버라이드 7케이스 |

**다음 할 일: 계획서 Task 1 Step 4** (빌드해서 링크 실패 확인 → Step 5에서 `.cpp` 작성).

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

## 결정 기록 (되돌리지 말 것)

- **v1 호환 모드 없음** — 호환 경로는 곧 인증 없이 동작하는 경로다.
- **`identified`는 재접속 시에도 자격 증명을 항상 싣는다** — 그래야 "`session_token` 없음"이 v1 서버 신호로 성립한다.
- **TLS 실패 시 평문 폴백 없음**, Shipping에서 `bUseTLS=False` 무시.
- **`MaxBodySize`(1 MiB)는 설정화하지 않는다** — 프로토콜 불변식.
- **세션 토큰 난독화는 보안이 아니다** — 키가 바이너리에 있다. 기기 소유자로부터 보호하지 않으며 문서에 명시한다.
- **`action_result`는 접수, `action_event`는 완료** — `move_to`가 15초를 넘길 수 있고 서버는 맵 크기를 모른다.
