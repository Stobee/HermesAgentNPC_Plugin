# 수동 검증 환경 구축 가이드

> **이 문서가 필요한 이유.** `Content/` 에 `.gitkeep` 하나뿐이고 맵·에셋이 전혀 없다.
> 자동화 테스트 21종은 모두 순수 로직이라 에셋 없이 돌지만, **PIE 가 필요한 검증은 하나도
> 할 수 없다.** 막혀 있는 것은 두 가지다.
>
> - **Task 13d Step 5** — `session_taken_over` 후 재연결이 멈추는지 확인
> - **Task 19** — 통합 검증 체크리스트 전체
>
> 아래 오브젝트를 만들면 둘 다 진행할 수 있다. 스텁 서버는
> `docs/testing/hermes_stub_server.py` 로 이미 준비되어 있다.

- 대상 엔진: UE 5.8
- 최종 갱신: 2026-07-30 (Task 13e 완료 시점)

---

## 1. 만들어야 하는 것

네 개다. 이름이 **정확히** 일치해야 하는 곳은 위젯 바인딩뿐이고, 나머지는 자유롭다.

| # | 에셋 | 부모 클래스 | 경로 제안 |
|---|---|---|---|
| 1 | `WBP_HermesDialogue` | `HermesDialogueWidget` | `Content/Hermes/UI/` |
| 2 | `BP_HermesNPC` | `HermesNPCCharacter` | `Content/Hermes/NPC/` |
| 3 | `BP_HermesTestPlayer` | `Character` (또는 기본 3인칭 폰) | `Content/Hermes/Player/` |
| 4 | `L_HermesTest` | (레벨) | `Content/Hermes/Maps/` |

### 1.1 `WBP_HermesDialogue` — 대화 위젯

`UHermesDialogueWidget` 를 부모로 하는 Widget Blueprint 를 만든다.

**세 위젯의 변수명이 C++ 의 `BindWidget` 이름과 정확히 같아야 한다.** 다르면 바인딩이
`nullptr` 로 남고, 컴파일은 되지만 아무 동작도 하지 않는다.

| 위젯 변수명 | 위젯 타입 | 역할 |
|---|---|---|
| `InputBox` | Editable Text Box | 플레이어 입력 |
| `DialogueText` | Text | NPC 응답·상태 표시 |
| `SendButton` | Button | 전송 |

> `Is Variable` 체크박스를 켜야 변수로 노출된다. 세 위젯 모두 켠다.
>
> `DialogueText` 에는 **여러 줄이 들어온다**(스트리밍 누적 텍스트). `Auto Wrap Text` 를
> 켜고 폭을 넉넉히 잡아야 응답이 잘리지 않는다.

디자이너 트리 예시 — 레이아웃은 자유다.

```
Canvas Panel
└─ Vertical Box
   ├─ DialogueText   (Text,             Auto Wrap Text = true)
   ├─ InputBox       (Editable Text Box)
   └─ SendButton     (Button)
      └─ Text ("전송")
```

C++ 가 `SendButton->OnClicked` 를 직접 구독하므로 **블루프린트에서 클릭 이벤트를 따로
연결하지 않는다.** 연결하면 한 번 누를 때 두 번 전송될 수 있다.

### 1.2 `BP_HermesNPC` — NPC

`AHermesNPCCharacter` 를 부모로 하는 Blueprint 를 만든다.

**Class Defaults 에서 설정할 것:**

| 속성 | 값 | 비고 |
|---|---|---|
| `Dialogue Widget Class` | `WBP_HermesDialogue` | **비우면 `Interact()` 가 조용히 아무것도 안 한다** |
| `Auto Register As Active Npc` | `true` | 레벨에 NPC 를 하나만 둘 것이므로 켠다 |

- `AIControllerClass` 와 `AutoPossessAI` 는 C++ 생성자가 이미 설정한다
  (`AHermesNPCAIController`, `PlacedInWorldOrSpawned`). **건드리지 않는다.**
- `Capsule` 이 바닥을 뚫지 않게 배치 높이만 맞춘다.

> **메쉬는 필요 없다.** 플러그인 소스 전체에 메쉬 참조가 없다. 이동은 `CapsuleComponent` +
> `CharacterMovementComponent`(둘 다 `ACharacter` 기본 제공)로 동작하고 내비게이션도 캡슐
> 기준이며, `eta_seconds` 는 `GetCharacterMovement()->GetMaxSpeed()` 를 쓴다.
>
> 다만 **메쉬가 없으면 이동을 눈으로 볼 수 없다.** `move_to` 의 실제 증거는 스텁 콘솔의
> `action_event {completed, arrived:true}` 이고 그것이 육안 확인보다 확실하지만, "엉뚱한
> 곳으로 갔다" 와 "아예 안 갔다" 를 구분하려면 보이는 편이 낫다.
>
> 그럴 때는 **스켈레탈 메쉬가 아니라 Static Mesh(Cube) 컴포넌트 하나**를 붙인다. AnimBP 가
> 필요 없어 T-포즈 문제도, 애님 에셋 준비도 없다. 캡슐에 큐브를 붙이고 스케일만 줄이면 된다.

> 레벨에 이 클래스 액터를 **둘 이상** 놓으려면 하나만 `Auto Register` 를 켜고 나머지는
> 꺼야 한다. 플러그인은 NPC 한 명만 다루며, 여럿이 등록하면
> `활성 NPC 교체: A -> B` 경고가 뜨고 마지막 것만 대상이 된다.

### 1.3 `BP_HermesTestPlayer` — 플레이어

기본 3인칭 템플릿 폰이면 충분하다. **한 가지만 추가한다: `Interact()` 를 부를 입력.**

플러그인이 플레이어에게 요구하는 것은 `UGameplayStatics::GetPlayerPawn(this, 0)` 이
**존재하고 위치를 갖는 것** 뿐이다(`follow_player` 가 그 위치를 추적한다). 여기도 메쉬는
필요 없다. 다만 **카메라 컴포넌트는 있는 편이 낫다** — 없으면 폰의 눈높이 시점으로 떨어져
조작이 불편하다. 3인칭 템플릿 폰을 쓰거나 SpringArm + Camera 를 붙인다.

`AHermesNPCCharacter::Interact()` 는 `BlueprintCallable` 이지만 **아무도 부르지 않는다.**
호출 경로를 직접 만들어야 대화창이 열린다. 가장 간단한 방법:

```
Enhanced Input Action (IA_Interact, 예: E 키)
  └─ Get Actor Of Class (Actor Class = BP_HermesNPC)
     └─ Interact
```

거리 검사가 필요하면 `Get Distance To` 로 게이트를 걸어도 되지만, 검증용으로는 없어도 된다.

> **Enhanced Input 함정.** Input Action 을 만들고 이벤트를 연결해도, **Input Mapping Context 를
> 등록하지 않으면 이벤트가 영영 발동하지 않는다.** 폰이나 플레이어 컨트롤러의 `BeginPlay` 에서
> `Get Player Controller` → `Get Enhanced Input Local Player Subsystem` → `Add Mapping Context`
> 를 호출해야 한다. 대화창이 안 열릴 때 가장 먼저 볼 곳이다.

> `BecomeActiveHermesNpc()` 도 `BlueprintCallable` 이다. 여러 NPC 를 두고 대상 전환을
> 시험하려면 이것도 입력에 걸어 둔다.

### 1.4 `L_HermesTest` — 테스트 레벨

| 필요한 것 | 이유 |
|---|---|
| 바닥(Floor) | 당연 |
| **`NavMeshBoundsVolume`** | **없으면 `move_to` 와 `follow_player` 가 전부 실패한다** |
| `PlayerStart` | 스폰 |
| `BP_HermesNPC` 액터 1개 | 대화·액션 대상 |
| 목표 지점 표식(선택) | `move_to` 목적지 확인용. Cube 하나 |

`NavMeshBoundsVolume` 을 놓고 `P` 키로 초록 내비메시가 바닥을 덮는지 확인한다.
**내비메시가 없으면 `MoveToLocation` 이 `Failed` 를 돌려주고 클라이언트는
`action_result ok=false, error="path blocked"` 를 보낸다** — 코드는 정상 동작이지만
이동 검증은 못 한다.

`move_to` 스텁 시나리오는 좌표 `(500, 500, 100)` 으로 보낸다. 내비메시가 그 지점을
덮도록 볼륨을 넉넉히 잡거나, 스크립트의 좌표를 레벨에 맞게 고친다.

### 1.5 GameMode 와 기본 맵

- GameMode 를 만들어 `Default Pawn Class` 를 `BP_HermesTestPlayer` 로 지정하고
  레벨의 World Settings 에 지정한다.
- `Project Settings > Maps & Modes` 의 **Editor Startup Map** 과 **Game Default Map** 을
  `L_HermesTest` 로 지정한다. 지금은 없는 `Content/Maps/SampleMap` 을 가리켜
  에디터 기동 때마다 `Can't find file .../SampleMap.umap` 경고가 난다.

---

## 2. 플러그인 설정

`Project Settings > Plugins > Hermes Agent NPC` 에서 확인한다.
값은 `Config/DefaultGame.ini` 에 저장된다.

| 설정 | 검증용 권장값 | 비고 |
|---|---|---|
| `Host` | `127.0.0.1` | 기본값 |
| `Port` | `8770` | 기본값. 스텁 서버 기본값과 같다 |
| `Use TLS` | `true` (그대로) | **아직 아무 영향이 없다** — TLS 는 Task 16 에서 구현된다. 현재 전송은 항상 평문이다 |
| `Keep Alive Ping Interval Seconds` | `20` → 검증 시 `5` | 짧게 줄이면 ping 을 빨리 볼 수 있다 |
| `Peer Timeout Seconds` | `60` → 검증 시 `15` | 사망 판정을 빨리 보려면 줄인다 |
| `Chat Response Timeout Seconds` | `60` → 검증 시 `10` | 타임아웃 검증 대기시간을 줄인다 |
| `Save Slot Name` | `HermesPlayer` | 자격 증명 슬롯 |

> **`PeerTimeoutSeconds` 는 `KeepAlivePingIntervalSeconds` 의 2배 이상으로 둔다.**
> 미만이면 기동 시 경고가 뜨고 정상 연결이 죽은 것으로 오판될 수 있다.
> 예: ping 5초 / timeout 15초.

**호스트·포트는 커맨드라인으로 덮을 수 있다** (ini 를 건드리지 않고 시험할 때 편하다):

```
-HermesHost=127.0.0.1 -HermesPort=9999
```

### 2.1 자격 증명 초기화

신규 발급 경로를 다시 보려면 SaveGame 을 지운다.

```powershell
Remove-Item "C:\Work\HermesAgentNPC\Saved\SaveGames\HermesPlayer.sav" -ErrorAction SilentlyContinue
```

### 2.2 로그 보기

플러그인은 전용 카테고리 `LogHermes` 를 쓴다. Verbose 까지 보려면:

```
-LogCmds="LogHermes Verbose"
```

또는 PIE 콘솔에서 `Log LogHermes Verbose`.

> **연결 성공과 `identify`/`identified` 에는 로그가 없다.** 정상 경로는 조용하다.
> 연결됐는지 확인하는 방법은 두 가지다: (a) 위젯의 `"연결 중..."` 이 사라지는지,
> (b) 스텁 서버 콘솔에 `[+] connected` 와 프레임 로그가 찍히는지. 후자가 확실하다.

---

## 3. 스텁 서버

`docs/testing/hermes_stub_server.py` — 표준 라이브러리만 쓴다. 설치할 것 없다.

```powershell
# 시나리오 목록
py docs\testing\hermes_stub_server.py --list

# 정상 흐름
py docs\testing\hermes_stub_server.py --scenario happy

# 재연결 정지 확인 (Task 13d Step 5)
py docs\testing\hermes_stub_server.py --scenario session_taken_over
```

프레이밍은 클라이언트와 동일한 **4바이트 big-endian 길이 프리픽스 + UTF-8 JSON** 이다.
주고받는 모든 프레임을 콘솔에 찍으므로 클라이언트가 실제로 무엇을 보내는지 그대로 볼 수 있다.

`--once` 를 주면 한 연결만 처리하고 끝난다. 재연결이 **멈췄는지** 확인할 때 유용하다 —
서버가 살아 있으면 "멈춤"과 "서버가 죽어서 못 붙음"을 구분할 수 없기 때문이다.

---

## 4. 검증 절차

### 4.1 Task 13d Step 5 — 종료성 에러 시 재연결 정지

계획서가 남겨 둔 미완 항목이다. 두 부분으로 나뉜다.

**(a) 재연결이 멈추는지**

1. `py docs\testing\hermes_stub_server.py --scenario session_taken_over` 실행
2. PIE 시작
3. 스텁 콘솔에 `identify` 수신 → `error session_taken_over` 송신 → `closing after fatal error` 확인
4. UE 로그에서 아래를 확인한다 (`LogHermes: Error`):

```
fatal server error session_taken_over: stub server forcing session_taken_over --
stopping the reconnect loop. Retrying does not heal this;
call Reconnect() to attempt it deliberately.
```

5. **이후 30초 이상 스텁 콘솔에 새 `[+] connected` 가 찍히지 않아야 한다.**
   이것이 이 항목의 핵심이다. 찍히면 정지가 동작하지 않는 것이다.
6. `unsupported_version_error` 시나리오로 같은 것을 반복한다.

**(b) eviction war 가 없는지**

두 클라이언트를 동시에 띄워 서로를 반복해서 걷어내지 않는지 본다.

1. 스텁을 `happy` 로 실행
2. 에디터에서 PIE 실행 (클라이언트 A)
3. `Project Settings > Maps & Modes` 와 무관하게, **두 번째 인스턴스**를 띄운다 —
   `Play > Advanced Settings > Number of Players` 를 2로 하거나, 별도로 에디터를
   한 번 더 실행한다
4. 스텁이 두 연결을 모두 받고, **둘 다 조용히 유지되는지** 확인한다

> 이 스텁은 실제 세션 탈취를 구현하지 않는다(같은 신원의 새 연결이 와도 기존 것을
> 끊지 않는다). 진짜 eviction war 검증은 **탈취를 구현한 실제 서버**가 필요하다.
> 스텁으로 확인할 수 있는 것은 "클라이언트가 `session_taken_over` 를 받으면 멈춘다"
> 까지다 — (a)가 그것이고, 그것이 eviction war 를 막는 클라이언트 측 조건 전부다.
> (b)는 실서버가 준비된 뒤로 미뤄도 된다.

### 4.2 Phase 3 기능 검증

Task 11~13e 로 들어간 것들이다. 각 항목의 시나리오와 기대 결과.

| 시나리오 | 확인할 것 | 관련 |
|---|---|---|
| `happy` | 응답이 **한 글자씩 늘어나며** 표시되고 마지막에 전문으로 확정된다 | Task 11 |
| `happy` | 전송 직후 `"생각 중..."` 이 뜨고 델타가 오면 대체된다 | Task 11 |
| `silent_after_identify` | `PeerTimeoutSeconds` 후 `peer silent for Ns, treating connection as dead` 로그 + 재연결 | Task 12 |
| `happy` (유휴 방치) | `KeepAlivePingIntervalSeconds` 마다 스텁 콘솔에 `ping` 수신 → `pong` 송신 | Task 12 |
| `chat_timeout` | `ChatResponseTimeoutSeconds` 후 `chat c-0001 timed out` 로그 + UI 에 `"응답을 받지 못했습니다."` | Task 13 |
| `server_busy` | **타임아웃을 기다리지 않고 즉시** UI 가 실패로 바뀐다 | Task 13e |
| `error_without_id` | 로그만 남고 **UI 는 계속 `"생각 중..."`** (타임아웃까지 유지) | Task 13e |
| `stale_delta` | `"이 텍스트는 화면에 나오면 안 된다"` 가 **표시되지 않는다**. Verbose 로 `ignoring stale chat_delta` | Task 13 |
| `interleaved_turns` | 두 번째 발화 응답에 `B조각` 이 섞이지 않는다 | Task 13, §4.9 |
| `move_to` | NPC 가 **실제로 이동한다**. `action_result` 에 `started`/`eta_seconds`, 도착 후 `action_event {completed, arrived}` | Task 13b |
| `move_to` | **`action_result` 에 `arrived` 가 없어야 한다.** 있으면 스텁이 `프로토콜 §4.5 위반` 을 찍는다 | Task 13b |
| `unknown_command` | `ok=false`, `error="unsupported command"` 회신 | §6 |
| `not_authorized` | `server rejected stored credentials ...` 로그 후 **신규 발급 요청**(`identify` 에 `player_id` 없음) | Task 13d |
| `bad_frame` | 정지하지 **않고** 백오프 재연결한다 | Task 13c/13d |

**`move_to` 검증은 이미 도착한 좌표를 한 번 시험해 볼 것.** NPC 를 목표 지점에
세워 두고 같은 좌표로 `move_to` 를 받으면 `AlreadyAtGoal` 경로를 탄다. 이 경로는
`action_result` 직후 `action_event {completed}` 가 **곧바로** 나가야 한다.

### 4.3 재연결 재개

`Reconnect()` 는 C++ `public` 이지만 `UFUNCTION` 이 아니라 **블루프린트에서 부를 수 없다.**
정지 후 재개를 시험하려면 방법이 둘이다.

- PIE 를 다시 시작한다(가장 간단. 정지 플래그는 세션마다 초기화된다)
- 또는 임시로 `Reconnect()` 에 `UFUNCTION(BlueprintCallable)` 을 붙여 입력에 걸고,
  확인 후 되돌린다

> 게임이 실제로 재개 UI 를 갖게 되면 그때 `UFUNCTION` 노출을 정식으로 넣는 것이 맞다.
> 지금 노출하지 않은 것은 호출 시점이 게임 설계에 달려 있고, 프로토콜 §3.4 가 이 호출을
> **의도적 행위**로 규정하기 때문이다 — 아무 데서나 부를 수 있게 두면 자동 재시도로
> 퇴화하기 쉽다.

---

## 5. 알려진 함정

| 증상 | 원인 |
|---|---|
| 대화창이 안 열린다 | `BP_HermesNPC` 의 `Dialogue Widget Class` 가 비어 있다. 또는 `Interact()` 를 부르는 입력이 없다. 또는 **Input Mapping Context 를 등록하지 않았다**(§1.3) |
| NPC 가 안 보인다 | 정상이다. 메쉬는 필수가 아니다. 보고 싶으면 Static Mesh(Cube) 컴포넌트를 붙인다(§1.2) |
| 내비메시가 있는데 `path blocked` | NPC **스폰 지점**이 내비메시를 벗어나 있다. 목표 좌표만 덮여도 부족하다 |
| 창은 열리는데 입력·버튼이 죽어 있다 | 위젯 변수명이 `InputBox`/`DialogueText`/`SendButton` 과 다르다. 또는 `Is Variable` 이 꺼져 있다 |
| 한 번 눌렀는데 두 번 전송된다 | 블루프린트에서 `SendButton` 클릭을 따로 연결했다. C++ 가 이미 구독한다 |
| `move_to` 가 항상 `path blocked` | `NavMeshBoundsVolume` 이 없거나 목표 좌표를 덮지 않는다 |
| 연결됐는지 알 수 없다 | 정상 경로에는 로그가 없다. 스텁 서버 콘솔을 볼 것 |
| 재실행마다 새 신원이 발급된다 | `Saved/SaveGames/HermesPlayer.sav` 저장 실패. `failed to save credentials to slot` 경고 확인 |
| 기동 때 `SampleMap.umap` 경고 | Maps & Modes 가 없는 맵을 가리킨다. §1.5 |

---

## 6. 완료 후 할 일

이 환경이 만들어지면 아래 두 문서의 상태를 갱신한다.

- `docs/superpowers/HANDOFF.md` — "Task 13d 미완 항목" 절을 검증 결과로 대체
- `docs/superpowers/plans/PROGRESS.md` — Task 13d 를 `[~]` 에서 `[x]` 로

`docs/superpowers/plans/integration-checklist.md` 는 Task 9~13b 이전에 작성되어 낡은
항목이 있다. Task 19 착수 시 이 문서의 §4.2 표를 정본으로 삼고 그쪽을 갱신할 것.
