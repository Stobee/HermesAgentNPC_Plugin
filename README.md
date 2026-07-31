# 🤖 Hermes Agent NPC - UE5 Plugin

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-blue.svg?logo=unrealengine)](https://www.unrealengine.com/)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Plugin Status](https://img.shields.io/badge/Plugin-Content%20Included-brightgreen.svg)]()
[![Automation Tests](https://img.shields.io/badge/Tests-24%2F24%20PASS-success.svg)]()

언리얼 엔진 5.8(Unreal Engine 5.8) C++ 기반의 **Hermes AI Agent NPC 독립 플러그인**입니다.  
외부 Hermes AI 백엔드 서버와 TCP 소켓 통신을 통해 유저와 자연어 대화를 나누고, LLM의 지시에 따라 인게임 액션(이동, 추적, 인벤토리 관리, 아이템 거래)을 비동기로 안전하게 수행합니다.

어느 UE5 프로젝트에나 `Plugins/` 폴더로 복사하여 **즉시 드래그 앤 드롭으로 장착 가능한 완제품 플러그인 패키지**입니다.

---

## 🌟 핵심 기능 (Key Features)

- 🔒 **안전한 TCP 프레이밍:** 4바이트 Big-Endian Length Prefix (최대 1MB 바디) 바이트 스트림 파싱.
- 🔐 **TLS 전송 (`FHermesTlsTransport`):** OpenSSL 기반 TLS 1.2+ 위에서 통신하며 SNI, 서버 공개키(SPKI) 핀 고정, 사설 CA 검증을 지원합니다. **평문 폴백이 없습니다** — TLS 실패는 연결 실패로 처리되며 조용히 다운그레이드되지 않습니다.
- ⚡ **비동기 소켓 워커 (`FHermesSocketWorker`):** `FRunnable` 기반의 독립 스레드로 구동되며 조각 Sleep 대기 기반 지수 백오프(Exponential Backoff) 및 DNS 해석(`GetAddressInfo`) 지원.
- ⚙️ **전역 프로젝트 설정 (`UHermesSettings`):** 에디터 Project Settings 및 `Config/DefaultGame.ini`를 통한 서버 호스트/포트/타임아웃 설정 및 커맨드라인 오버라이드 지원.
- 🛡️ **입력 강건성 및 리소스 방어:** 파라미터 수량/좌표 하드 바운드 검증, 토큰 버킷 레이트 리미터, 큐 상한 및 틱 소비 예산 제어.
- 💾 **서버 발급 신원 세션:** 클라이언트는 자기 신원을 주장하지 않습니다. 최초 접속 시 서버가 `player_id`와 `session_token`을 발급하고, `SaveGame`(`HermesPlayer.sav`)에 난독화해 보관하여 재접속 시 이전 대화 맥락을 유지합니다.
- 🎯 **화이트리스트 액션 디스패처 (`UHermesActionDispatcher`):** 허용된 4종의 액션 지시만 라우팅하며 15초 타임아웃 회신 보장.
- 📦 **완제품 블루프린트 내장 (`CanContainContent: true`):** 바로 레벨에 배치할 수 있는 `BP_HermesNPC` 및 바인딩 완료된 UMG 대화 위젯(`WBP_HermesDialogue`) 포함.

---

## 📁 플러그인 구조 (Plugin Architecture)

```text
Plugins/
└── HermesAgentNPC/
    ├── HermesAgentNPC.uplugin              <-- 플러그인 매니페스트 ("CanContainContent": true)
    ├── Source/
    │   └── HermesAgentNPC/                 <-- C++ 핵심 모듈 전체
    │       ├── Actions/                    <-- 디스패처, 핸들러 4종, 파라미터 검증, 레이트 리밋
    │       ├── Connection/                 <-- 연결 서브시스템, 자격 증명, liveness, 발화 추적
    │       ├── Inventory/                  <-- 인벤토리 컴포넌트 & 오버플로 포화 처리
    │       ├── NPC/                        <-- NPC 캐릭터 & AIController
    │       ├── Protocol/                   <-- 소켓 프레이밍 코덱 & 메시지 JSON (v2)
    │       ├── Settings/                   <-- UDeveloperSettings 설정 클래스 (UHermesSettings)
    │       ├── Transport/                  <-- 소켓 워커, 평문/TLS 전송, TLS 검증 정책, DNS 해석
    │       └── UI/                         <-- UMG 대화 위젯 C++
    └── Content/                            <-- 내장 완제품 블루프린트 에셋
        ├── NPC/
        │   └── BP_HermesNPC.uasset          <-- 완제품 NPC 캐릭터 블루프린트
        └── Widgets/
            └── WBP_HermesDialogue.uasset    <-- UMG 대화 위젯 블루프린트
```

---

## 🚀 빠른 시작 및 사용 방법 (Quick Start)

### 1. 플러그인 이식
적용할 언리얼 엔진 5.8 프로젝트의 `Plugins/` 디렉토리에 이 플러그인 폴더(`HermesAgentNPC/`)를 복사합니다.

### 2. `.uproject` 플러그인 활성화
프로젝트의 `.uproject` 파일에 플러그인이 활성화되어 있는지 확인합니다.
```json
"Plugins": [
	{
		"Name": "HermesAgentNPC",
		"Enabled": true
	}
]
```

### 3. 레벨 배치 및 상호작용
1. 언리얼 에디터의 콘텐츠 브라우저 설정에서 **"Show Plugin Content"**를 체크합니다.
2. `HermesAgentNPC Content/NPC/BP_HermesNPC`를 원하는 레벨 위치에 드래그하여 배치합니다.
3. 레벨 바닥에 `NavMeshBoundsVolume`을 배치하여 초록색 길찾기 영역을 생성합니다.
4. 플레이어 캐릭터 키 입력(예: `E` 키) 시 NPC의 **`Interact()`** 블루프린트 노드를 호출하면 대화창이 즉시 구동됩니다.

---

## ⚙️ 서버 설정 (Server Configuration)

서버 주소는 플러그인 내부 소스에 하드코딩되지 않으며, **도입 프로젝트의 설정**에서 제어합니다.

> **이 설정은 게임 개발자가 빌드 시점에 채웁니다.** `UHermesSettings`는 `UDeveloperSettings`이며 값은 `Config/DefaultGame.ini`에 저장되어 패키징에 포함됩니다. 최종 플레이어에게 노출되는 런타임 옵션이 아닙니다.

### 1. 에디터에서 설정
**Edit > Project Settings > Plugins > Hermes Agent NPC**

| 항목 | 기본값 | 설명 |
| :--- | :--- | :--- |
| `Host` | `127.0.0.1` | 백엔드 호스트명 또는 IP (도메인명 사용 가능) |
| `Port` | `8770` | TCP 포트 |
| `Use TLS` | `true` | TLS 사용 여부. **Shipping 빌드에서는 `false`여도 강제로 켜집니다** |
| `Tls Server Name` | (비움) | SNI 및 인증서 호스트명 검증에 쓸 이름. 비우면 `Host`를 그대로 사용 |
| `Tls Pinned Public Key Hashes` | (비움) | 서버 공개키(SPKI) SHA-256 해시의 base64 목록. 하나라도 있으면 핀 검증 |
| `Tls Private Ca Path` | (비움) | 사설 CA 인증서(PEM) 경로. 프로젝트 디렉터리 기준 상대 경로 |
| `Tls Handshake Timeout Seconds` | `10.0` 초 | TLS 핸드셰이크 대기 한계 |
| `Initial Reconnect Delay` | `0.5` 초 | 재연결 초기 지연 시간 (실패마다 2배 증가) |
| `Max Reconnect Delay` | `30.0` 초 | 재연결 백오프 최대 지연 시간 |
| `Action Timeout Seconds` | `15.0` 초 | 액션 수행 결과 회신 타임아웃 |

전체 항목은 `Connection`, `Connection|TLS`, `Connection|Tuning`, `Connection|Liveness`, `Gameplay` 카테고리로 나뉘어 있습니다. 위 표는 도입 시 가장 먼저 보게 되는 것들입니다.

설정값은 프로젝트의 `Config/DefaultGame.ini`에 저장됩니다:
```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=hermes.example.com
Port=8770
bUseTLS=True
+TlsPinnedPublicKeyHashes=YOUR_BASE64_SPKI_HASH_HERE
```

> 배열 설정(`TlsPinnedPublicKeyHashes`)은 ini에서 `+` 접두사로 항목을 추가합니다. 키를 교체하는 동안 구·신 핀을 함께 두려면 줄을 여러 개 씁니다.

### 2. TLS 인증서 설정

**TLS 사용 여부는 서버와 협상하지 않습니다.** 클라이언트 설정만으로 결정되며, 서버가 TLS를 받지 않으면 연결이 실패할 뿐 평문으로 붙지 않습니다.

검증 방식은 서버 인증서의 종류에 따라 셋 중 하나가 자동으로 선택됩니다:

| 서버 인증서 | 채울 설정 | 검증 방식 |
| :--- | :--- | :--- |
| 자체 서명 | `Tls Pinned Public Key Hashes` | 공개키 핀 고정 |
| 사설 CA 발급 | `Tls Private Ca Path` | 해당 CA를 신뢰 저장소에 추가 후 표준 체인+호스트명 검증 |
| 공인 CA 발급 | (둘 다 비움) | 시스템 루트 CA로 표준 체인+호스트명 검증 |

> **핀이 하나라도 있으면 핀 검증이 우선합니다.** 자체 서명을 허용하는 대신 정확히 그 키만 신뢰하므로, 사설 CA보다 좁고 강한 조건이기 때문입니다.

#### 자체 서명 인증서 + 핀 고정 (LAN 서버)

`192.168.x.x` 같은 사설 IP는 공인 인증서를 발급받을 수 없으므로 이 방식을 씁니다. 서버 인증서에서 핀 값을 뽑습니다:

```bash
openssl x509 -in server.crt -pubkey -noout \
  | openssl pkey -pubin -outform der \
  | openssl dgst -sha256 -binary \
  | openssl enc -base64
```

출력된 base64 문자열을 `Tls Pinned Public Key Hashes`에 넣습니다.

> **핀은 인증서가 아니라 공개키(SPKI)에 겁니다.** 인증서를 갱신해도 키쌍만 유지하면 클라이언트를 다시 배포할 필요가 없습니다.

#### IP로 접속하는 경우

`Tls Server Name`이 비어 있으면 `Host` 값이 SNI와 호스트명 검증에 쓰입니다. `Host`가 IP 주소라면 서버 인증서의 SAN에 그 IP가 들어 있어야 하며, 그렇지 않다면 `Tls Server Name`에 인증서상의 이름을 따로 지정합니다.

### 3. 빌드별 서버 전환 (Development/Test 빌드)
실행 인자를 전달하여 재컴파일 없이 연결 서버를 오버라이드할 수 있습니다:
```cmd
YourGame.exe -HermesHost=10.0.0.5 -HermesPort=9000
```
> **Shipping 빌드 보안:** Shipping 빌드에서는 커맨드라인 오버라이드 인자가 무시되어 최종 유저가 임의의 서버로 접속을 변경하는 것을 차단합니다.
>
> **TLS 설정에는 커맨드라인 오버라이드가 아예 없습니다.** 검증 정책을 실행 인자로 낮출 수 있으면 그 자체가 취약점이기 때문입니다.

### 4. 개발 중 평문 사용

서버에 아직 인증서가 없다면 `Use TLS`를 끄고 작업할 수 있습니다. 이 경우 연결할 때마다 경고 로그가 남습니다:

```
LogHermes: Warning: connecting WITHOUT TLS to 127.0.0.1:8770 — development only
```

Shipping 빌드에서는 이 설정이 무시되고 TLS가 강제되며, 무시되었다는 사실이 에러 로그로 남습니다.

> `WITH_SSL`이 없는 플랫폼에서 TLS를 요구하면 **연결을 거부합니다.** 조용히 평문으로 내려가지 않습니다.

### 5. 대상 NPC 지정 (Active NPC)

**이 플러그인은 NPC 한 개체만을 대상으로 합니다.** 프로토콜에 NPC 식별자가 없으므로, 하나의 연결은 언제나 단 하나의 NPC를 조종합니다(`ue5-socket-protocol.md`의 "Scope" 절).

어느 액터를 에이전트가 조종할지는 프로젝트가 정합니다:

| 방법 | 사용 시점 |
| :--- | :--- |
| `Auto Register As Active Npc` 체크 (기본값 `true`) | `BeginPlay`에서 자동으로 활성 NPC가 됩니다. 대화형 NPC가 하나뿐인 일반적인 경우. |
| `BecomeActiveHermesNpc()` 호출 (블루프린트/C++) | 원하는 시점에 직접 지정합니다. 스폰되는 NPC, 챕터별 교체 등. |

> **레벨에 `AHermesNPCCharacter`를 여러 개 배치한다면 하나만 남기고 `Auto Register As Active Npc`를 꺼야 합니다.** 여러 개가 자동 등록되면 마지막에 등록한 액터가 대상이 되고, 이전 액터는 교체 경고와 함께 배선이 해제됩니다:
>
> ```
> [Hermes] 활성 NPC 교체: BP_Blacksmith_C_0 -> BP_Guard_C_1. 이 플러그인은 NPC 한 명만 대상으로 한다.
> ```
>
> 활성 NPC가 파괴되면(`EndPlay`) 배선이 자동으로 해제되어, 죽은 액터로 액션이 향하지 않습니다.

---

## 🔐 보안 모델 (Security Model)

무엇을 막고 무엇을 막지 않는지 명시합니다. **막지 않는 것을 막는다고 적지 않는 것이 이 표의 목적입니다.**

| 위협 | 상태 |
| :--- | :--- |
| `player_id`를 알아낸 제3자의 사칭 | ✅ 차단 — 신원은 서버가 발급하며 `session_token`으로 검증 |
| 경로상 공격자의 도청 | ✅ 차단 — TLS |
| 경로상 공격자의 `action_request` 주입 | ✅ 차단 — TLS + 인증서 검증 |
| 가짜 서버로의 유인 | ✅ 차단 — 공개키 핀 고정 또는 CA 검증 |
| 최종 사용자의 임의 서버 리다이렉트 | ✅ 차단 — Shipping에서 커맨드라인 오버라이드 무시 |
| 설정 실수로 인한 평문 통신 | ✅ 차단 — Shipping에서 TLS 강제, 평문 폴백 없음 |
| 악성 피어의 클라이언트 자원 고갈 | ✅ 차단 — 큐 상한, 틱 예산, 레이트 리밋 |
| 비정상 파라미터로 인한 크래시 | ✅ 차단 — 파라미터 하드 바운드 |
| **기기 소유자의 세션 토큰 추출** | ⚠️ **의도적으로 미해결** |
| 서버측 자원 고갈, 프롬프트 인젝션 | ❌ 서버 구현 책임 |

세션 토큰은 `SaveGame`에 `FAES`로 감싸 보관하지만 **키가 바이너리 안에 있으므로 이것은 암호화가 아니라 난독화입니다.** 막는 것은 "세이브 파일을 그대로 복사해 남에게 넘기는" 수준까지이며, 기기 소유자로부터는 보호되지 않고 그럴 방법도 없습니다. 클라이언트에 심은 비밀은 그 기기 소유자에게 결국 노출되므로 그 이상을 시도하면 obscurity에 불과합니다.

막는 대상은 **"내 `player_id`를 알아낸 다른 사람"**이지 **"내 PC를 뜯는 나 자신"**이 아닙니다. 후자에 대한 진짜 대책은 서버측 단기 토큰과 세션 바인딩이며, 이는 서버 구현의 몫입니다.

---

## 📜 지원 액션 명령 스펙 (Action Command Catalog)

서버에서 `action_request`로 하달되는 화이트리스트 명령 4종 목록입니다.

| Command | Params Payload 예시 | 설명 / 결과 회신 |
| :--- | :--- | :--- |
| `move_to` | `{ "location": { "x": 100.0, "y": 250.0, "z": 0.0 } }` | 지정 월드 좌표로 NPC 이동 (`{ "arrived": true }`) |
| `follow_player` | `{ "enabled": true }` | 150cm 거리 유지하며 플레이어 추적/정지 (`{ "following": true }`) |
| `inventory_manage` | `{ "operation": "list" }` | NPC 인벤토리 조회/드랍 (`{ "items": [...] }`) |
| `item_transfer` | `{ "direction": "give", "item_id": "gold", "quantity": 10 }` | 플레이어와 NPC 간 아이템 거래 (`{ "transferred": 10 }`) |

### 🛡️ 파라미터 안전 검증 (Safety Bounds)
클라이언트는 액션 실행 전 모든 파라미터를 검증하며, 범위를 벗어날 경우 게임 로직에 전달되지 않고 `ok=false` 및 에러 메시지를 즉시 반환합니다.

| 제약 항목 | 하드 바운드 조건 | 위반 시 `error` |
| :--- | :--- | :--- |
| `move_to` 좌표 유한성 | `abs(v) <= MaxWorldCoordinate` (1e7 cm) | `coordinate out of range` |
| `item_transfer.quantity` | 정수 범위 `1 <= q <= MaxItemQuantity` (999,999) | `quantity out of range` |
| `item_transfer.item_id` | 비어있지 않음 & `length <= MaxItemIdLength` (64자) | `invalid item_id` |
| 초당 액션 수 | 토큰 버킷 레이트 리미터 (초당 20회) | `rate limited` |

---

## ⚠️ NPC가 말만 하고 움직이지 않는다면 (작은 모델 주의)

**플러그인을 의심하기 전에 서버의 모델을 먼저 확인하십시오.**

가장 흔한 오연동 증상입니다. NPC가 "이동합니다"라고 대답은 하는데 실제로는
움직이지 않고, 로그에 에러가 하나도 없습니다. 클라이언트만 보면 정상이라
플러그인 버그로 오인하기 쉽습니다.

원인은 대개 **서버 쪽 LLM이 도구를 호출하지 않는 것**입니다. 작은 모델(2B급)은
도구를 쓸 줄 알면서도 `tool_choice=auto`에서 도구 대신 말로 때웁니다. 그러면
`action_request` 프레임이 **애초에 나가지 않으므로** 플러그인은 실행할 것이
없습니다. 아무 잘못도 하지 않은 채 아무 일도 일어나지 않습니다.

### 어느 쪽 문제인지 30초 만에 가르는 법

```powershell
# 스텁 서버로 액션 경로만 따로 돌린다 (LLM 불필요)
.\docs\testing\run-headless-verification.ps1 -Scenario move_to -Seconds 55 -ResetSave `
    -Exec "Hermes.Interact @3, Hermes.Chat @5 move please"
```

여기서 아래가 보이면 **플러그인은 정상**이고 문제는 서버입니다.

```
<< action_request {move_to, location:{500,500,100}}
>> action_result  {started, eta_seconds}
>> action_event   {completed, arrived:true}
```

### 실서버에서 프레임을 눈으로 확인하는 법

플러그인은 송수신 프레임을 `LogHermes` Verbose로 남깁니다
(`session_token`은 `***`로 가려집니다).

```
-LogCmds="LogHermes Verbose"
```

`chat_delta`/`chat_response`만 있고 `action_request`가 하나도 없다면 서버가
도구를 부르지 않은 것입니다.

### 서버 팀에 전달할 내용

Hermes 서버 저장소의 `README.md` "작은 모델 주의사항" 절과
`scripts/measure_tool_call_rate.py`를 보시면 됩니다. 요지는 두 가지입니다.

- 기동 시 능력 게이트(`supports_tools`) 통과는 "템플릿이 툴 호출을 **표현**할 수
  있다"는 뜻이지 "모델이 실제로 **호출**한다"는 뜻이 아닙니다.
- 시스템 프롬프트에 말투·문체 지시가 한 줄이라도 들어가면 작은 모델은 도구를
  건너뜁니다. 실측으로 호출률이 20/20에서 0/12로 떨어졌습니다.

---

## 🧪 빌드 & 자동화 테스트 (Build & Automation Test)

### C++ 프로젝트 컴파일 (Build.bat)
```cmd
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="<YourProject>.uproject" -WaitMutex
```

### Automation Unit Test 무인 실행 (27/27 PASS)
```cmd
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<YourProject>.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

---

## 📖 상세 기술 문서

프로젝트 루트에 포함된 **[HermesAgentNPC_Documentation.html](./HermesAgentNPC_Documentation.html)** 파일을 브라우저로 열면 더욱 자세한 인터페이스 기술 사양 및 아키텍처 다이어그램을 확인하실 수 있습니다.

---

### 📄 License

Distributed under the MIT License.
