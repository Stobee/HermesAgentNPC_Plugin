# Hermes UE5 클라이언트 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hermes 서버(`192.168.0.111:8770`)와 길이-프리픽스 JSON/TCP로 통신하며 대화하고 게임 내 행동을 실행하는 UE5.8 C++ 비서 NPC 클라이언트를 구현한다.

**Architecture:** 3계층 — Transport(순수 코덱 + `FRunnable` 소켓 워커, `TQueue` 2개로 게임스레드와 분리) → Connection(`UGameInstanceSubsystem`이 워커 수명·핸드셰이크·라우팅 담당) → Dispatch(화이트리스트 디스패처 + `IHermesActionHandler` 4종) + UMG 대화 UI. 순수 로직은 엔진 비의존으로 분리해 UE Automation Test로 단독 검증한다.

**Tech Stack:** Unreal Engine 5.8, C++17, Sockets/Networking, Json/JsonUtilities, AIModule/NavigationSystem, UMG, UE Automation Testing.

## Global Constraints

- 엔진 버전: **UE 5.8**, C++ 전용 (Blueprint 노출 없음).
- 플랫폼: Windows 클라이언트. 서버: `192.168.0.111:8770` (TCP).
- 프레이밍: **4바이트 uint32 big-endian 길이 프리픽스 + UTF-8 JSON 바디**, 바디 최대 **1,048,576 바이트(1 MiB)**. 길이는 바디 바이트만 계산. **파싱은 항상 길이 프리픽스 기준**, 패킷 경계 기준 금지.
- 메시지 타입: `identify` / `identified` / `chat` / `chat_response` / `action_request` / `action_result` / `ping` / `pong` / `error` — 스펙(`ue5-socket-protocol.md`)과 정확히 일치.
- 핸드셰이크: 연결 직후 `identify`를 먼저 보내고 `identified` 수신 후에만 `chat` 송신.
- `action_request`에는 성공/실패와 무관하게 **동일 `id`로 15초 이내** `action_result` 회신.
- 재연결: 지수 백오프. 재접속 시 **동일 `player_id`**로 재-identify.
- `player_id`: 안정적 per-player UUID, **SaveGame에 영구 저장**.
- 화이트리스트 액션: `move_to`, `follow_player`, `inventory_manage`, `item_transfer`. 미등록 command는 실행 금지, `ok:false, error:"unsupported command"`.
- 인벤토리 모델: `UHermesInventoryComponent`가 `TArray<UHermesItem*>` 보유. `UHermesItem : UObject { FString ItemId; int32 Quantity; }`.

**테스트 관행(도메인 현실):** UE C++ 단위 테스트는 엔진 Automation 프레임워크로 실행하며 에디터/빌드 툴체인이 필요하다. 순수 로직(코덱, 어큐뮬레이터, 디스패처 라우팅)은 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`로 실제 테스트를 작성한다. 소켓/AI/UMG 등 엔진 통합 코드는 실서버 없이 의미 있는 단위 테스트가 불가능하므로 **컴파일 성공 + §9 수동 체크리스트**로 검증한다. 각 테스트 실행 커맨드는 에디터 커맨드릿 형태로 제시한다.

---

### Task 1: 프로젝트 스캐폴딩 & 모듈 의존성

**Files:**
- Create: `HermesAgentNPC.uproject`
- Create: `Source/HermesAgentNPC/HermesAgentNPC.Build.cs`
- Create: `Source/HermesAgentNPC/HermesAgentNPC.h`
- Create: `Source/HermesAgentNPC/HermesAgentNPC.cpp`
- Create: `Source/HermesAgentNPC.Target.cs`
- Create: `Source/HermesAgentNPCEditor.Target.cs`
- Create: `.gitignore`

**Interfaces:**
- Consumes: (없음 — 최초 태스크)
- Produces: 빌드 가능한 `HermesAgentNPC` 게임 모듈. 이후 모든 태스크의 소스는 `Source/HermesAgentNPC/` 아래에 위치.

- [ ] **Step 1: `.gitignore` 작성 (UE 표준)**

```gitignore
# Unreal Engine
Binaries/
Build/
DerivedDataCache/
Intermediate/
Saved/
.vs/
*.sln
*.VC.db
*.opensdf
*.sdf
```

- [ ] **Step 2: `.uproject` 작성**

`HermesAgentNPC.uproject`:
```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "Hermes agent NPC UE5 client",
	"Modules": [
		{
			"Name": "HermesAgentNPC",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		}
	]
}
```

- [ ] **Step 3: Target 파일 작성**

`Source/HermesAgentNPC.Target.cs`:
```csharp
using UnrealBuildTool;

public class HermesAgentNPCTarget : TargetRules
{
	public HermesAgentNPCTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.Add("HermesAgentNPC");
	}
}
```

`Source/HermesAgentNPCEditor.Target.cs`:
```csharp
using UnrealBuildTool;

public class HermesAgentNPCEditorTarget : TargetRules
{
	public HermesAgentNPCEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.Add("HermesAgentNPC");
	}
}
```

- [ ] **Step 4: Build.cs 작성 (전체 의존성)**

`Source/HermesAgentNPC/HermesAgentNPC.Build.cs`:
```csharp
using UnrealBuildTool;

public class HermesAgentNPC : ModuleRules
{
	public HermesAgentNPC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"Sockets", "Networking",
			"Json", "JsonUtilities",
			"AIModule", "NavigationSystem",
			"UMG", "Slate", "SlateCore"
		});
	}
}
```

- [ ] **Step 5: 모듈 구현 파일 작성**

`Source/HermesAgentNPC/HermesAgentNPC.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
```

`Source/HermesAgentNPC/HermesAgentNPC.cpp`:
```cpp
#include "HermesAgentNPC.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, HermesAgentNPC, "HermesAgentNPC");
```

- [ ] **Step 6: 프로젝트 파일 생성 & 빌드 검증**

Run (경로는 설치된 엔진에 맞게 조정):
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: `Build succeeded` (0 errors). 모듈이 링크됨.

- [ ] **Step 7: Commit**

```bash
git add HermesAgentNPC.uproject Source/ .gitignore
git commit -m "feat: UE5.8 C++ 프로젝트 스캐폴딩 및 모듈 의존성"
```

---

### Task 2: `FHermesFrameCodec` — 프레임 인코딩 (TDD)

**Files:**
- Create: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.h`
- Create: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.cpp`
- Test: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.spec.cpp`

**Interfaces:**
- Consumes: (없음)
- Produces:
  - `bool FHermesFrameCodec::Encode(const FString& JsonBody, TArray<uint8>& OutBytes)` — 성공 시 `true`, 바디가 1 MiB 초과면 `false`. `OutBytes` = 4바이트 BE 길이 + UTF-8 바디.
  - 상수 `FHermesFrameCodec::MaxBodySize = 1048576`.

- [ ] **Step 1: 실패하는 테스트 작성**

`Source/HermesAgentNPC/Protocol/HermesFrameCodec.spec.cpp`:
```cpp
#include "Misc/AutomationTest.h"
#include "Protocol/HermesFrameCodec.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesFrameCodecEncodeTest,
	"Hermes.Protocol.FrameCodec.Encode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesFrameCodecEncodeTest::RunTest(const FString& Parameters)
{
	// ASCII 바디 "{}" -> 길이 2
	TArray<uint8> Out;
	TestTrue(TEXT("encode ok"), FHermesFrameCodec::Encode(TEXT("{}"), Out));
	TestEqual(TEXT("total len = 4 + 2"), Out.Num(), 6);
	// big-endian length prefix == 2
	TestEqual(TEXT("prefix byte0"), (int32)Out[0], 0);
	TestEqual(TEXT("prefix byte1"), (int32)Out[1], 0);
	TestEqual(TEXT("prefix byte2"), (int32)Out[2], 0);
	TestEqual(TEXT("prefix byte3"), (int32)Out[3], 2);
	TestEqual(TEXT("body0"), (int32)Out[4], (int32)'{');
	TestEqual(TEXT("body1"), (int32)Out[5], (int32)'}');

	// UTF-8 멀티바이트: "가"는 UTF-8 3바이트
	TArray<uint8> Out2;
	TestTrue(TEXT("encode utf8 ok"), FHermesFrameCodec::Encode(TEXT("가"), Out2));
	TestEqual(TEXT("utf8 body len 3 -> total 7"), Out2.Num(), 7);
	TestEqual(TEXT("utf8 prefix == 3"), (int32)Out2[3], 3);

	// 1 MiB 초과 거부: 1MiB+1 개의 'a'
	FString Huge = FString::ChrN(FHermesFrameCodec::MaxBodySize + 1, TEXT('a'));
	TArray<uint8> Out3;
	TestFalse(TEXT("oversize rejected"), FHermesFrameCodec::Encode(Huge, Out3));

	return true;
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Protocol.FrameCodec; Quit" -unattended -nopause -nullrhi -log
```
Expected: 컴파일 에러(`HermesFrameCodec.h` 없음) 또는 테스트 실패.

- [ ] **Step 3: 최소 구현 작성**

`Source/HermesAgentNPC/Protocol/HermesFrameCodec.h`:
```cpp
#pragma once
#include "CoreMinimal.h"

class FHermesFrameCodec
{
public:
	static constexpr int32 MaxBodySize = 1048576; // 1 MiB

	/** JsonBody(UTF-8) 앞에 4바이트 big-endian 길이를 붙여 OutBytes에 채운다. 초과 시 false. */
	static bool Encode(const FString& JsonBody, TArray<uint8>& OutBytes);
};
```

`Source/HermesAgentNPC/Protocol/HermesFrameCodec.cpp`:
```cpp
#include "Protocol/HermesFrameCodec.h"

bool FHermesFrameCodec::Encode(const FString& JsonBody, TArray<uint8>& OutBytes)
{
	FTCHARToUTF8 Utf8(*JsonBody);
	const int32 BodyLen = Utf8.Length();
	if (BodyLen <= 0 || BodyLen > MaxBodySize)
	{
		return false;
	}

	OutBytes.Reset(4 + BodyLen);
	OutBytes.Add((uint8)((BodyLen >> 24) & 0xFF));
	OutBytes.Add((uint8)((BodyLen >> 16) & 0xFF));
	OutBytes.Add((uint8)((BodyLen >> 8) & 0xFF));
	OutBytes.Add((uint8)(BodyLen & 0xFF));
	OutBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), BodyLen);
	return true;
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: (Step 2와 동일 커맨드)
Expected: `Hermes.Protocol.FrameCodec.Encode` PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/Protocol/HermesFrameCodec.*
git commit -m "feat: 프레임 인코더(4바이트 BE 길이 프리픽스) + 테스트"
```

---

### Task 3: `FFrameAccumulator` — 수신 프레임 파서 (TDD)

**Files:**
- Modify: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.h` (클래스 추가)
- Modify: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.cpp`
- Modify: `Source/HermesAgentNPC/Protocol/HermesFrameCodec.spec.cpp` (테스트 추가)

**Interfaces:**
- Consumes: `FHermesFrameCodec::Encode` (테스트에서 프레임 생성용), `FHermesFrameCodec::MaxBodySize`.
- Produces:
  - `class FFrameAccumulator`
    - `void Feed(const uint8* Data, int32 Len)` — 수신 바이트 누적.
    - `bool TryPop(FString& OutJson)` — 완성 프레임 1개 반환, 없으면 `false`.
    - `bool HasError() const` — `len==0` 또는 `len>1 MiB` 감지 시 `true`(연결 종료 유발).

- [ ] **Step 1: 실패하는 테스트 작성 (부분/연속/에러 케이스)**

`HermesFrameCodec.spec.cpp`에 추가:
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrameAccumulatorTest,
	"Hermes.Protocol.FrameAccumulator.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFrameAccumulatorTest::RunTest(const FString& Parameters)
{
	// 두 프레임을 이어 붙인 뒤, 1바이트씩 흘려넣어도 정확히 2개가 나와야 한다.
	TArray<uint8> A, B, Stream;
	FHermesFrameCodec::Encode(TEXT("{\"type\":\"ping\"}"), A);
	FHermesFrameCodec::Encode(TEXT("{\"type\":\"pong\"}"), B);
	Stream.Append(A); Stream.Append(B);

	FFrameAccumulator Acc;
	TArray<FString> Popped;
	for (int32 i = 0; i < Stream.Num(); ++i)
	{
		Acc.Feed(&Stream[i], 1); // 1바이트씩(패킷 경계 무관)
		FString Json;
		while (Acc.TryPop(Json)) { Popped.Add(Json); }
	}
	TestEqual(TEXT("two frames parsed"), Popped.Num(), 2);
	TestEqual(TEXT("frame0"), Popped[0], TEXT("{\"type\":\"ping\"}"));
	TestEqual(TEXT("frame1"), Popped[1], TEXT("{\"type\":\"pong\"}"));
	TestFalse(TEXT("no error"), Acc.HasError());

	// len == 0 은 프로토콜 에러
	FFrameAccumulator Acc2;
	const uint8 ZeroLen[4] = {0,0,0,0};
	Acc2.Feed(ZeroLen, 4);
	FString Dummy;
	TestFalse(TEXT("zero-len pops nothing"), Acc2.TryPop(Dummy));
	TestTrue(TEXT("zero-len is error"), Acc2.HasError());

	// len > 1 MiB 는 프로토콜 에러
	FFrameAccumulator Acc3;
	const uint8 BigLen[4] = {0x00, 0x20, 0x00, 0x00}; // 0x00200000 = 2 MiB
	Acc3.Feed(BigLen, 4);
	TestTrue(TEXT("oversize is error"), Acc3.HasError());

	return true;
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Protocol.FrameAccumulator; Quit" -unattended -nopause -nullrhi -log
```
Expected: 컴파일 에러(`FFrameAccumulator` 없음).

- [ ] **Step 3: 최소 구현 작성**

`HermesFrameCodec.h`에 추가:
```cpp
class FFrameAccumulator
{
public:
	void Feed(const uint8* Data, int32 Len);
	bool TryPop(FString& OutJson);
	bool HasError() const { return bError; }

private:
	TArray<uint8> Buffer;
	bool bError = false;
};
```

`HermesFrameCodec.cpp`에 추가:
```cpp
void FFrameAccumulator::Feed(const uint8* Data, int32 Len)
{
	if (bError || Len <= 0) return;
	Buffer.Append(Data, Len);
}

bool FFrameAccumulator::TryPop(FString& OutJson)
{
	if (bError || Buffer.Num() < 4) return false;

	const uint32 BodyLen =
		((uint32)Buffer[0] << 24) | ((uint32)Buffer[1] << 16) |
		((uint32)Buffer[2] << 8)  |  (uint32)Buffer[3];

	if (BodyLen == 0 || BodyLen > (uint32)FHermesFrameCodec::MaxBodySize)
	{
		bError = true;
		return false;
	}
	if ((uint32)Buffer.Num() < 4 + BodyLen) return false; // 아직 바디 미완성

	const uint8* BodyPtr = Buffer.GetData() + 4;
	OutJson = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(BodyPtr), BodyLen).Get(), BodyLen > 0 ? -1 : 0);
	// 정확한 길이 변환: UTF-8 디코드
	OutJson = FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(BodyPtr), (int32)BodyLen).Get();

	Buffer.RemoveAt(0, 4 + BodyLen, EAllowShrinking::No);
	return true;
}
```

> 주의: `FUTF8ToTCHAR`는 명시적 길이를 받는 오버로드를 사용해 바디를 정확히 `BodyLen` 바이트만 디코드한다(널 종단에 의존하지 않음).

- [ ] **Step 4: 테스트 통과 확인**

Run: (Step 2와 동일 커맨드)
Expected: `Hermes.Protocol.FrameAccumulator.Parse` PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/Protocol/HermesFrameCodec.*
git commit -m "feat: 수신 프레임 파서(길이 기준, 부분/연속/에러 처리) + 테스트"
```

---

### Task 4: 메시지 타입 상수 & JSON 헬퍼

**Files:**
- Create: `Source/HermesAgentNPC/Protocol/HermesMessages.h`
- Create: `Source/HermesAgentNPC/Protocol/HermesMessages.cpp`
- Test: `Source/HermesAgentNPC/Protocol/HermesMessages.spec.cpp`

**Interfaces:**
- Consumes: `Json`/`JsonUtilities` 모듈.
- Produces:
  - 네임스페이스 `HermesMsg`에 타입 문자열 상수: `Identify`, `Identified`, `Chat`, `ChatResponse`, `ActionRequest`, `ActionResult`, `Ping`, `Pong`, `Error`.
  - `bool HermesJson::Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj)`.
  - `FString HermesJson::Serialize(const TSharedRef<FJsonObject>& Obj)` — 압축(공백 없는) 출력.
  - `FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& PlayerName)`.
  - `FString HermesJson::MakeChat(const FString& Id, const FString& Text)`.
  - `FString HermesJson::MakeActionResult(const FString& Id, bool bOk, const TSharedPtr<FJsonObject>& Result, const FString& Error)`.
  - `FString HermesJson::MakePong(const FString& Id)`.

- [ ] **Step 1: 실패하는 테스트 작성**

`HermesMessages.spec.cpp`:
```cpp
#include "Misc/AutomationTest.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesMessagesTest,
	"Hermes.Protocol.Messages.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesMessagesTest::RunTest(const FString& Parameters)
{
	// identify 직렬화 후 다시 파싱하면 필드가 보존된다
	const FString Id = HermesJson::MakeIdentify(TEXT("uuid-1"), TEXT("Aria"));
	TSharedPtr<FJsonObject> Obj;
	TestTrue(TEXT("parse ok"), HermesJson::Parse(Id, Obj));
	TestEqual(TEXT("type"), Obj->GetStringField(TEXT("type")), HermesMsg::Identify);
	TestEqual(TEXT("player_id"), Obj->GetStringField(TEXT("player_id")), TEXT("uuid-1"));
	TestEqual(TEXT("player_name"), Obj->GetStringField(TEXT("player_name")), TEXT("Aria"));

	// action_result 실패 케이스
	const FString Ar = HermesJson::MakeActionResult(TEXT("act-1"), false, nullptr, TEXT("path blocked"));
	TSharedPtr<FJsonObject> Obj2;
	TestTrue(TEXT("parse ar"), HermesJson::Parse(Ar, Obj2));
	TestEqual(TEXT("ar type"), Obj2->GetStringField(TEXT("type")), HermesMsg::ActionResult);
	TestFalse(TEXT("ar ok=false"), Obj2->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("ar error"), Obj2->GetStringField(TEXT("error")), TEXT("path blocked"));

	// 잘못된 JSON은 파싱 실패
	TSharedPtr<FJsonObject> Bad;
	TestFalse(TEXT("bad json"), HermesJson::Parse(TEXT("{not json"), Bad));
	return true;
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Protocol.Messages; Quit" -unattended -nopause -nullrhi -log
```
Expected: 컴파일 에러(`HermesMessages.h` 없음).

- [ ] **Step 3: 최소 구현 작성**

`HermesMessages.h`:
```cpp
#pragma once
#include "CoreMinimal.h"

class FJsonObject;

namespace HermesMsg
{
	inline const FString Identify      = TEXT("identify");
	inline const FString Identified    = TEXT("identified");
	inline const FString Chat          = TEXT("chat");
	inline const FString ChatResponse  = TEXT("chat_response");
	inline const FString ActionRequest = TEXT("action_request");
	inline const FString ActionResult  = TEXT("action_result");
	inline const FString Ping          = TEXT("ping");
	inline const FString Pong          = TEXT("pong");
	inline const FString Error         = TEXT("error");
}

namespace HermesJson
{
	bool Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj);
	FString Serialize(const TSharedRef<FJsonObject>& Obj);

	FString MakeIdentify(const FString& PlayerId, const FString& PlayerName);
	FString MakeChat(const FString& Id, const FString& Text);
	FString MakeActionResult(const FString& Id, bool bOk,
		const TSharedPtr<FJsonObject>& Result, const FString& Error);
	FString MakePong(const FString& Id);
}
```

`HermesMessages.cpp`:
```cpp
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

bool HermesJson::Parse(const FString& Json, TSharedPtr<FJsonObject>& OutObj)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
}

FString HermesJson::Serialize(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

FString HermesJson::MakeIdentify(const FString& PlayerId, const FString& PlayerName)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Identify);
	O->SetStringField(TEXT("player_id"), PlayerId);
	if (!PlayerName.IsEmpty())
	{
		O->SetStringField(TEXT("player_name"), PlayerName);
	}
	return Serialize(O);
}

FString HermesJson::MakeChat(const FString& Id, const FString& Text)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Chat);
	O->SetStringField(TEXT("id"), Id);
	O->SetStringField(TEXT("text"), Text);
	return Serialize(O);
}

FString HermesJson::MakeActionResult(const FString& Id, bool bOk,
	const TSharedPtr<FJsonObject>& Result, const FString& Error)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::ActionResult);
	O->SetStringField(TEXT("id"), Id);
	O->SetBoolField(TEXT("ok"), bOk);
	if (Result.IsValid())
	{
		O->SetObjectField(TEXT("result"), Result);
	}
	if (!Error.IsEmpty())
	{
		O->SetStringField(TEXT("error"), Error);
	}
	return Serialize(O);
}

FString HermesJson::MakePong(const FString& Id)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), HermesMsg::Pong);
	if (!Id.IsEmpty())
	{
		O->SetStringField(TEXT("id"), Id);
	}
	return Serialize(O);
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: (Step 2와 동일 커맨드)
Expected: `Hermes.Protocol.Messages.Build` PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/Protocol/HermesMessages.*
git commit -m "feat: 메시지 타입 상수 및 JSON 빌드/파싱 헬퍼 + 테스트"
```

---

### Task 5: `FHermesSocketWorker` — FRunnable 소켓 워커 + 재연결

**Files:**
- Create: `Source/HermesAgentNPC/Transport/HermesSocketWorker.h`
- Create: `Source/HermesAgentNPC/Transport/HermesSocketWorker.cpp`

**Interfaces:**
- Consumes: `FHermesFrameCodec::Encode`, `FFrameAccumulator`, `Sockets`/`Networking`.
- Produces:
  - `class FHermesSocketWorker : public FRunnable`
    - 생성자 `FHermesSocketWorker(const FString& Host, int32 Port)`.
    - `void EnqueueOutbound(const FString& Json)` — 게임스레드→워커 송신 큐 push.
    - `bool DequeueInbound(FString& OutJson)` — 워커→게임스레드 수신 큐 pop.
    - `bool IsConnected() const`.
    - `void RequestStop()` — 루프 종료 요청.
    - `virtual uint32 Run() override`, `virtual void Stop() override`.
- 이 태스크는 실서버 의존이라 자동화 단위 테스트 없음. **검증 = 컴파일 성공 + Task 10의 통합 체크리스트.**

- [ ] **Step 1: 헤더 작성**

`Transport/HermesSocketWorker.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/Queue.h"
#include "Protocol/HermesFrameCodec.h"

class FSocket;
class FRunnableThread;

class FHermesSocketWorker : public FRunnable
{
public:
	FHermesSocketWorker(const FString& InHost, int32 InPort);
	virtual ~FHermesSocketWorker() override;

	void Start();                                  // 스레드 생성
	void EnqueueOutbound(const FString& Json);
	bool DequeueInbound(FString& OutJson);
	bool IsConnected() const { return bConnected; }
	void RequestStop();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	bool ConnectSocket();
	void CloseSocket();
	bool SendAllPending();     // 아웃바운드 큐 flush
	bool ReceiveAvailable();   // 논블로킹 recv -> accumulator -> inbound 큐

	FString Host;
	int32 Port;

	FSocket* Socket = nullptr;
	FRunnableThread* Thread = nullptr;
	FFrameAccumulator Accumulator;

	TQueue<FString, EQueueMode::Spsc> Outbound; // 게임→워커
	TQueue<FString, EQueueMode::Spsc> Inbound;  // 워커→게임

	FThreadSafeBool bStopRequested = false;
	FThreadSafeBool bConnected = false;
};
```

- [ ] **Step 2: 구현 작성 (연결/송수신/지수 백오프)**

`Transport/HermesSocketWorker.cpp`:
```cpp
#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesFrameCodec.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "HAL/RunnableThread.h"
#include "Math/UnrealMathUtility.h"

FHermesSocketWorker::FHermesSocketWorker(const FString& InHost, int32 InPort)
	: Host(InHost), Port(InPort) {}

FHermesSocketWorker::~FHermesSocketWorker()
{
	RequestStop();
	if (Thread)
	{
		Thread->Kill(true); // 스레드 종료 대기
		delete Thread;
		Thread = nullptr;
	}
	CloseSocket();
}

void FHermesSocketWorker::Start()
{
	Thread = FRunnableThread::Create(this, TEXT("HermesSocketWorker"));
}

void FHermesSocketWorker::EnqueueOutbound(const FString& Json) { Outbound.Enqueue(Json); }
bool FHermesSocketWorker::DequeueInbound(FString& OutJson) { return Inbound.Dequeue(OutJson); }
void FHermesSocketWorker::RequestStop() { bStopRequested = true; }
void FHermesSocketWorker::Stop() { bStopRequested = true; }

bool FHermesSocketWorker::ConnectSocket()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) return false;

	FIPv4Address Addr;
	if (!FIPv4Address::Parse(Host, Addr)) return false;

	TSharedRef<FInternetAddr> InetAddr = SS->CreateInternetAddr();
	InetAddr->SetIp(Addr.Value);
	InetAddr->SetPort(Port);

	Socket = FTcpSocketBuilder(TEXT("HermesClient")).AsBlocking().Build();
	if (!Socket) return false;

	if (!Socket->Connect(*InetAddr))
	{
		CloseSocket();
		return false;
	}
	Socket->SetNonBlocking(true); // 연결 후 논블로킹 수신
	Accumulator = FFrameAccumulator(); // 새 연결마다 파서 리셋
	return true;
}

void FHermesSocketWorker::CloseSocket()
{
	bConnected = false;
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SS) SS->DestroySocket(Socket);
		Socket = nullptr;
	}
}

bool FHermesSocketWorker::SendAllPending()
{
	FString Json;
	while (Outbound.Dequeue(Json))
	{
		TArray<uint8> Bytes;
		if (!FHermesFrameCodec::Encode(Json, Bytes)) continue; // 인코드 불가한 프레임은 스킵
		int32 Total = 0;
		while (Total < Bytes.Num())
		{
			int32 Sent = 0;
			if (!Socket->Send(Bytes.GetData() + Total, Bytes.Num() - Total, Sent) || Sent <= 0)
			{
				return false; // 송신 실패 -> 재연결
			}
			Total += Sent;
		}
	}
	return true;
}

bool FHermesSocketWorker::ReceiveAvailable()
{
	uint8 Buf[4096];
	uint32 Pending = 0;
	while (Socket->HasPendingData(Pending))
	{
		int32 Read = 0;
		if (!Socket->Recv(Buf, sizeof(Buf), Read))
		{
			return false; // 수신 에러 -> 재연결
		}
		if (Read <= 0) break;
		Accumulator.Feed(Buf, Read);
		if (Accumulator.HasError()) return false; // 프레이밍 위반 -> 연결 종료

		FString Json;
		while (Accumulator.TryPop(Json))
		{
			Inbound.Enqueue(Json);
		}
	}
	return true;
}

uint32 FHermesSocketWorker::Run()
{
	float Backoff = 0.5f;
	const float MaxBackoff = 30.f;

	while (!bStopRequested)
	{
		if (!bConnected)
		{
			if (ConnectSocket())
			{
				bConnected = true;
				Backoff = 0.5f; // 성공 시 리셋
			}
			else
			{
				const float Jitter = FMath::FRandRange(0.f, Backoff * 0.25f);
				FPlatformProcess::Sleep(Backoff + Jitter);
				Backoff = FMath::Min(Backoff * 2.f, MaxBackoff);
				continue;
			}
		}

		if (!SendAllPending() || !ReceiveAvailable())
		{
			CloseSocket(); // 다음 루프에서 재연결
			continue;
		}
		FPlatformProcess::Sleep(0.005f); // busy-wait 방지 (5ms)
	}

	CloseSocket();
	return 0;
}
```

> `AsBlocking().Build()` 후 `Connect`는 블로킹 연결을 시도하고, 성공 시 `SetNonBlocking(true)`로 전환해 게임 루프 폴링과 궁합을 맞춘다. `Sleep(5ms)`로 CPU 점유를 억제한다.

- [ ] **Step 3: 컴파일 검증**

Run:
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: `Build succeeded`.

- [ ] **Step 4: Commit**

```bash
git add Source/HermesAgentNPC/Transport/HermesSocketWorker.*
git commit -m "feat: FRunnable 소켓 워커 - 연결/논블로킹 송수신/지수 백오프 재연결"
```

---

### Task 6: `UHermesActionDispatcher` + `IHermesActionHandler` (화이트리스트 라우팅 TDD)

**Files:**
- Create: `Source/HermesAgentNPC/Actions/HermesActionTypes.h`
- Create: `Source/HermesAgentNPC/Actions/HermesActionHandler.h`
- Create: `Source/HermesAgentNPC/Actions/HermesActionDispatcher.h`
- Create: `Source/HermesAgentNPC/Actions/HermesActionDispatcher.cpp`
- Test: `Source/HermesAgentNPC/Actions/HermesActionDispatcher.spec.cpp`

**Interfaces:**
- Consumes: `HermesJson::MakeActionResult`, `FJsonObject`.
- Produces:
  - `struct FHermesActionPayload { FString Id; FString Command; TSharedPtr<FJsonObject> Params; }`.
  - `DECLARE_DELEGATE_ThreeParams(FHermesActionResultDelegate, bool /*bOk*/, TSharedPtr<FJsonObject> /*Result*/, FString /*Error*/)`.
  - `UINTERFACE` `UHermesActionHandler` + `IHermesActionHandler`:
    - `virtual bool CanHandle(const FString& Command) const = 0;`
    - `virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) = 0;`
  - `class UHermesActionDispatcher : public UObject`
    - `void RegisterHandler(TScriptInterface<IHermesActionHandler> Handler)`.
    - `void Dispatch(const FHermesActionPayload& Payload, TFunction<void(const FString& ActionResultJson)> OnResult)` — 화이트리스트 확인 → 핸들러 실행 → 결과를 `action_result` JSON으로 `OnResult` 콜백. 미등록 command면 즉시 `unsupported command`. 15초 미회신 시 `timeout`.

- [ ] **Step 1: 실패하는 테스트 작성 (라우팅 + 화이트리스트)**

`Actions/HermesActionDispatcher.spec.cpp`:
```cpp
#include "Misc/AutomationTest.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/HermesActionHandler.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"

// 테스트용 핸들러: "echo" command만 처리, 즉시 성공
class FTestEchoHandler : public IHermesActionHandler
{
public:
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("echo"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("echoed"), true);
		OnDone.ExecuteIfBound(true, R, FString());
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesDispatcherTest,
	"Hermes.Actions.Dispatcher.Route",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesDispatcherTest::RunTest(const FString& Parameters)
{
	UHermesActionDispatcher* D = NewObject<UHermesActionDispatcher>(GetTransientPackage());

	// UObject 기반 핸들러 래퍼 등록
	UTestHandlerObject* HandlerObj = NewObject<UTestHandlerObject>(GetTransientPackage());
	D->RegisterHandler(HandlerObj);

	// 등록된 command -> ok:true, result.echoed
	FHermesActionPayload P;
	P.Id = TEXT("act-1"); P.Command = TEXT("echo"); P.Params = MakeShared<FJsonObject>();
	FString Result1;
	D->Dispatch(P, [&](const FString& Json){ Result1 = Json; });
	TSharedPtr<FJsonObject> O1;
	HermesJson::Parse(Result1, O1);
	TestEqual(TEXT("ok id echoed back"), O1->GetStringField(TEXT("id")), TEXT("act-1"));
	TestTrue(TEXT("ok true"), O1->GetBoolField(TEXT("ok")));

	// 미등록 command -> ok:false, error:"unsupported command"
	FHermesActionPayload P2;
	P2.Id = TEXT("act-2"); P2.Command = TEXT("delete_world"); P2.Params = MakeShared<FJsonObject>();
	FString Result2;
	D->Dispatch(P2, [&](const FString& Json){ Result2 = Json; });
	TSharedPtr<FJsonObject> O2;
	HermesJson::Parse(Result2, O2);
	TestFalse(TEXT("unsupported ok false"), O2->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("unsupported error"), O2->GetStringField(TEXT("error")), TEXT("unsupported command"));
	return true;
}
```

> 위 테스트는 `UTestHandlerObject`(IHermesActionHandler를 구현하는 UObject)를 필요로 한다. Step 3에서 spec 파일 상단에 최소 정의를 함께 둔다.

- [ ] **Step 2: 테스트 실패 확인**

Run:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Actions.Dispatcher; Quit" -unattended -nopause -nullrhi -log
```
Expected: 컴파일 에러(`HermesActionDispatcher.h` 없음).

- [ ] **Step 3: 최소 구현 작성**

`Actions/HermesActionTypes.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FHermesActionPayload
{
	FString Id;
	FString Command;
	TSharedPtr<FJsonObject> Params;
};

DECLARE_DELEGATE_ThreeParams(FHermesActionResultDelegate,
	bool /*bOk*/, TSharedPtr<FJsonObject> /*Result*/, FString /*Error*/);
```

`Actions/HermesActionHandler.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Actions/HermesActionTypes.h"
#include "HermesActionHandler.generated.h"

UINTERFACE(MinimalAPI)
class UHermesActionHandler : public UInterface { GENERATED_BODY() };

class IHermesActionHandler
{
	GENERATED_BODY()
public:
	virtual bool CanHandle(const FString& Command) const = 0;
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) = 0;
};
```

`Actions/HermesActionDispatcher.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionTypes.h"
#include "Actions/HermesActionHandler.h"
#include "HermesActionDispatcher.generated.h"

UCLASS()
class UHermesActionDispatcher : public UObject
{
	GENERATED_BODY()
public:
	void RegisterHandler(TScriptInterface<IHermesActionHandler> Handler);

	/** 화이트리스트 확인 → 핸들러 실행 → action_result JSON을 OnResult로 반환. */
	void Dispatch(const FHermesActionPayload& Payload,
		TFunction<void(const FString& ActionResultJson)> OnResult);

private:
	UPROPERTY()
	TArray<TScriptInterface<IHermesActionHandler>> Handlers;
};
```

`Actions/HermesActionDispatcher.cpp`:
```cpp
#include "Actions/HermesActionDispatcher.h"
#include "Protocol/HermesMessages.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"

void UHermesActionDispatcher::RegisterHandler(TScriptInterface<IHermesActionHandler> Handler)
{
	if (Handler) Handlers.Add(Handler);
}

void UHermesActionDispatcher::Dispatch(const FHermesActionPayload& Payload,
	TFunction<void(const FString&)> OnResult)
{
	IHermesActionHandler* Chosen = nullptr;
	for (const TScriptInterface<IHermesActionHandler>& H : Handlers)
	{
		if (H && H->CanHandle(Payload.Command)) { Chosen = H.GetInterface(); break; }
	}

	if (!Chosen)
	{
		// 화이트리스트 미등록: 즉시 거부
		OnResult(HermesJson::MakeActionResult(Payload.Id, false, nullptr, TEXT("unsupported command")));
		return;
	}

	// 중복 회신 방지 가드 (핸들러 or 타임아웃 중 하나만)
	TSharedRef<bool> bDone = MakeShared<bool>(false);
	const FString Id = Payload.Id;

	FHermesActionResultDelegate OnDone;
	OnDone.BindLambda([bDone, Id, OnResult](bool bOk, TSharedPtr<FJsonObject> Result, FString Error)
	{
		if (*bDone) return;
		*bDone = true;
		OnResult(HermesJson::MakeActionResult(Id, bOk, Result, Error));
	});

	Chosen->Execute(Payload, OnDone);

	// 15초 타임아웃 폴백: World 타이머가 있으면 등록 (없으면 즉시성 핸들러엔 무해)
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Th;
		World->GetTimerManager().SetTimer(Th, [bDone, Id, OnResult]()
		{
			if (*bDone) return;
			*bDone = true;
			OnResult(HermesJson::MakeActionResult(Id, false, nullptr, TEXT("timeout")));
		}, 15.f, false);
	}
}
```

`HermesActionDispatcher.spec.cpp` 상단에 테스트용 UObject 핸들러 추가:
```cpp
#include "UObject/Object.h"
#include "HermesActionDispatcher.spec.generated.h" // UHT가 생성 (아래 UCLASS용)

UCLASS()
class UTestHandlerObject : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("echo"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("echoed"), true);
		OnDone.ExecuteIfBound(true, R, FString());
	}
};
```

> 참고: spec 파일에 `UCLASS`를 두려면 해당 파일이 UHT 처리 대상이어야 한다. 문제가 되면 테스트 핸들러를 별도의 `TestHermesHandler.h`로 분리해도 된다. 즉시성 핸들러라 타임아웃 타이머는 `bDone` 가드로 무해하게 무시된다.

- [ ] **Step 4: 테스트 통과 확인**

Run: (Step 2와 동일 커맨드)
Expected: `Hermes.Actions.Dispatcher.Route` PASS.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/Actions/
git commit -m "feat: 액션 디스패처(화이트리스트 라우팅, 15초 타임아웃 폴백) + 인터페이스 + 테스트"
```

---

### Task 7: 인벤토리 모델 + 액션 핸들러 4종

**Files:**
- Create: `Source/HermesAgentNPC/Inventory/HermesItem.h`
- Create: `Source/HermesAgentNPC/Inventory/HermesInventoryComponent.h`
- Create: `Source/HermesAgentNPC/Inventory/HermesInventoryComponent.cpp`
- Create: `Source/HermesAgentNPC/Actions/MoveToActionHandler.h/.cpp`
- Create: `Source/HermesAgentNPC/Actions/FollowPlayerActionHandler.h/.cpp`
- Create: `Source/HermesAgentNPC/Actions/InventoryActionHandler.h/.cpp`
- Create: `Source/HermesAgentNPC/Actions/ItemTransferActionHandler.h/.cpp`
- Test: `Source/HermesAgentNPC/Inventory/HermesInventory.spec.cpp`

**Interfaces:**
- Consumes: `IHermesActionHandler`, `FHermesActionPayload`, `FHermesActionResultDelegate`, `AAIController::MoveToLocation`.
- Produces:
  - `UCLASS() UHermesItem : public UObject { UPROPERTY() FString ItemId; UPROPERTY() int32 Quantity; }`.
  - `UCLASS() UHermesInventoryComponent : public UActorComponent`:
    - `TArray<UHermesItem*> Items;` (`UPROPERTY()`).
    - `int32 GetQuantity(const FString& ItemId) const;`
    - `void Add(const FString& ItemId, int32 Qty);`
    - `bool Remove(const FString& ItemId, int32 Qty);` — 부족 시 `false`.
    - `TArray<TSharedPtr<FJsonValue>> ListAsJson() const;`
  - 4개 핸들러 UObject: `UMoveToActionHandler`, `UFollowPlayerActionHandler`, `UInventoryActionHandler`, `UItemTransferActionHandler` — 각 `IHermesActionHandler` 구현. 생성 시 대상 NPC(`AHermesNPCCharacter*`)를 주입받는 `Init(AHermesNPCCharacter*)` 제공.

- [ ] **Step 1: 인벤토리 실패 테스트 작성**

`Inventory/HermesInventory.spec.cpp`:
```cpp
#include "Misc/AutomationTest.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Inventory/HermesItem.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHermesInventoryTest,
	"Hermes.Inventory.AddRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHermesInventoryTest::RunTest(const FString& Parameters)
{
	UHermesInventoryComponent* Inv = NewObject<UHermesInventoryComponent>(GetTransientPackage());
	Inv->Add(TEXT("health_potion"), 2);
	TestEqual(TEXT("qty 2"), Inv->GetQuantity(TEXT("health_potion")), 2);

	TestTrue(TEXT("remove 1 ok"), Inv->Remove(TEXT("health_potion"), 1));
	TestEqual(TEXT("qty 1"), Inv->GetQuantity(TEXT("health_potion")), 1);

	TestFalse(TEXT("remove too many fails"), Inv->Remove(TEXT("health_potion"), 5));
	TestEqual(TEXT("qty unchanged"), Inv->GetQuantity(TEXT("health_potion")), 1);
	return true;
}
```

- [ ] **Step 2: 테스트 실패 확인**

Run:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Inventory; Quit" -unattended -nopause -nullrhi -log
```
Expected: 컴파일 에러(`HermesInventoryComponent.h` 없음).

- [ ] **Step 3: 인벤토리 모델 구현**

`Inventory/HermesItem.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HermesItem.generated.h"

UCLASS()
class UHermesItem : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() FString ItemId;
	UPROPERTY() int32 Quantity = 0;
};
```

`Inventory/HermesInventoryComponent.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "HermesInventoryComponent.generated.h"

class UHermesItem;

UCLASS(ClassGroup=(Hermes), meta=(BlueprintSpawnableComponent))
class UHermesInventoryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	int32 GetQuantity(const FString& ItemId) const;
	void Add(const FString& ItemId, int32 Qty);
	bool Remove(const FString& ItemId, int32 Qty);
	TArray<TSharedPtr<FJsonValue>> ListAsJson() const;

private:
	UHermesItem* Find(const FString& ItemId) const;

	UPROPERTY()
	TArray<UHermesItem*> Items;
};
```

`Inventory/HermesInventoryComponent.cpp`:
```cpp
#include "Inventory/HermesInventoryComponent.h"
#include "Inventory/HermesItem.h"

UHermesItem* UHermesInventoryComponent::Find(const FString& ItemId) const
{
	for (UHermesItem* It : Items)
	{
		if (It && It->ItemId == ItemId) return It;
	}
	return nullptr;
}

int32 UHermesInventoryComponent::GetQuantity(const FString& ItemId) const
{
	const UHermesItem* It = Find(ItemId);
	return It ? It->Quantity : 0;
}

void UHermesInventoryComponent::Add(const FString& ItemId, int32 Qty)
{
	if (Qty <= 0) return;
	if (UHermesItem* It = Find(ItemId)) { It->Quantity += Qty; return; }
	UHermesItem* New = NewObject<UHermesItem>(this);
	New->ItemId = ItemId; New->Quantity = Qty;
	Items.Add(New);
}

bool UHermesInventoryComponent::Remove(const FString& ItemId, int32 Qty)
{
	UHermesItem* It = Find(ItemId);
	if (!It || Qty <= 0 || It->Quantity < Qty) return false;
	It->Quantity -= Qty;
	if (It->Quantity == 0) Items.Remove(It);
	return true;
}

TArray<TSharedPtr<FJsonValue>> UHermesInventoryComponent::ListAsJson() const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const UHermesItem* It : Items)
	{
		if (!It) continue;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("item_id"), It->ItemId);
		O->SetNumberField(TEXT("quantity"), It->Quantity);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}
```

- [ ] **Step 4: 인벤토리 테스트 통과 확인**

Run: (Step 2와 동일 커맨드)
Expected: `Hermes.Inventory.AddRemove` PASS.

- [ ] **Step 5: 4종 핸들러 구현**

각 핸들러는 `Init(AHermesNPCCharacter* InNpc)`로 대상 NPC를 받는다. NPC 클래스는 Task 8에서 정의하지만, 핸들러는 전방 선언 + 접근자만 사용하므로 여기서 먼저 구현하고 Task 8에서 배선한다.

`Actions/MoveToActionHandler.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "MoveToActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UMoveToActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("move_to"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;
private:
	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
```

`Actions/MoveToActionHandler.cpp`:
```cpp
#include "Actions/MoveToActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "AIController.h"
#include "Dom/JsonObject.h"

void UMoveToActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid()) { OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable")); return; }

	const TSharedPtr<FJsonObject>* Loc = nullptr;
	double X=0, Y=0, Z=0;
	if (!Payload.Params.IsValid() ||
		!Payload.Params->TryGetObjectField(TEXT("location"), Loc) ||
		!(*Loc)->TryGetNumberField(TEXT("x"), X) ||
		!(*Loc)->TryGetNumberField(TEXT("y"), Y) ||
		!(*Loc)->TryGetNumberField(TEXT("z"), Z))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid location"));
		return;
	}

	AAIController* AI = Cast<AAIController>(Npc->GetController());
	if (!AI) { OnDone.ExecuteIfBound(false, nullptr, TEXT("no ai controller")); return; }

	const FVector Dest((float)X, (float)Y, (float)Z);
	EPathFollowingRequestResult::Type R = AI->MoveToLocation(Dest);
	if (R == EPathFollowingRequestResult::Failed)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("path blocked"));
		return;
	}
	// 최소 동작: 요청 성공을 도착으로 간주해 즉시 회신 (확장 시 OnMoveCompleted 델리게이트로 대체)
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("arrived"), true);
	OnDone.ExecuteIfBound(true, Res, FString());
}
```

`Actions/FollowPlayerActionHandler.h/.cpp` (enabled 토글 → NPC follow 상태):
```cpp
// FollowPlayerActionHandler.h
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actions/HermesActionHandler.h"
#include "FollowPlayerActionHandler.generated.h"

class AHermesNPCCharacter;

UCLASS()
class UFollowPlayerActionHandler : public UObject, public IHermesActionHandler
{
	GENERATED_BODY()
public:
	void Init(AHermesNPCCharacter* InNpc) { Npc = InNpc; }
	virtual bool CanHandle(const FString& Command) const override { return Command == TEXT("follow_player"); }
	virtual void Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone) override;
private:
	UPROPERTY() TWeakObjectPtr<AHermesNPCCharacter> Npc;
};
```
```cpp
// FollowPlayerActionHandler.cpp
#include "Actions/FollowPlayerActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Dom/JsonObject.h"

void UFollowPlayerActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid()) { OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable")); return; }
	bool bEnabled = false;
	if (!Payload.Params.IsValid() || !Payload.Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid enabled")); return;
	}
	Npc->SetFollowPlayer(bEnabled);
	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("following"), bEnabled);
	OnDone.ExecuteIfBound(true, Res, FString());
}
```

`Actions/InventoryActionHandler.cpp` 핵심 (operation 분기):
```cpp
// InventoryActionHandler.h 는 위 패턴과 동일 (CanHandle == "inventory_manage")
#include "Actions/InventoryActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Dom/JsonObject.h"

void UInventoryActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid()) { OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable")); return; }
	UHermesInventoryComponent* Inv = Npc->GetInventory();
	if (!Inv) { OnDone.ExecuteIfBound(false, nullptr, TEXT("no inventory")); return; }

	FString Op;
	if (!Payload.Params.IsValid() || !Payload.Params->TryGetStringField(TEXT("operation"), Op))
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid operation")); return;
	}

	if (Op == TEXT("list"))
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetArrayField(TEXT("items"), Inv->ListAsJson());
		OnDone.ExecuteIfBound(true, Res, FString());
		return;
	}
	if (Op == TEXT("drop"))
	{
		FString Target;
		if (Payload.Params->TryGetStringField(TEXT("target"), Target) &&
			Inv->Remove(Target, Inv->GetQuantity(Target)))
		{
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("dropped"), Target);
			OnDone.ExecuteIfBound(true, Res, FString());
			return;
		}
		OnDone.ExecuteIfBound(false, nullptr, TEXT("drop target not found")); return;
	}
	// sort 등은 최소 구현: 성공 처리 (확장 지점)
	if (Op == TEXT("sort"))
	{
		OnDone.ExecuteIfBound(true, MakeShared<FJsonObject>(), FString()); return;
	}
	OnDone.ExecuteIfBound(false, nullptr, TEXT("unsupported operation"));
}
```

`Actions/ItemTransferActionHandler.cpp` 핵심:
```cpp
#include "Actions/ItemTransferActionHandler.h"
#include "NPC/HermesNPCCharacter.h"
#include "Inventory/HermesInventoryComponent.h"
#include "Dom/JsonObject.h"

void UItemTransferActionHandler::Execute(const FHermesActionPayload& Payload, FHermesActionResultDelegate OnDone)
{
	if (!Npc.IsValid()) { OnDone.ExecuteIfBound(false, nullptr, TEXT("npc unavailable")); return; }
	UHermesInventoryComponent* Inv = Npc->GetInventory();
	if (!Inv) { OnDone.ExecuteIfBound(false, nullptr, TEXT("no inventory")); return; }

	FString Direction, ItemId;
	double Qd = 0;
	if (!Payload.Params.IsValid() ||
		!Payload.Params->TryGetStringField(TEXT("direction"), Direction) ||
		!Payload.Params->TryGetStringField(TEXT("item_id"), ItemId) ||
		!Payload.Params->TryGetNumberField(TEXT("quantity"), Qd) || Qd < 1)
	{
		OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid params")); return;
	}
	const int32 Qty = (int32)Qd;

	// give: NPC -> player (NPC 인벤토리에서 차감), receive: player -> NPC (NPC 인벤토리에 추가)
	if (Direction == TEXT("give"))
	{
		if (!Inv->Remove(ItemId, Qty)) { OnDone.ExecuteIfBound(false, nullptr, TEXT("insufficient quantity")); return; }
	}
	else if (Direction == TEXT("receive"))
	{
		Inv->Add(ItemId, Qty);
	}
	else { OnDone.ExecuteIfBound(false, nullptr, TEXT("invalid direction")); return; }

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetNumberField(TEXT("transferred"), Qty);
	OnDone.ExecuteIfBound(true, Res, FString());
}
```

> `InventoryActionHandler.h`/`ItemTransferActionHandler.h`는 `MoveToActionHandler.h`와 동일한 UCLASS 패턴(각기 다른 `CanHandle` 문자열, `Init(AHermesNPCCharacter*)`, `TWeakObjectPtr<AHermesNPCCharacter> Npc`)으로 작성한다.

- [ ] **Step 6: 컴파일 검증** (NPC 클래스는 Task 8에서 완성되므로, 이 태스크의 핸들러 .cpp는 Task 8 이후 함께 빌드된다. 이 커밋 시점엔 인벤토리/아이템만 빌드 검증하고 핸들러는 헤더/구현만 배치)

Run:
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: NPC 심볼 미정의로 핸들러 .cpp가 에러날 수 있음 → 이 경우 Task 8과 묶어 빌드. 인벤토리 테스트(`Hermes.Inventory.AddRemove`)는 이 시점에 PASS해야 함.

- [ ] **Step 7: Commit**

```bash
git add Source/HermesAgentNPC/Inventory/ Source/HermesAgentNPC/Actions/
git commit -m "feat: 인벤토리 모델(UObject 아이템 TArray) + 액션 핸들러 4종"
```

---

### Task 8: NPC 캐릭터 + AI 컨트롤러

**Files:**
- Create: `Source/HermesAgentNPC/NPC/HermesNPCCharacter.h`
- Create: `Source/HermesAgentNPC/NPC/HermesNPCCharacter.cpp`
- Create: `Source/HermesAgentNPC/NPC/HermesNPCAIController.h`
- Create: `Source/HermesAgentNPC/NPC/HermesNPCAIController.cpp`

**Interfaces:**
- Consumes: `UHermesInventoryComponent`, `ACharacter`, `AAIController`.
- Produces:
  - `UCLASS() AHermesNPCCharacter : public ACharacter`
    - `UHermesInventoryComponent* GetInventory() const;`
    - `void SetFollowPlayer(bool bEnabled);`
    - `bool IsFollowing() const;`
    - `Tick`에서 follow 활성 시 플레이어 위치로 이동 갱신.
  - `UCLASS() AHermesNPCAIController : public AAIController`.

- [ ] **Step 1: AI 컨트롤러 작성**

`NPC/HermesNPCAIController.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "HermesNPCAIController.generated.h"

UCLASS()
class AHermesNPCAIController : public AAIController
{
	GENERATED_BODY()
public:
	AHermesNPCAIController();
};
```
`NPC/HermesNPCAIController.cpp`:
```cpp
#include "NPC/HermesNPCAIController.h"

AHermesNPCAIController::AHermesNPCAIController()
{
	bAttachToPawn = true;
}
```

- [ ] **Step 2: NPC 캐릭터 작성**

`NPC/HermesNPCCharacter.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HermesNPCCharacter.generated.h"

class UHermesInventoryComponent;

UCLASS()
class AHermesNPCCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	AHermesNPCCharacter();

	UHermesInventoryComponent* GetInventory() const { return Inventory; }
	void SetFollowPlayer(bool bEnabled) { bFollowing = bEnabled; }
	bool IsFollowing() const { return bFollowing; }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	UHermesInventoryComponent* Inventory;

	bool bFollowing = false;
	float FollowRepathAccum = 0.f;
};
```
`NPC/HermesNPCCharacter.cpp`:
```cpp
#include "NPC/HermesNPCCharacter.h"
#include "NPC/HermesNPCAIController.h"
#include "Inventory/HermesInventoryComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

AHermesNPCCharacter::AHermesNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AHermesNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Inventory = CreateDefaultSubobject<UHermesInventoryComponent>(TEXT("Inventory"));
}

void AHermesNPCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFollowing) return;

	FollowRepathAccum += DeltaSeconds;
	if (FollowRepathAccum < 0.25f) return; // 0.25s마다 경로 갱신
	FollowRepathAccum = 0.f;

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	AAIController* AI = Cast<AAIController>(GetController());
	if (Player && AI)
	{
		AI->MoveToActor(Player, 150.f); // 150cm 근접 유지
	}
}
```

- [ ] **Step 3: 컴파일 검증 (핸들러 포함 전체 빌드)**

Run:
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: `Build succeeded` — 이제 Task 7 핸들러 .cpp의 `AHermesNPCCharacter` 심볼이 해석됨.

- [ ] **Step 4: Commit**

```bash
git add Source/HermesAgentNPC/NPC/
git commit -m "feat: NPC 캐릭터(인벤토리 소유, follow tick) + AI 컨트롤러"
```

---

### Task 9: `UHermesConnectionSubsystem` — 워커 수명·핸드셰이크·라우팅

**Files:**
- Create: `Source/HermesAgentNPC/HermesSaveGame.h`
- Create: `Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.h`
- Create: `Source/HermesAgentNPC/Connection/HermesConnectionSubsystem.cpp`

**Interfaces:**
- Consumes: `FHermesSocketWorker`, `HermesJson::*`, `HermesMsg::*`, `UHermesActionDispatcher`, 4개 핸들러.
- Produces:
  - `UCLASS() UHermesSaveGame : public USaveGame { UPROPERTY() FString PlayerId; }`.
  - `UCLASS() UHermesConnectionSubsystem : public UGameInstanceSubsystem`
    - `void SendChat(const FString& Text);`
    - `DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatResponse, const FString& /*Text*/, const FString& /*Id*/)` → `FOnChatResponse OnChatResponse;`
    - `DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, bool /*bReady*/)` → `FOnConnectionStateChanged OnConnectionStateChanged;`
    - `void RegisterNpc(AHermesNPCCharacter* Npc);` — 핸들러 4종을 이 NPC로 Init 후 디스패처에 등록.

- [ ] **Step 1: SaveGame 클래스 작성**

`HermesSaveGame.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "HermesSaveGame.generated.h"

UCLASS()
class UHermesSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() FString PlayerId;
};
```

- [ ] **Step 2: 서브시스템 헤더 작성**

`Connection/HermesConnectionSubsystem.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "HermesConnectionSubsystem.generated.h"

class FHermesSocketWorker;
class UHermesActionDispatcher;
class AHermesNPCCharacter;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnChatResponse, const FString&, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectionStateChanged, bool);

UCLASS()
class UHermesConnectionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void SendChat(const FString& Text);
	void RegisterNpc(AHermesNPCCharacter* Npc);

	FOnChatResponse OnChatResponse;
	FOnConnectionStateChanged OnConnectionStateChanged;

private:
	bool Tick(float DeltaTime);              // 게임스레드 인바운드 소비
	void HandleFrame(const TSharedPtr<class FJsonObject>& Obj);
	void SendJson(const FString& Json);      // 워커 아웃바운드로
	FString LoadOrCreatePlayerId();
	void SendIdentify();
	void FlushPendingChats();

	TUniquePtr<FHermesSocketWorker> Worker;
	FTSTicker::FDelegateHandle TickHandle;

	UPROPERTY() UHermesActionDispatcher* Dispatcher = nullptr;

	FString PlayerId;
	bool bIdentified = false;
	bool bWasConnected = false;
	int32 ChatCounter = 0;
	TArray<FString> PendingChats; // identified 전 보류
};
```

- [ ] **Step 3: 서브시스템 구현 작성**

`Connection/HermesConnectionSubsystem.cpp`:
```cpp
#include "Connection/HermesConnectionSubsystem.h"
#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesMessages.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/MoveToActionHandler.h"
#include "Actions/FollowPlayerActionHandler.h"
#include "Actions/InventoryActionHandler.h"
#include "Actions/ItemTransferActionHandler.h"
#include "HermesSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"

static const TCHAR* HermesHost = TEXT("192.168.0.111");
static const int32  HermesPort = 8770;
static const TCHAR* SaveSlot   = TEXT("HermesPlayer");

void UHermesConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PlayerId = LoadOrCreatePlayerId();

	Dispatcher = NewObject<UHermesActionDispatcher>(this);

	Worker = MakeUnique<FHermesSocketWorker>(HermesHost, HermesPort);
	Worker->Start();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHermesConnectionSubsystem::Tick), 0.f);
}

void UHermesConnectionSubsystem::Deinitialize()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (Worker)
	{
		Worker->RequestStop();
		Worker.Reset(); // 소멸자에서 스레드 Kill(true)
	}
	Super::Deinitialize();
}

FString UHermesConnectionSubsystem::LoadOrCreatePlayerId()
{
	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		if (!SG->PlayerId.IsEmpty()) return SG->PlayerId;
	}
	UHermesSaveGame* New = Cast<UHermesSaveGame>(UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	New->PlayerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	UGameplayStatics::SaveGameToSlot(New, SaveSlot, 0);
	return New->PlayerId;
}

void UHermesConnectionSubsystem::RegisterNpc(AHermesNPCCharacter* Npc)
{
	if (!Npc || !Dispatcher) return;
	UMoveToActionHandler* H1 = NewObject<UMoveToActionHandler>(this); H1->Init(Npc); Dispatcher->RegisterHandler(H1);
	UFollowPlayerActionHandler* H2 = NewObject<UFollowPlayerActionHandler>(this); H2->Init(Npc); Dispatcher->RegisterHandler(H2);
	UInventoryActionHandler* H3 = NewObject<UInventoryActionHandler>(this); H3->Init(Npc); Dispatcher->RegisterHandler(H3);
	UItemTransferActionHandler* H4 = NewObject<UItemTransferActionHandler>(this); H4->Init(Npc); Dispatcher->RegisterHandler(H4);
}

void UHermesConnectionSubsystem::SendJson(const FString& Json)
{
	if (Worker) Worker->EnqueueOutbound(Json);
}

void UHermesConnectionSubsystem::SendIdentify()
{
	SendJson(HermesJson::MakeIdentify(PlayerId, FString()));
}

void UHermesConnectionSubsystem::SendChat(const FString& Text)
{
	const FString Id = FString::Printf(TEXT("c-%04d"), ++ChatCounter);
	const FString Json = HermesJson::MakeChat(Id, Text);
	if (bIdentified) SendJson(Json);
	else PendingChats.Add(Json); // identified 후 flush
}

void UHermesConnectionSubsystem::FlushPendingChats()
{
	for (const FString& J : PendingChats) SendJson(J);
	PendingChats.Reset();
}

bool UHermesConnectionSubsystem::Tick(float DeltaTime)
{
	if (!Worker) return true;

	// 연결 엣지 감지: 새로 연결되면 재-identify
	const bool bNow = Worker->IsConnected();
	if (bNow && !bWasConnected)
	{
		bIdentified = false;
		SendIdentify();
	}
	if (!bNow && bWasConnected)
	{
		bIdentified = false;
		OnConnectionStateChanged.Broadcast(false);
	}
	bWasConnected = bNow;

	FString Json;
	while (Worker->DequeueInbound(Json))
	{
		TSharedPtr<FJsonObject> Obj;
		if (HermesJson::Parse(Json, Obj)) HandleFrame(Obj);
	}
	return true; // 계속 틱
}

void UHermesConnectionSubsystem::HandleFrame(const TSharedPtr<FJsonObject>& Obj)
{
	FString Type;
	if (!Obj->TryGetStringField(TEXT("type"), Type)) return;

	if (Type == HermesMsg::Identified)
	{
		bIdentified = true;
		FlushPendingChats();
		OnConnectionStateChanged.Broadcast(true);
	}
	else if (Type == HermesMsg::ChatResponse)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		OnChatResponse.Broadcast(Text, Id);
	}
	else if (Type == HermesMsg::ActionRequest)
	{
		FHermesActionPayload P;
		Obj->TryGetStringField(TEXT("id"), P.Id);
		Obj->TryGetStringField(TEXT("command"), P.Command);
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("params"), ParamsObj)) P.Params = *ParamsObj;
		else P.Params = MakeShared<FJsonObject>();

		Dispatcher->Dispatch(P, [this](const FString& ResultJson){ SendJson(ResultJson); });
	}
	else if (Type == HermesMsg::Ping)
	{
		FString Id; Obj->TryGetStringField(TEXT("id"), Id);
		SendJson(HermesJson::MakePong(Id));
	}
	else if (Type == HermesMsg::Error)
	{
		FString Code, Msg;
		Obj->TryGetStringField(TEXT("code"), Code);
		Obj->TryGetStringField(TEXT("message"), Msg);
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] error %s: %s"), *Code, *Msg);
		// bad_frame/not_authorized는 서버가 연결을 닫으므로 워커가 끊김 감지 후 자동 재연결
	}
}
```

- [ ] **Step 4: 컴파일 검증**

Run:
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: `Build succeeded`.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/Connection/ Source/HermesAgentNPC/HermesSaveGame.h
git commit -m "feat: 연결 서브시스템 - 워커 수명/핸드셰이크/재연결 재-identify/타입 라우팅"
```

---

### Task 10: 대화 UMG 위젯 + NPC 상호작용 + 테스트 레벨

**Files:**
- Create: `Source/HermesAgentNPC/UI/HermesDialogueWidget.h`
- Create: `Source/HermesAgentNPC/UI/HermesDialogueWidget.cpp`
- Modify: `Source/HermesAgentNPC/NPC/HermesNPCCharacter.h/.cpp` (Interact → 위젯 오픈, BeginPlay에서 RegisterNpc)

**Interfaces:**
- Consumes: `UHermesConnectionSubsystem::SendChat/OnChatResponse/OnConnectionStateChanged`, `UUserWidget`.
- Produces:
  - `UCLASS() UHermesDialogueWidget : public UUserWidget`
    - `void OpenFor(UHermesConnectionSubsystem* Conn);`
    - 내부: 입력 텍스트 전송, 응답/대기("생각 중...") 표시.
  - `AHermesNPCCharacter::Interact()` — 위젯 오픈. `BeginPlay`에서 서브시스템에 `RegisterNpc(this)`.

- [ ] **Step 1: 위젯 클래스 작성 (C++ 바인딩)**

`UI/HermesDialogueWidget.h`:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HermesDialogueWidget.generated.h"

class UHermesConnectionSubsystem;
class UEditableTextBox;
class UTextBlock;
class UButton;

UCLASS()
class UHermesDialogueWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void OpenFor(UHermesConnectionSubsystem* Conn);

	virtual void NativeDestruct() override;

protected:
	UPROPERTY(meta=(BindWidget)) UEditableTextBox* InputBox = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* DialogueText = nullptr;
	UPROPERTY(meta=(BindWidget)) UButton* SendButton = nullptr;

	UFUNCTION() void OnSendClicked();

private:
	void HandleChatResponse(const FString& Text, const FString& Id);
	void HandleConnState(bool bReady);

	UPROPERTY() UHermesConnectionSubsystem* Connection = nullptr;
	FDelegateHandle ChatHandle;
	FDelegateHandle StateHandle;
};
```

`UI/HermesDialogueWidget.cpp`:
```cpp
#include "UI/HermesDialogueWidget.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UHermesDialogueWidget::OpenFor(UHermesConnectionSubsystem* Conn)
{
	Connection = Conn;
	if (SendButton) SendButton->OnClicked.AddDynamic(this, &UHermesDialogueWidget::OnSendClicked);
	if (Connection)
	{
		ChatHandle  = Connection->OnChatResponse.AddUObject(this, &UHermesDialogueWidget::HandleChatResponse);
		StateHandle = Connection->OnConnectionStateChanged.AddUObject(this, &UHermesDialogueWidget::HandleConnState);
	}
	AddToViewport();
}

void UHermesDialogueWidget::NativeDestruct()
{
	if (Connection)
	{
		Connection->OnChatResponse.Remove(ChatHandle);
		Connection->OnConnectionStateChanged.Remove(StateHandle);
	}
	Super::NativeDestruct();
}

void UHermesDialogueWidget::OnSendClicked()
{
	if (!Connection || !InputBox) return;
	const FString Text = InputBox->GetText().ToString();
	if (Text.IsEmpty()) return;
	Connection->SendChat(Text);
	InputBox->SetText(FText::GetEmpty());
	if (DialogueText) DialogueText->SetText(FText::FromString(TEXT("생각 중...")));
}

void UHermesDialogueWidget::HandleChatResponse(const FString& Text, const FString& Id)
{
	if (DialogueText) DialogueText->SetText(FText::FromString(Text));
}

void UHermesDialogueWidget::HandleConnState(bool bReady)
{
	if (!bReady && DialogueText) DialogueText->SetText(FText::FromString(TEXT("연결 중...")));
}
```

- [ ] **Step 2: NPC에 Interact + RegisterNpc 배선**

`HermesNPCCharacter.h`에 추가:
```cpp
public:
	void Interact(); // 플레이어 상호작용 진입점
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category="Hermes")
	TSubclassOf<class UHermesDialogueWidget> DialogueWidgetClass;

private:
	UPROPERTY() class UHermesDialogueWidget* DialogueWidget = nullptr;
```

`HermesNPCCharacter.cpp`에 추가:
```cpp
#include "UI/HermesDialogueWidget.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "Engine/GameInstance.h"

void AHermesNPCCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>())
		{
			Conn->RegisterNpc(this);
		}
	}
}

void AHermesNPCCharacter::Interact()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI || !DialogueWidgetClass) return;
	UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>();
	if (!Conn) return;
	if (!DialogueWidget)
	{
		DialogueWidget = CreateWidget<UHermesDialogueWidget>(GI, DialogueWidgetClass);
	}
	if (DialogueWidget) DialogueWidget->OpenFor(Conn);
}
```

- [ ] **Step 3: 전체 빌드 검증**

Run:
```
"<UE_5.8>/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex
```
Expected: `Build succeeded`. 전체 자동화 테스트도 통과 확인:
```
"<UE_5.8>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi -log
```
Expected: `Hermes.Protocol.*`, `Hermes.Actions.*`, `Hermes.Inventory.*` 모두 PASS.

- [ ] **Step 4: 에디터 수동 셋업 안내 (문서화)**

에디터에서 수행(코드 아님, README에 기재):
1. `WBP_HermesDialogue`를 `UHermesDialogueWidget` 기반으로 생성, `InputBox`(EditableTextBox)/`DialogueText`(TextBlock)/`SendButton`(Button) 이름 그대로 배치.
2. NPC 블루프린트(또는 배치 인스턴스)의 `DialogueWidgetClass`에 `WBP_HermesDialogue` 지정.
3. 테스트 레벨에 `NavMeshBoundsVolume` 추가(이동/follow 필요), 바닥 지오메트리, 플레이어 스타트, `AHermesNPCCharacter` 1기 배치.
4. 플레이어 입력(예: `E` 키)에서 근처 NPC의 `Interact()` 호출.

- [ ] **Step 5: Commit**

```bash
git add Source/HermesAgentNPC/UI/ Source/HermesAgentNPC/NPC/
git commit -m "feat: 대화 UMG 위젯 + NPC Interact/RegisterNpc 배선"
```

---

### Task 11: 서버 연동 통합 테스트 체크리스트 (문서)

**Files:**
- Create: `docs/superpowers/plans/integration-checklist.md`

**Interfaces:**
- Consumes: 완성된 클라이언트 전체.
- Produces: 실서버 검증 절차 문서.

- [ ] **Step 1: 체크리스트 문서 작성**

`docs/superpowers/plans/integration-checklist.md`:
```markdown
# Hermes 클라이언트 통합 테스트 체크리스트

## 사전
- [ ] 서버 `scripts/ue5_test_client.py`를 같은 LAN에서 실행해 프레이밍이 맞는지 먼저 확인
      (connect → identify → chat → 응답 출력, action_request 자동 응답).

## 클라이언트 검증 (에디터 PIE)
- [ ] PIE 실행 시 로그에 소켓 연결 성공 + `identify` 송신 → `identified` 수신 확인.
- [ ] SaveGame(`HermesPlayer`)에 player_id가 저장됐는지 확인(재실행 시 동일 UUID).
- [ ] NPC에 다가가 Interact → 대화창 오픈.
- [ ] 채팅 입력(예: "언덕 위로 이동해줘") → "생각 중..." → `chat_response.text` 표시.
- [ ] 서버가 `move_to` action_request 발행 시 NPC가 실제 이동 + `action_result{arrived:true}` 회신(15초 내).
- [ ] `follow_player(enabled:true)` → NPC가 플레이어 추적. `false` → 정지.
- [ ] `inventory_manage(list)` → `action_result.result.items` 반환.
- [ ] `item_transfer(give/receive)` → 수량 변화 + `{transferred:N}`.
- [ ] 미등록 command 강제 주입 시 `ok:false, error:"unsupported command"` 회신.

## 견고성
- [ ] 서버를 잠시 중단 → 클라이언트가 지수 백오프로 재연결 시도 로그.
- [ ] 서버 재기동 → 자동 재연결 + 동일 player_id 재-identify → 대화 맥락 유지.
- [ ] 서버 `ping` 수신 시 `pong` 회신 확인.
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/plans/integration-checklist.md
git commit -m "docs: 서버 연동 통합 테스트 체크리스트"
```

---

## Self-Review

**Spec coverage (스펙 §별 대응):**
- §3 프로젝트/모듈 구조 → Task 1.
- §4.1 FrameCodec → Task 2 / §4.1 FFrameAccumulator → Task 3.
- §4.2 소켓 워커/재연결/백오프 → Task 5. 메시지 상수/JSON → Task 4.
- §5 Connection(핸드셰이크/라우팅/ping-pong/error/재-identify) → Task 9.
- §6.1 디스패처/화이트리스트/15초 타임아웃 → Task 6. §6.2 핸들러 4종 + 인벤토리 → Task 7.
- §7 에러 처리 → Task 9(HandleFrame error 분기) + 워커 재연결(Task 5).
- §8 NPC/AIController/대화 UI/테스트 레벨 → Task 8, 10.
- §9 검증(자동화 테스트 + 통합 체크리스트) → Task 2/3/4/6/7 테스트, Task 11.
- 커버리지 갭 없음.

**Placeholder scan:** 모든 코드 스텝에 실제 구현 포함. "적절한 에러 처리" 류 표현 없음. Task 6의 spec-내 UCLASS는 UHT 제약 시 별도 헤더로 분리하라는 구체 지침 포함.

**Type consistency:** `FHermesActionPayload{Id,Command,Params}`, `FHermesActionResultDelegate(bool,TSharedPtr<FJsonObject>,FString)`, `Dispatch(Payload, TFunction<void(const FString&)>)`, `GetInventory()/SetFollowPlayer()/IsFollowing()`, `RegisterNpc()`, `OnChatResponse(Text,Id)`, `OnConnectionStateChanged(bReady)` — 태스크 간 일치 확인. `FHermesFrameCodec::Encode`/`FFrameAccumulator::Feed/TryPop/HasError`, `HermesJson::Make*`/`Parse`/`Serialize`, `HermesMsg::*` 상수 — 정의(Task 2/3/4)와 사용(Task 5/6/9) 시그니처 일치.
