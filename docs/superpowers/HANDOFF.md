# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-28
- 브랜치: `feat/settings-globalization-protocol-v2` (master에서 분기)

## 지금 상태

**Phase 1 진행 중 — Task 3 (DNS 해석), 컴파일 에러 수정 중.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 v2 문서 | Task 17 (앞당김) | ✅ 완료 |
| 1 — 설정 전역화 | Task 1~3 | 🔶 Task 1·2 완료, **Task 3 진행 중** |
| 2 — 입력 강건성 | Task 4~8 | ⬜ 대기 |
| 3 — 프로토콜 v2 코드 | Task 9~13b | ⛔ **보류** (서버 v2 준비 후) |
| 4 — TLS | Task 14~16 | ⛔ **보류** (동상) |
| 5 — 문서·검증 | Task 18~19 | ⬜ 대기 |

**이번 세션 목표는 Phase 1~2 (Task 1~8)까지.** Phase 3부터는 클라이언트가 v1 서버와 통신 불가가 되므로 서버 재구축과 일정을 맞춰야 한다.

## 🔴 재개 지점 — 여기부터 읽을 것

**Task 3의 마지막 빌드가 컴파일 에러로 실패했다.** 미커밋 상태다.

```
HermesSocketWorker.cpp(75,4): error C2039:
  'WithProtocol': is not a member of 'FTcpSocketBuilder'
```

계획서 Task 3 Step 1이 `FTcpSocketBuilder(...).WithProtocol(...)` 를 쓰라고 했는데 **UE 5.8의 `FTcpSocketBuilder`에는 그 메서드가 없다.** 계획서가 틀렸다.

`Engine/Source/Runtime/Networking/Public/Common/TcpSocketBuilder.h` 를 열어 실제 API를 확인하고 아래 중 하나로 고친다:

1. `FTcpSocketBuilder` 대신 `SS->CreateSocket(NAME_Stream, TEXT("HermesClient"), Chosen->Address->GetProtocolType())` 를 직접 호출 — 프로토콜 타입을 받는 정식 경로
2. `FTcpSocketBuilder` 에 프로토콜 지정 수단이 있으면 그 이름으로 교체

**IPv4 우선 선택 로직(`Chosen`)과 `GetAddressInfo` 교체는 이미 되어 있다.** 소켓 생성 한 줄만 고치면 된다.

고친 뒤 계획서 Task 3 Step 2(빌드·테스트) → Step 3(수동 확인, 선택) → Step 4(커밋) 순으로 진행한다. 그다음 Task 4.

**계획서도 함께 고칠 것** — 같은 오류가 Task 14(`FHermesPlainTransport::Connect`)에도 복사되어 있다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 설계 스펙 | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |
| 프로토콜 v2 계약 | `ue5-socket-protocol.md` |

**계획서가 정본이다.** Task별 Step이 실행 가능한 단위로 쪼개져 있고 코드 전문이 들어 있다. 이어받을 때는 계획서의 해당 Task부터 읽으면 된다.

## 커밋 히스토리

```
47c404b  refactor: 하드코딩 상수를 UHermesSettings 소비로 전환   (Task 2 ✅)
b55528b  feat: UHermesSettings 설정 클래스 및 커맨드라인 오버라이드 (Task 1 ✅)
f6c1a6e  docs: 검토 결과를 구현 계획서에 반영
10d0279  docs: action_event 프레임 추가 — 15초 딜레마 해소
7e8e53d  docs: 프로토콜 문서를 v2로 개정 (서버 재구축 계약)
7186dd5  docs: 구현 계획서 작성            (master)
```

## 미커밋 작업 (Task 3, 빌드 실패 상태)

| 파일 | 내용 |
|---|---|
| `Plugins/.../Transport/HermesSocketWorker.cpp` | `GetAddressInfo` + IPv4 우선 선택. **`WithProtocol` 한 줄이 컴파일 실패** |

되돌리려면: `git checkout Plugins/HermesAgentNPC/Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp`

## 테스트 기준선

현재 **6종 전부 통과** (Task 2 시점 기준):

```
Hermes.Actions.Dispatcher.Route
Hermes.Inventory.AddRemove
Hermes.Protocol.FrameAccumulator.Parse
Hermes.Protocol.FrameCodec.Encode
Hermes.Protocol.Messages.Build
Hermes.Settings.CommandLineOverride     ← Task 1 신규
```

Phase 2 종료 시 12종이 되어야 한다.

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

**2. `FTcpSocketBuilder::WithProtocol` 부재 (Task 3, 미해결).** 위 재개 지점 참조.

**3. 계획서의 코드는 검증되지 않았다.** 위 둘 다 계획서를 그대로 따랐다가 실패한 경우다. 계획서는 방향과 근거로 쓰고, **API 시그니처는 엔진 소스로 확인**하는 편이 빠르다.

## 결정 기록 (되돌리지 말 것)

- **v1 호환 모드 없음** — 호환 경로는 곧 인증 없이 동작하는 경로다.
- **`identified`는 재접속 시에도 자격 증명을 항상 싣는다** — 그래야 "`session_token` 없음"이 v1 서버 신호로 성립한다.
- **TLS 실패 시 평문 폴백 없음**, Shipping에서 `bUseTLS=False` 무시.
- **`MaxBodySize`(1 MiB)는 설정화하지 않는다** — 프로토콜 불변식.
- **세션 토큰 난독화는 보안이 아니다** — 키가 바이너리에 있다. 기기 소유자로부터 보호하지 않으며 문서에 명시한다.
- **`action_result`는 접수, `action_event`는 완료** — `move_to`가 15초를 넘길 수 있고 서버는 맵 크기를 모른다.
