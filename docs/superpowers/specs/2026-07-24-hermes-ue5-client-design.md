# Hermes Agent NPC — UE5 클라이언트 설계 (v1)

- **작성일:** 2026-07-24
- **대상 엔진:** Unreal Engine 5.8, C++ 전용 (Blueprint 노출 없음)
- **플랫폼:** Windows (클라이언트) ↔ Linux Hermes 서버 (`192.168.0.111:8770`, TCP)
- **근거 문서:** `ue5-socket-protocol.md` (프로토콜 v1 전체 스펙), `claude_code_prompt_ue5_client.md` (작업 브리프)

---

## 1. 개요 & 목표

Hermes 에이전트(Linux 서버)와 TCP 소켓으로 통신하는 비서형 NPC를 UE5(C++)로 구현한다.
플레이어와 대화하고, 서버가 파악한 의도에 따라 게임 내 행동(이동, 인벤토리 관리 등)을 실행한다.
대화 기억은 서버가 담당하므로 클라이언트는 안정적인 `player_id`(세션 식별자)만 유지한다.

**성공 기준**
- `ue5-socket-protocol.md`의 프레이밍·메시지 타입을 **정확히 그대로** 구현한다.
- 게임 스레드를 블로킹하지 않고 송수신한다.
- 연결 끊김을 감지하고 지수 백오프로 자동 재연결하며, 동일 `player_id`로 재-identify 하여 대화 세션을 이어간다.
- 초기 4종 액션(`move_to`, `follow_player`, `inventory_manage`, `item_transfer`)을 실행하고, 같은 `id`로 15초 이내에 `action_result`를 회신한다.
- 새 액션은 핸들러 클래스 1개 추가만으로 확장 가능하다.

**비목표 (YAGNI)**
- 완성도 높은 게임플레이(아이템 정의 에셋 파이프라인, 풀 인벤토리 UI, 픽업/드롭 연출 등)는 만들지 않는다. 액션 핸들러는 프로토콜을 완전히 검증할 **최소한**으로 동작하되 확장 용이하게 설계한다.
- Blueprint 노출은 하지 않는다.

---

## 2. 결정 사항 (브레인스토밍 확정)

| 항목 | 결정 |
|------|------|
| 프로젝트 위치 | `C:\Work\HermesAgentNPC`에 **신규 UE5.8 C++ 프로젝트** 생성 |
| 게임 구현 깊이 | **최소 동작 + 확장 용이**. 인벤토리는 `UObject` 아이템을 `TArray`로 보유 |
| `player_id` 저장 | **SaveGame에 영구 저장** (앱 재시작 후에도 동일 UUID 유지) |
| 네트워킹/스레딩 | **접근 A**: 전용 `FRunnable` 워커 스레드 + 스레드 안전 `TQueue` 2개 |

---

## 3. 프로젝트 / 모듈 구조

- 프로젝트명: `HermesAgentNPC` (Blank 템플릿, C++).
- 단일 게임 모듈 `HermesAgentNPC`로 시작.
- `HermesAgentNPC.Build.cs` 의존성:
  `Core`, `CoreUObject`, `Engine`, `Sockets`, `Networking`, `Json`, `JsonUtilities`,
  `AIModule`, `NavigationSystem`, `UMG`, `Slate`, `SlateCore`.

### 레이어 구조

```
[Transport 계층]   FHermesFrameCodec (순수 C++, 엔진 비의존)
                   FHermesSocketWorker : FRunnable  ── 블로킹/논블로킹 Recv/Send + 재연결
                          │  TQueue(수신) / TQueue(송신)  (스레드 경계, SPSC)
                          ▼
[Connection 계층]  UHermesConnectionSubsystem : UGameInstanceSubsystem
                     - 워커 수명 관리, 게임스레드에서 수신 큐 소비(FTSTicker)
                     - identify 핸드셰이크 상태머신, ping/pong
                     - 델리게이트: OnChatResponse, OnConnectionStateChanged
                          │
        ┌─────────────────┴──────────────────┐
        ▼                                     ▼
[Dispatch 계층]                         [대화 UI 계층]
 UHermesActionDispatcher                AHermesNPCCharacter (ACharacter)
  - IHermesActionHandler 레지스트리       + AHermesNPCAIController
  - 화이트리스트 라우팅 → action_result   UHermesDialogueWidget (UUserWidget, C++)
  - 핸들러 4종
```

**설계 원칙**
- `FHermesFrameCodec`는 엔진 비의존 순수 함수로 분리해 자동화 테스트로 단독 검증한다.
- 스레드 경계는 오직 두 개의 `TQueue`(SPSC)뿐. 워커는 소켓만, 게임스레드는 게임플레이만 담당.
- 모든 게임플레이 API(AI 이동, 인벤토리, UMG) 호출은 게임스레드에서만 수행한다.

---

## 4. Transport 계층

### 4.1 `FHermesFrameCodec` (순수 C++, 엔진 비의존)

프레이밍 규약: **4바이트 길이(uint32, big-endian) + UTF-8 JSON 바디**, 바디 최대 1,048,576 바이트(1 MiB).
길이는 바디 바이트 수만 세며 프리픽스 4바이트는 포함하지 않는다.

- `static bool Encode(const FString& JsonBody, TArray<uint8>& OutBytes)`
  - UTF-8 변환 → 바디 길이 계산 → 1 MiB 초과면 `false` → 4바이트 BE 프리픽스 + 바디.
- `class FFrameAccumulator`
  - `void Feed(const uint8* Data, int32 Len)` — 수신 바이트를 내부 버퍼에 누적.
  - `bool TryPop(FString& OutJson)` — 완성 프레임 1개를 꺼냄. 없으면 `false`.
  - `bool HasError() const` / 에러 사유 — `len == 0` 또는 `len > 1048576`이면 프로토콜 에러.
  - **길이 프리픽스 기준으로만 파싱**한다. 단일 `Recv`가 부분 프레임/여러 프레임을 반환해도 정확히 동작해야 한다.

파싱 의사코드:
```
4바이트 확보 안 됨 → 대기
len = uint32_be(첫 4바이트)
if len == 0 || len > 1048576 → 프로토콜 에러(연결 종료 유발)
바디 len 바이트 확보 안 됨 → 대기
body 확보 → 버퍼에서 4+len 소비 → OutJson = utf8_decode(body)
```

### 4.2 `FHermesSocketWorker : FRunnable`

- 소유: `FSocket*` 하나. 상태: `Disconnected → Connecting → Connected`.
- `Run()` 루프:
  - **연결**: `FTcpSocketBuilder`로 `192.168.0.111:8770` 접속.
  - **송신**: 아웃바운드 큐 폴링 → `FHermesFrameCodec::Encode` → 부분 send 루프로 완전 송신.
  - **수신**: 논블로킹 `Recv` → `FFrameAccumulator::Feed` → 완성 프레임을 인바운드 큐로 push.
  - **에러/끊김**: 소켓 에러 또는 accumulator 프로토콜 에러 감지 시 소켓 정리 후 재연결.
- **지수 백오프**: 0.5s → 1s → 2s → … → 최대 30s, 지터 포함. 재연결 성공 시 백오프 리셋.
- **종료**: `Stop()`에서 실행 플래그 내리고 소켓 close. 서브시스템 `Deinitialize`에서 스레드를 확실히 조인(`Kill(true)`).
- **스레드 경계**: `TQueue<FString, EQueueMode::Spsc>` 2개(인바운드/아웃바운드) + 원자적 상태 플래그(`FThreadSafeBool`/`std::atomic`).

---

## 5. Connection 계층 — `UHermesConnectionSubsystem`

`UGameInstanceSubsystem` 상속 → 씬 전환에도 연결 유지.

- **`Initialize`**: SaveGame에서 `player_id` 로드. 없으면 `FGuid::NewGuid()`로 생성 후 SaveGame 저장. 워커 스레드 시작.
- **게임스레드 소비**: `FTSTicker` 콜백에서 인바운드 큐를 드레인 → JSON 파싱 → `type`별 라우팅.
- **핸드셰이크 상태머신**:
  - 연결되면 즉시 `identify { player_id, player_name? }` 송신.
  - `identified` 수신 전까지 상태 = `Identifying`. 이 동안의 `SendChat` 요청은 내부 큐에 보류.
  - `identified { chat_id, ok:true }` 수신 → 상태 `Ready` → 보류된 `chat` flush.
- **재연결 시**: 워커 재접속 → 서브시스템이 **동일 `player_id`로 `identify` 자동 재송신** → 서버가 기존 세션 재개.
- **`type` 라우팅**:
  | 수신 type | 처리 |
  |-----------|------|
  | `identified` | 상태 `Ready` 전환, 보류 chat flush |
  | `chat_response` | `OnChatResponse.Broadcast(text, id)` (위젯 구독) |
  | `action_request` | `UHermesActionDispatcher::Dispatch(payload)`로 전달 |
  | `ping` | 같은 `id`로 `pong` 즉시 송신 |
  | `pong` | keepalive 응답 확인 |
  | `error` | §7 에러 코드별 처리 |
- **자체 keepalive(선택)**: N초마다 `ping` 송신, 응답 지연 시 연결 이상으로 간주.
- **공개 API**:
  - `void SendChat(const FString& Text)` — 클라 생성 `id` 부여 후 `chat` 송신(또는 보류).
  - `FOnChatResponse OnChatResponse` (멀티캐스트 델리게이트).
  - `FOnConnectionStateChanged OnConnectionStateChanged` — UI "생각 중..."/연결 상태 연출용.

---

## 6. Dispatch 계층 — 액션 처리

### 6.1 `IHermesActionHandler` + `UHermesActionDispatcher`

- `FHermesActionPayload = { FString Id, FString Command, TSharedPtr<FJsonObject> Params }`.
- 인터페이스(UINTERFACE):
  - `bool CanHandle(const FString& Command) const`.
  - `void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)`.
  - **콜백/델리게이트 방식** → 이동처럼 완료까지 시간이 걸리는 액션도 나중에 결과 회신(비동기 가능).
- 디스패처:
  - 등록된 핸들러를 순회하며 `CanHandle` 첫 매칭에 라우팅.
  - **화이트리스트 검증**: 매칭 핸들러가 없으면 실행하지 않고 즉시
    `action_result { id, ok:false, error:"unsupported command" }` 회신 (프로토콜 §5 `unknown_command` 대응).
  - **15초 타임아웃 폴백**: Dispatch 시 타이머 등록. 핸들러가 시간 내 `OnDone`을 호출하지 않으면
    `action_result { id, ok:false, error:"timeout" }` 회신(중복 회신 방지 가드 포함).
  - `OnDone` 호출 시 `action_result { id, ok, result?, error? }`를 **요청과 동일한 `id`**로 회신.
- **확장**: 새 액션 = 핸들러 클래스 1개 추가 + 등록 한 줄.

### 6.2 초기 핸들러 4종 (최소 동작)

| 핸들러 | command | 동작 | 성공 result | 실패 예시 |
|--------|---------|------|-------------|-----------|
| `UMoveToActionHandler` | `move_to` | `params.location{x,y,z}`(cm) 검증 → `AAIController::MoveToLocation`. 도착/실패 델리게이트에서 회신 | `{ arrived:true }` | `ok:false, error:"path blocked"` |
| `UFollowPlayerActionHandler` | `follow_player` | `params.enabled` 토글. 활성 시 매 틱 플레이어 위치로 MoveTo 갱신 | `{ following:true/false }` | 좌표/폰 없음 시 `ok:false` |
| `UInventoryActionHandler` | `inventory_manage` | `UHermesInventoryComponent` 대상. `operation`: `list`/`sort`/`drop`(`target`) | `list` → `{ items:[...] }` | 미지원 operation → `ok:false, error:"unsupported operation"` |
| `UItemTransferActionHandler` | `item_transfer` | `direction`(`give`/`receive`), `item_id`, `quantity≥1` 검증 → 인벤토리 아이템 이동 | `{ transferred:N }` | 부족 시 `ok:false, error:"insufficient quantity"` |

- 파라미터 검증(좌표 범위, 존재하지 않는 `item_id`, `quantity` 범위 등)은 각 핸들러 내부 책임.
- 인벤토리 모델: `UHermesInventoryComponent`가 `TArray<UHermesItem*>` 보유.
  `UHermesItem : UObject` = `{ FString ItemId; int32 Quantity; }`.

---

## 7. 에러 처리 & 견고성

프로토콜 §5 에러 코드 대응:

| `code` | 연결 | 클라 처리 |
|--------|------|-----------|
| `not_identified` | 유지 | 로그. 핸드셰이크 상태머신이 identify 우선을 보장하므로 정상 흐름에선 발생하지 않아야 함 |
| `unknown_type` | 유지 | 로그 |
| `unknown_command` | 유지 | 로그 (클라도 화이트리스트로 사전 차단) |
| `bad_frame` | **종료** | 워커가 끊김 감지 → 재연결 로직 |
| `not_authorized` | 종료 | 로그 + 재연결(또는 상위에 알림) |

- 서버 응답 지연: 위젯이 `OnConnectionStateChanged`/전송 대기 상태를 받아 NPC "생각 중..." 연출.
- 잘못된 JSON 포맷 수신: 해당 프레임 스킵 + 로그(연결은 유지, 프레이밍 자체 오류만 종료 대상).
- 모든 `action_request`에는 성공/실패와 무관하게 15초 내 반드시 회신.

---

## 8. NPC & 대화 UI

- `AHermesNPCCharacter : ACharacter` — `UHermesInventoryComponent` 소유, follow 상태 보유.
- `AHermesNPCAIController : AAIController` — NavMesh 기반 `move_to`/`follow_player` 이동 담당.
- 플레이어 `Interact` 입력 → NPC가 `UHermesDialogueWidget` 오픈 + 서브시스템 델리게이트 구독.
- `UHermesDialogueWidget : UUserWidget` (C++ 상속·제어):
  - 텍스트 입력 → `Subsystem->SendChat(text)`.
  - `OnChatResponse` 수신 → 대사창에 `text` 표시.
  - 송신~응답 사이 대기 연출("생각 중...").
- 액션 결과는 NPC 월드 동작(이동/인벤토리 변화)으로 즉시 반영. `chat_response.actions` 요약은 정보용 로그.
- 레벨: NavMesh(`NavMeshBoundsVolume`)가 깔린 테스트 레벨 1개 + 플레이어 폰 + NPC 1기 배치.

---

## 9. 검증 & 테스트 전략

- **단위(자동화 테스트, TDD 우선)**: `FHermesFrameCodec` 인코딩/디코딩, `FFrameAccumulator`(부분 프레임·연속 프레임·경계값 0/1MiB), 디스패처 화이트리스트 라우팅.
- **통합(수동 체크리스트)**:
  1. 서버의 `scripts/ue5_test_client.py`로 프레이밍이 맞는지 먼저 확인.
  2. 실제 `192.168.0.111:8770` 접속: 연결 → `identify` → `identified` 수신.
  3. `chat` 송신 → `chat_response.text` 렌더 확인.
  4. `action_request`(4종) 왕복 → 동일 `id`로 `action_result` 회신, 월드 반영 확인.
  5. 연결 강제 종료 → 자동 재연결 + 동일 `player_id` 재-identify → 대화 맥락 유지 확인.
  6. `ping`/`pong` keepalive 확인.

---

## 10. 구현 순서 (개요)

1. UE5.8 C++ 프로젝트 스캐폴딩 + Build.cs 의존성 설정.
2. `FHermesFrameCodec` + `FFrameAccumulator` (+ 자동화 테스트, TDD).
3. `FHermesSocketWorker`(FRunnable) + 재연결/백오프.
4. `UHermesConnectionSubsystem` — 워커 수명, 큐 소비, 핸드셰이크, ping/pong, 에러 처리.
5. `UHermesActionDispatcher` + `IHermesActionHandler` + 타임아웃 폴백 (+ 라우팅 테스트).
6. 핸들러 4종 + `UHermesInventoryComponent`/`UHermesItem` + NPC/AIController.
7. `UHermesDialogueWidget` + NPC 상호작용 + 테스트 레벨(NavMesh).
8. 서버 연동 테스트(체크리스트) 수행.

> 상세 단계별 구현 계획은 이 스펙을 근거로 별도 구현 계획 문서(writing-plans)에서 작성한다.
