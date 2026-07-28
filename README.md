# 🤖 Hermes Agent NPC - UE5 Plugin

[![Unreal Engine](https://img.shields.io/badge/Unreal%20Engine-5.8-blue.svg?logo=unrealengine)](https://www.unrealengine.com/)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Plugin Status](https://img.shields.io/badge/Plugin-Content%20Included-brightgreen.svg)]()
[![Automation Tests](https://img.shields.io/badge/Tests-12%2F12%20PASS-success.svg)]()

언리얼 엔진 5.8(Unreal Engine 5.8) C++ 기반의 **Hermes AI Agent NPC 독립 플러그인**입니다.  
외부 Hermes AI 백엔드 서버와 TCP 소켓 통신을 통해 유저와 자연어 대화를 나누고, LLM의 지시에 따라 인게임 액션(이동, 추적, 인벤토리 관리, 아이템 거래)을 비동기로 안전하게 수행합니다.

어느 UE5 프로젝트에나 `Plugins/` 폴더로 복사하여 **즉시 드래그 앤 드롭으로 장착 가능한 완제품 플러그인 패키지**입니다.

---

## 🌟 핵심 기능 (Key Features)

- 🔒 **안전한 TCP 프레이밍:** 4바이트 Big-Endian Length Prefix (최대 1MB 바디) 바이트 스트림 파싱.
- ⚡ **비동기 소켓 워커 (`FHermesSocketWorker`):** `FRunnable` 기반의 독립 스레드로 구동되며 조각 Sleep 대기 기반 지수 백오프(Exponential Backoff) 및 DNS 해석(`GetAddressInfo`) 지원.
- ⚙️ **전역 프로젝트 설정 (`UHermesSettings`):** 에디터 Project Settings 및 `Config/DefaultGame.ini`를 통한 서버 호스트/포트/타임아웃 설정 및 커맨드라인 오버라이드 지원.
- 🛡️ **입력 강건성 및 리소스 방어:** 파라미터 수량/좌표 하드 바운드 검증, 토큰 버킷 레이트 리미터, 큐 상한 및 틱 소비 예산 제어.
- 💾 **영구 플레이어 UUID 세션:** `SaveGame`(`HermesPlayer.sav`)에 UUID를 저장하여 재접속 시에도 AI와의 이전 대화 맥락 유지.
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
    │       ├── Connection/                 <-- 연결 서브시스템, SaveGame, 보류 발화 상한
    │       ├── Inventory/                  <-- 인벤토리 컴포넌트 & 오버플로 포화 처리
    │       ├── NPC/                        <-- NPC 캐릭터 & AIController
    │       ├── Protocol/                   <-- 소켓 프레이밍 코덱 & 메시지 JSON
    │       ├── Settings/                   <-- UDeveloperSettings 설정 클래스 (UHermesSettings)
    │       ├── Transport/                  <-- FRunnable 백그라운드 소켓 워커 & DNS 해석
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

### 1. 에디터 설정
**Edit > Project Settings > Plugins > Hermes Agent NPC**

| 항목 | 기본값 | 설명 |
| :--- | :--- | :--- |
| `Host` | `127.0.0.1` | 백엔드 호스트명 또는 IP (도메인명 사용 가능) |
| `Port` | `8770` | TCP 포트 |
| `Initial Reconnect Delay` | `1.0` 초 | 재연결 초기 지연 시간 |
| `Max Reconnect Delay` | `30.0` 초 | 재연결 백오프 최대 지연 시간 |
| `Action Timeout Seconds` | `15.0` 초 | 액션 수행 결과 회신 타임아웃 |

설정값은 프로젝트의 `Config/DefaultGame.ini`에 저장됩니다:
```ini
[/Script/HermesAgentNPC.HermesSettings]
Host=hermes.example.com
Port=8770
```

### 2. 커맨드라인 오버라이드 (Development/Test 빌드)
실행 인자를 전달하여 재컴파일 없이 연결 서버를 오버라이드할 수 있습니다:
```cmd
YourGame.exe -HermesHost=10.0.0.5 -HermesPort=9000
```
> **Shipping 빌드 보안:** Shipping 빌드에서는 커맨드라인 오버라이드 인자가 무시되어 최종 유저가 임의의 서버로 접속을 변경하는 것을 차단합니다.

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

## 🧪 빌드 & 자동화 테스트 (Build & Automation Test)

### C++ 프로젝트 컴파일 (Build.bat)
```cmd
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="<YourProject>.uproject" -WaitMutex
```

### Automation Unit Test 무인 실행 (12/12 PASS)
```cmd
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<YourProject>.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```

---

## 📖 상세 기술 문서

프로젝트 루트에 포함된 **[HermesAgentNPC_Documentation.html](./HermesAgentNPC_Documentation.html)** 파일을 브라우저로 열면 더욱 자세한 인터페이스 기술 사양 및 아키텍처 다이어그램을 확인하실 수 있습니다.

---

### 📄 License

Distributed under the MIT License.
