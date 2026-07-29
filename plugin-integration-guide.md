# UE5 플러그인 팀 전달용 Hermes 서버 사양 및 연동 유의 사항

본 문서는 언리얼 엔진 5(UE5) 게임 클라이언트 플러그인 개발 팀이 **Hermes Llama Server**와 소켓 연결을 맺고 실시간 대화 및 NPC 액션을 연동하기 위해 준수해야 하는 **프로토콜 사양**, **메시지 규격**, **에러 처리 규칙** 및 **통합 유의 사항**을 정리한 가이드입니다.

---

## 📌 1. 네트워크 및 프레이밍 기본 사양

### 1.1 연결 기본 정보
- **기본 포트**: `8770` (TCP)
- **전송 계층**: TLS 1.2+ (개발 환경 전용 평문 모드 지원 `HERMES_USE_TLS=false`)
- **방화벽 설정**: 서버 인바운드 규칙 허용 필요
  ```powershell
  New-NetFirewallRule -DisplayName "Hermes 8770" -Direction Inbound -LocalPort 8770 -Protocol TCP -Action Allow
  ```

### 1.2 4-byte Big-Endian Framing
모든 프레임은 **4바이트 Big-Endian integer 길이 프리픽스 + UTF-8 JSON 바이트**로 구성됩니다.

```
┌───────────────────────────┬──────────────────────────────────────────┐
│  Length (4-byte BE int)   │           JSON Payload (UTF-8)           │
│  e.g., 0x00 0x00 0x00 0x2A│  {"type":"identify","protocol_version":2} │
└───────────────────────────┴──────────────────────────────────────────┘
```

> ⚠️ **유의 사항**:
> - `nc`나 `telnet` 등 텍스트 기반 도구는 4바이트 바이너리 길이 프리픽스를 붙일 수 없어 연동 검증에 사용할 수 없습니다.
> - 서버에 1 MiB (`1,048,576` bytes)를 초과하는 프레임을 보내면 `bad_frame` 에러 수신 후 소켓이 강제 종료됩니다.

---

## 🔑 2. 신원 핸드셰이크 & 세션 수명주기 (P-1, P-2, P-6)

### 2.1 첫 접속 핸드셰이크 (`P-1`, `P-2`)
클라이언트는 소켓 연결 직후 반드시 `identify` 프레임을 보낸 후 `identified` 응답을 받아야 일반 대화(`chat`)를 할 수 있습니다.

- **클라이언트 첫 접속 송신**:
  ```json
  {
    "type": "identify",
    "protocol_version": 2,
    "player_name": "HeroPlayer"
  }
  ```
  > ⚠️ **`protocol_version: 2` 필수 (`P-1`)**: 누락되거나 2가 아닌 경우 `unsupported_version` 에러가 발생하고 연결이 끊어집니다.

- **서버 발급 응답**:
  ```json
  {
    "type": "identified",
    "ok": true,
    "player_id": "p-a1b2c3d4",
    "session_token": "tok-x9y8z7w6",
    "chat_id": "chat-m5n6o7p8"
  }
  ```

### 2.2 자격 증명 재사용 및 재연결 (`P-2`)
- 클라이언트는 서버가 발급한 `player_id`와 `session_token`을 로컬 디바이스에 저장해 두어야 합니다.
- 인터넷 끊김 등으로 재접속할 때는 저장된 두 자격 증명을 모두 포함하여 보내야 기존 세션 및 대화 히스토리를 이어받을 수 있습니다:
  ```json
  {
    "type": "identify",
    "protocol_version": 2,
    "player_id": "p-a1b2c3d4",
    "session_token": "tok-x9y8z7w6"
  }
  ```

### 2.3 세션 탈취 (Session Takeover) 및 재접속 중단 (`P-6`)
- 다른 기기/클라이언트가 동일한 `player_id`로 접속하면, **기존에 연결되어 있던 클라이언트로 `session_taken_over` 에러가 전송된 후 기존 연결이 닫힙니다.**
- **클라이언트 구현 필수 사항 (`P-6`)**: `session_taken_over` 에러를 수신한 클라이언트는 **자동 재연결(Reconnection Loop)을 즉시 중단**해야 합니다. 무한 재연결 시 두 클라이언트 간 무한 세션 강제 축출 전쟁(Eviction War)이 발생합니다.

---

## 💬 3. 대화 및 스트리밍 규격 (P-4, P-7, P-8)

### 3.1 발화 요청 (`chat`)
```json
{
  "type": "chat",
  "id": "c-0001",
  "text": "안녕, 오늘 날씨 어때?"
}
```

### 3.2 델타 수신 및 텍스트 렌더링 (`P-4`)
LLM 추론 과정에서 텍스트 청크가 생성될 때마다 서버가 `chat_delta` 프레임을 푸시합니다.
```json
{"type": "chat_delta", "id": "c-0001", "seq": 0, "text": "오늘"}
{"type": "chat_delta", "id": "c-0001", "seq": 1, "text": " 날씨는"}
{"type": "chat_delta", "id": "c-0001", "seq": 2, "text": " 화창합니다."}
```
- 클라이언트 UI는 `id`별로 텍스트를 순차 누적하여 화면에 실시간 렌더링합니다.

### 3.3 최종 완성 응답 (`chat_response`)
추론이 완료되면 `chat_response`가 전달됩니다. `text`에는 누적분과 동일한 전문 텍스트가 들어있습니다.
```json
{
  "type": "chat_response",
  "id": "c-0001",
  "text": "오늘 날씨는 화창합니다."
}
```

### 3.4 자발 발화 (Unprompted Turn) 처리 (`P-7`)
- NPC가 액션 완료 후 스스로 말을 거는 자발 발화의 경우, `chat_delta` 없이 **`id`가 없는 `chat_response` 프레임 단 1개만 송신**됩니다.
  ```json
  {
    "type": "chat_response",
    "text": "목적지에 도착했습니다!"
  }
  ```
- **클라이언트 구현 필수 사항 (`P-7`)**: `chat_response` 수신 시 `id`가 `null`이거나 없더라도 오류로 처리하지 않고 대화창에 메시지를 정상 출력해야 합니다.

### 3.5 타임아웃 및 핑/퐁 (`P-8`)
- 서버는 30초마다 `ping` 프레임을 보냅니다 (`{"type": "ping", "id": "k-123"}`).
- 클라이언트는 수신 즉시 동일한 `id`로 `pong` 프레임 (`{"type": "pong", "id": "k-123"}`)을 회신해야 90초 침묵 타임아웃에 의한 접속 해제를 막을 수 있습니다.

---

## 🎯 4. 액션 (Tool Call) 규격 및 되먹임 (P-5)

### 4.1 액션 요청 푸시 (`action_request`)
NPC가 대화 도중 게임 내 액션을 수행해야 할 때 서버가 클라이언트에 푸시합니다.
```json
{
  "type": "action_request",
  "id": "act-1001",
  "command": "move_to",
  "params": {
    "location": {"x": 1200.0, "y": 340.0, "z": 90.0}
  }
}
```

### 4.2 액션 접수 회신 (`action_result`) - `P-5` 핵심!
클라이언트는 `action_request` 수신 직후 액션 처리 결과를 회신합니다.

> ⚠️ **`move_to` 등 `LONG_RUNNING` 액션 거짓말 금지 (`P-5`)**:
> - `move_to` 접수 시점에 `{"arrived": true}`를 보내면 아직 이동하지도 않았는데 도착했다고 거짓말하는 것입니다.
> - 접수 시에는 단순히 **이동 시작 접수만 회신**해야 합니다:
>   ```json
>   {
>     "type": "action_result",
>     "id": "act-1001",
>     "ok": true,
>     "result": {"started": true, "eta_seconds": 2.5}
>   }
>   ```

### 4.3 액션 완료 이벤트 푸시 (`action_event`) - `P-5`
실제 캐릭터가 이동을 마치고 목적지에 도착한 시점에 클라이언트가 서버로 완료 이벤트를 전송합니다.
```json
{
  "type": "action_event",
  "id": "act-1001",
  "event": "completed",
  "result": {"arrived": true}
}
```
> 서버는 이 `action_event`를 수신하면 관측 기록(Tool Observation)을 대화 히스토리에 기록하고, **자발 발화**를 유도하여 NPC가 "도착했습니다"라고 말하게 만듭니다.

### 4.4 액션 카탈로그 규격표

| Command | 액션 유형 | Params 규격 | 비고 |
|---|---|---|---|
| `move_to` | `LONG_RUNNING` | `location: {x, y, z}` (-100,000 ~ 100,000) | 이동 완료 시 `action_event` 송신 필수 |
| `follow_player` | `IMMEDIATE` | `enabled: bool` | 플레이어 추적 상태 전환 |
| `inventory_manage` | `IMMEDIATE` | `operation: string` | 인벤토리 열기/닫기/조회 |
| `item_transfer` | `IMMEDIATE` | `item_id: string, quantity: int` (1 ~ 99) | 아이템 주고받기 |

---

## ⚠️ 5. 에러 코드 10종 대응 가이드

서버에서 송신할 수 있는 10가지 에러 코드 규격과 클라이언트 권장 대응 방식입니다:

| 에러 코드 (`code`) | 연결 처리 | 원인 및 클라이언트 대응 가이드 |
|---|---|---|
| `bad_frame` | **소켓 종료** | 1 MiB 초과 프레임 또는 JSON 깨짐. 디버깅 필요 |
| `unsupported_version` | **소켓 종료** | `protocol_version: 2` 미지정. 프로토콜 버전 확인 |
| `not_authorized` | **소켓 종료** | 유효하지 않거나 말소된 토큰. 자격 증명 삭제 후 신규 발급 |
| `session_taken_over` | **소켓 종료** | 다른 기기 접속. **재연결 루프 즉시 중단 (`P-6`)** |
| `not_identified` | 연결 유지 | `identify` 완료 전 `chat` 송신. 핸드셰이크 완료 대기 |
| `unknown_type` | 연결 유지 | 프로토콜에 없는 `type` 지정 |
| `unknown_command` | 연결 유지 | 카탈로그에 없는 액션 명령 참조 |
| `rate_limited` | 연결 유지 (해당 `id` 포함) | 턴 큐 상한 초과. 해당 `chat.id` 발화 취소 처리 |
| `server_busy` | 연결 유지 (해당 `id` 포함) | LLM 백엔드 미가동/초과. 해당 발화 취소 및 사용자 안내 |
| `internal_error` | 연결 유지 | 서버 내부 예외. 대기 중인 UI 턴 취소 처리 |

---

## ✅ 6. 플러그인 개발 팀 통합 체크리스트 (P-1 ~ P-8)

개발 완료 후 아래 체크리스트 8개 항목을 검증해 주시기 바랍니다:

- [ ] **P-1**: `identify` 프레임에 `protocol_version: 2`를 명시하여 송신하는가?
- [ ] **P-2**: `identified` 응답의 `player_id`와 `session_token`을 로컬 저장소에 보관하고 재접속 시 재전송하는가?
- [ ] **P-3**: TLS 1.2+ 전송 연결이 정상 동작하는가?
- [ ] **P-4**: `chat_delta` 텍스트 청크를 `id`별로 올바르게 누적 렌더링하는가?
- [ ] **P-5**: `move_to` 액션 수신 시 `action_result`에는 접수만 알리고, 실제 도착 시점에 `action_event`를 송신하는가?
- [ ] **P-6**: `session_taken_over` 에러 수신 시 자동 재연결을 즉시 중단하는가?
- [ ] **P-7**: `id`가 없는 자발 발화 `chat_response`도 오류 없이 대화창에 렌더링하는가?
- [ ] **P-8**: 서버의 30초 `ping`에 `pong`으로 즉시 회신하고 60초 이상 델타 무반응 시 타임아웃을 처리하는가?
