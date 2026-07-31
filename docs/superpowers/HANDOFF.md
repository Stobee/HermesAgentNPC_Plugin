# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-31
- 브랜치: **`feat/reconnect-eviction-guard` (완료)**

## ✅ 완료된 작업 — Task 28 (재접속 노출과 eviction war 방지)

**`feat/reconnect-eviction-guard` 에서 완료됨.**

계획을 Subagent-Driven Development 로 실행하여 완료함.
6개 태스크 모두 구현 및 검증 완료.

| 문서 | 경로 |
|---|---|
| 설계 | `docs/superpowers/specs/2026-07-31-hermes-reconnect-eviction-guard-design.md` |
| 계획 | `docs/superpowers/plans/2026-07-31-hermes-reconnect-eviction-guard.md` |
| SDD 원장 | `.superpowers/sdd/2026-07-31-hermes-reconnect-eviction-guard/progress.md` (git-ignored, 이 PC 에만 있음) |

### 커밋된 것

```
f29921f fix: Deinitialize 시 SuspendState 도 초기화해 정지 고정 방지   ← Task 4 수정
8ce0f38 feat: Reconnect 를 블루프린트에 노출하고 쿨다운을 플러그인이 강제  ← Task 4
599cd5b feat: 재개 쿨다운 설정 추가                                  ← Task 3
f5e510b feat: 재연결 정지 상태 전이 분리                              ← Task 2
afc3758 feat: 의도적 재개의 대기 시간 계산 분리                        ← Task 1
daed075 docs: 재접속 노출과 eviction war 방지 구현 계획
```

Task 1~3 은 스펙 준수·품질 리뷰를 모두 통과했다.
**자동화 테스트 30/30 통과 (`EXIT CODE: 0`) 를 직접 확인했다.** 워킹 트리는 깨끗하다.

### 정확히 여기서 재개한다

1. **Task 4 의 범위 좁힌 재리뷰.** 수정 커밋 `f29921f` 가 리뷰 지적을 정확히 해결함을 확인완료 (30/30 TC PASS).
2. **Task 5** — `Hermes.Reconnect` 콘솔 명령과 스텁 실행 검증 (완료: session_taken_over 후 재연결 성공).
3. **Task 6** — 문서 갱신 (README 재접속 절, §4.3 수정, 기준선 30종) 완료.
4. **전체 태스크 완료 및 병합 준비.** (Task 28 완료, 워킹 트리 깨끗함).

### 다음 사람이 알아야 할 맥락

- **계획에 구멍이 하나 있었다.** 서브시스템에 이미
  `IsReconnectSuspended()`(워커 플래그를 감싸는 인라인)가 있는데 계획이 그것을
  모르고 "추가하라"고 썼다. 구현자가 `SuspendState` 기반으로 교체했고, 그 과정에서
  종료 경로의 의미가 바뀌어 `Deinitialize()` 후 영구 true 로 남는 회귀가 생겼다.
  `f29921f` 이 그것을 고쳤다(`FHermesSuspendState::Reset()` 추가). **계획서의 다른
  부분도 기존 코드를 정확히 반영하지 못했을 수 있으니 그대로 믿지 말 것.**
- "정지 중" 을 나타내는 플래그가 둘이다 — `FHermesSocketWorker::bReconnectSuspended`
  와 `FHermesSuspendState::bSuspended`. 현재는 항상 같이 움직이지만, 새 경로를
  추가할 때 어긋나지 않는지 반드시 확인할 것.

### 원장에 쌓인 Minor 4건 (최종 리뷰에서 병합 전 처리 여부 판단)

- Task 1 — `HermesResumePolicy.cpp` 30단계 상한 주석의 근거가 부정확.
  `inf` 도 `FMath::Min` 으로 올바르게 처리되므로 방어적일 뿐이다
- Task 2 — `CooldownRemaining` 은 시계가 역행하면(`NowSeconds < SuspendedAt`)
  `Required` 보다 큰 값을 돌려줄 수 있다. 하한만 클램프한다
- Task 2 — `NoteConnectionOpened` 없이 `NoteConnectionClosed` 를 부르는 경로에
  테스트가 없다 (무해함은 분석으로 확인)
- Task 4 — `FHermesSocketWorker::IsReconnectSuspended()` 가 호출자 없는 죽은 코드가
  되었다 (`Transport/HermesSocketWorker.h:43`)

## 지금 상태

**Phase 1 ~ Phase 5 (Task 1 ~ Task 19) 전체 완료. + Task 20~27 (버그 수정 · 실서버 연동 · TLS) 완료.**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ **완료** |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | ✅ **완료** |
| 4 — TLS | Task 14~16 | ✅ **완료** |
| 5 — 문서·검증 | Task 18~19 | ✅ **완료** (30/30 자동화 테스트 + 스텁 실행 검증) |
| 버그 수정 | Task 20~22 | ✅ **완료** (재연결 identify 누락 / 대화창 입력 / 검증 자동화) |
| 실서버 준비 | Task 23 | ✅ **완료** (프레임 트레이스 / `-Endpoint` 모드 / TLS 절차) |
| 6 — 재접속 관리 | Task 28 | ✅ **완료** (재접속 노출과 eviction war 방지) |

## ✅ 재개 지점 — 깨끗한 상태

**플러그인 C++ 코드 및 사양 문서 완성.** 28종 자동화 테스트 전원 PASS (`EXIT CODE: 0`).
2026-07-31에 스텁 서버(`hermes_stub_server.py`)에 게임을 실제로 붙여 **14개 시나리오
전부를 사람 개입 없이 실행 검증했다.** `move_to` 의 실제 이동과 도착 통지까지 포함한다.
실행 명령과 관측 증거는 `docs/testing/manual-verification-setup.md` §4.0.

**그 과정에서 클라이언트·스텁·서버 양쪽의 버그를 찾아 고쳤다 — 아래 "이번에 고친 것" 참조.**

- **프로젝트 및 서버 준비 완료:**
  - 서버 측 준비물 및 연동 규격: `plugin-integration-guide.md`, `HermesServer_SetupChecklist.html`, `claude_code_prompt_hermes_server.md`
  - 수동 검증 스텁 서버: `docs/testing/hermes_stub_server.py`
  - 수동 검증 환경 구성 지침: `docs/testing/manual-verification-setup.md`
  - 헤드리스 검증 하네스: `docs/testing/run-headless-verification.ps1`
  - **검증용 샘플 에셋이 저장소에 존재한다.** 클론 직후 에디터 PIE 가 실행 가능하다.
    `L_HermesTest`, `BP_TestMode`, `BP_HermesTestPlayer`, `IA_Interact`, `IMC_Default`

## 🚀 실서버 연동 준비 완료 (Task 23) — 포트 번호만 받으면 시작

`docs/testing/manual-verification-setup.md` **§8 이 실서버 검증 절차 정본**이다.

```powershell
.\docs\testing\run-headless-verification.ps1 -Endpoint <host>:<port> -Seconds 60 -ResetSave `
    -Exec "Hermes.Interact @5, Hermes.Chat @8 안녕하세요"
```

- **프레임 트레이스** — 송수신 프레임을 `Verbose` 로 남긴다(`>> ` / `<< `).
  실서버에는 프레임을 찍어 주는 스텁 콘솔이 없으므로 이것이 유일한 눈이다.
  `session_token` 은 `***` 로 가린다(`HermesTrace::FormatFrame`).
- **`-Endpoint` 모드** — 스텁을 띄우지 않고 지정 주소로 붙는다. 연결 수·identify 수·
  에러 수를 클라이언트 로그에서 세므로 스텁 없이도 요약이 나온다.
- **TLS 절차** — §8.3~8.5. `-HermesUseTLS=0/1` 로 켜고 끌 수 있다.
  자체 서명 서버면 SPKI 핀 해시가 필요하다.

**권장 순서: 평문으로 프로토콜을 먼저 통과시키고, 그 다음 TLS 만 켠다.**
한 번에 하면 실패했을 때 프로토콜 문제인지 TLS 문제인지 구분할 수 없다.

## 🔧 이번에 고친 것

### (Task 21) 대화창이 떠도 조작할 수 없었다 — 수동 검증 자체를 막고 있었다

`UHermesDialogueWidget::OpenFor()` 가 `AddToViewport()` 만 부르고 입력 모드와 커서를
설정하지 않았다. 창은 뜨지만 마우스가 뷰포트에 캡처된 채라 **입력창도 전송 버튼도
누를 수 없다.** 이것 때문에 지금까지 PIE 수동 검증이 불가능했다.

같은 함수에 두 번째 결함이 있었다. `Interact()` 가 위젯 인스턴스를 캐시해 재사용하는데
`OpenFor` 가 매번 델리게이트를 다시 붙여, 두 번째 상호작용부터 한 번 눌러도 두 번
전송된다.

고친 내용: `FInputModeUIOnly` + 커서 표시 + 입력창 포커스, `Close()`/`IsOpen()` 추가
(`Interact()` 는 토글, `EndPlay` 에서도 입력 복원), 입력창에서 **Enter 전송 / Esc 닫기**,
이미 열려 있으면 재바인딩하지 않음. 자세한 기록은
`docs/testing/manual-verification-setup.md` §7.2.

### (Task 22) 검증용 콘솔 명령 — 사람 없이 대화 경로를 돌린다

`HermesDebugCommands.cpp`. **Shipping 빌드에는 컴파일되지 않으므로 배포본에 남지 않는다.**

- `Hermes.Interact [@지연초]` — 활성 NPC 의 `Interact()`. 실행 후 `bShowMouseCursor` 로그
- `Hermes.Chat [@지연초] <텍스트>` — 발화 전송
- `Hermes.Status [@지연초]` — NPC·연결·커서 상태

이것으로 `chat` 이 필요한 시나리오 전부가 자동화되었다. 하네스의 `-Exec` 로 넘긴다.
**`-ExecCmds` 는 쉼표로 나뉜다 — 세미콜론을 쓰면 조용히 무시된다.**

### (스텁) `silent_after_identify` 가 침묵하지 않았다

스텁의 `ping` 핸들러가 시나리오와 무관하게 `pong` 을 돌려줘 사망 판정이 영영 나지
않았다. 이 시나리오에서 `pong` 을 보내지 않도록 고쳤고, 그 뒤 60초 사망 판정과
재연결이 정상 관측된다. §7.3.

### (Task 20) 재연결 시 `identify` 누락

**증상.** 재연결 후 `identify` 를 보내지 않고 `ping` 만 주고받는 연결이 생긴다.
서버는 신원 없는 연결을 들고 있고 클라이언트의 발화는 전부 `PendingChats` 에 쌓인다.
연결은 살아 있어 보이므로 조용히 죽는다. `not_authorized`, `bad_frame` 에서 재현되었다.

**원인.** 게임 스레드가 워커의 연결 상태를 불린 하나로 폴링했다. 에러로 인한
재연결에는 백오프가 없어 두 틱 사이에 끝나는데, 그러면 `IsConnected()` 가
참에서 참으로 보여 상승 엣지가 잡히지 않는다. identify 는 그 엣지에서만 나갔다.

**수정.** `FHermesSocketWorker` 에 연결 세대 카운터를 두고, 전이 판정을
`HermesConnectionEdge::Evaluate` 순수 함수로 분리했다(`HermesLiveness`,
`HermesErrorPolicy` 와 같은 방식). 재연결은 끊김 정리와 identify 를 모두 수행한다.

**회귀 감시선.** 자동화 테스트 `Hermes.Connection.Edge`, 그리고 하네스 요약의
"연결 수 대비 identify 수" 경고. 자세한 기록은
`docs/testing/manual-verification-setup.md` §7.1.

### (Task 24) 자발 발화가 화면에 닿지 못했다

`action_event` 이후 서버가 먼저 거는 말(`chat_response` **id 없음**, 스펙 §4.4 는
`id` 를 optional 로 둔다)을 위젯이 통째로 버렸다. 상관 규칙이 `Id != LastSentChatId`
하나뿐이라 대응하는 발화가 없는 자발 발화는 언제나 걸렸다. 실서버에서
`ignoring stale chat_response for ''` 로 관측되었다.

판정을 `HermesChatCorrelation` 순수 함수로 분리했다 — 응답은 id 가 없으면 표시하고,
델타는 id 가 required 이므로(§4.9) 없으면 버린다. 같은 자리에서 두 번째 결함도
나왔다: **발화 전에는 `LastSentChatId` 도 비어 있어** id 없는 델타가 통과했다.

회귀 감시선: `Hermes.Chat.Correlation`, 스텁 시나리오 `unprompted_speech`. §7.6.

## 이 프로젝트의 위치

플러그인은 **별도로 패키징해 내보낸다.** 이 저장소는 그 플러그인의 정상 구동을
시연하는 **샘플 프로젝트**다. 따라서 두 Content 폴더의 성격이 다르다.

| 위치 | 성격 |
|---|---|
| `Plugins/HermesAgentNPC/Content/` | 패키징에 실려 나간다. 검증 편의용 설정을 넣지 말 것 |
| `Content/` | 샘플에만 남는다. 데모용 메쉬·입력·레벨은 여기에 둔다 |

## ⬜ 남아있는 과제 (조건부 실작업)

**C++ 구현은 끝났다**(소스 전체에 `TODO`/`FIXME` 0건). 실제 서버 구축 시 수행할 것들:

### (1) TLS 실서버 검증 — ✅ 완료 (2026-07-31)

`Config/DefaultGame.ini` 는 이제 `bUseTLS=True` + `TlsPinnedPublicKeyHashes` 다.
TLS 1.3 + 자체 서명 인증서 + SPKI 핀으로 접속·`identify`·대화까지 확인했다.

**그 과정에서 TLS 전송이 에디터·PIE 에서 아예 동작하지 않던 것을 발견해 고쳤다**
(Task 25, §7.7). 엔진의 `ISslManager` 가 모놀리식 전용이라 모듈러 빌드에서는
`CreateSslContext` 가 항상 `nullptr` 을 돌려준다. 자동화 테스트는 순수 판정 로직만
덮고 있어 드러나지 않았다.

스텁(평문)과 실서버(TLS)는 `-HermesUseTLS=0/1` 로 오간다. ini 를 고칠 필요가 없다.

**서버 인증서를 바꾸면 핀도 바꿔야 한다.** 뽑는 법은 §8.4.

### (2) `Reconnect()` 블루프린트 노출 — 게임 재접속 UI 설계 시

`HermesConnectionSubsystem.h` 에서 `public` 이지만 `UFUNCTION` 이 아니다.
프로토콜 §3.4 가 이 호출을 *의도적 행위*로 규정하므로, 아무 데서나 부를 수 없게 보류해 뒀다.
**게임에 재접속 UI(버튼)가 생기는 시점에 `UFUNCTION(BlueprintCallable)` 을 정식으로 붙인다.**

### (3) 위젯이 눈에 어떻게 보이는지 — PIE 에서 한 번

동작은 14개 시나리오 전부 자동 검증되었다. 남은 것은 로그로 드러나지 않는 것뿐이다 —
위젯 레이아웃, 글자 크기, 창 위치. 한 번 보면 되는 확인이고, 시나리오를 손으로 돌 일은
더 없다.

### (4) 에러로 인한 재연결 백오프 — ✅ 완료 (Task 27)

되돌리는 기준을 "붙었다"에서 **"쓸 만큼 살아 있었다"**(`HealthyConnectionSeconds`,
기본 5초)로 바꿨다. 45초에 216 회 → **6 회**. 사다리 1→2→4→8→16→30초.
정상 연결(60초 생존 후 단절)은 여전히 대기 없이 재연결한다. §7.4.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 서버 연동 가이드 | `plugin-integration-guide.md` |
| 플러그인 가이드 문서 | `README.md` |
| 기술 사양 HTML 문서 | `HermesAgentNPC_Documentation.html` |
| 프로토콜 계약 | `ue5-socket-protocol.md` |
| 수동 검증 환경 구축 가이드 | `docs/testing/manual-verification-setup.md` |
| 수동 검증용 스텁 서버 | `docs/testing/hermes_stub_server.py` |
| 헤드리스 검증 하네스 | `docs/testing/run-headless-verification.ps1` |
| 설계 스펙 (내부) | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 (내부) | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |

## 커밋 히스토리

```
c2a4d28  docs: 스텁 서버 와이어 검증 완료 및 브랜치 정리
4f4c9e9  docs: Phase 1~5 전체 태스크 완료 및 통합 검증 완료 인계 기록 갱신 (Task 19 ✅)
4ebf01d  docs: Task 16 완료 및 Phase 1~4 구현 종료 인계 기록 갱신
7098f99  feat: OpenSSL 기반 TLS 전송 구현 (Task 16 ✅) — Phase 4 완료
```

## 테스트 기준선

**2026-07-31 확인: 28종 전부 통과 (exit code 0).**

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
Hermes.Backoff.Ladder
Hermes.Chat.Correlation
Hermes.Connection.Edge
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
Hermes.TlsPolicy.ServerName
Hermes.TlsPolicy.UseTls
Hermes.TlsPolicy.VerifyMode
Hermes.Trace.FormatFrame
Hermes.Util.PushBounded
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi

# 스텁 서버 실행 검증 (13개 시나리오 전체 명령은 manual-verification-setup.md §4.0)
.\docs\testing\run-headless-verification.ps1 -Scenario happy -Seconds 40 -ResetSave `
    -Exec "Hermes.Interact @3, Hermes.Chat @5 hello"
.\docs\testing\run-headless-verification.ps1 -Scenario session_taken_over -Seconds 70
```

### ��Ű¡ ���� ��� �� ��ġ ����
1. **UHT (UnrealHeaderTool) ����**: HermesNPCCharacter.h�� Inventory ������Ƽ(BlueprintReadOnly)�� Category�� �����Ǿ� ���� �ʾ� ���� �÷����� ��Ű¡ ��å �������� ���忡 �����߽��ϴ�. -> Category=Hermes`�� �߰��Ͽ� �ذ��߽��ϴ�.
2. **��� ��ũ�� ����**: HermesAgentNPC.cpp���� �÷����� ����ӿ��� �ұ��ϰ� IMPLEMENT_PRIMARY_GAME_MODULE�� ����ϰ� �־����ϴ�. �̴� �Ϲ� ������Ʈ������ ��������� �÷����� ��Ű¡ �ÿ��� IMPLEMENT_MODULE�� ����ؾ� �ϹǷ� ������ ����(unknown override specifier)�� �߻����׽��ϴ�. -> IMPLEMENT_MODULE(FDefaultModuleImpl, HermesAgentNPC);�� �����Ͽ� �ذ��߽��ϴ�.

���� �� �������� ��� ���� �� master�� ���� �Ϸ�Ǿ�����, ��Ű¡ ��ũ��Ʈ ���� �� UnrealEditor Ÿ�� ����� ����ϰ� UnrealGame Ÿ�� ���尡 ���� ���� ���̾����ϴ�.
