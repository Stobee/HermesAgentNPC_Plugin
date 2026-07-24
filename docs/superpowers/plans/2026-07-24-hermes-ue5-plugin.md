# Hermes UE5 Plugin Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Current project C++ source code and core Blueprints (`BP_HermesNPC`, `WBP_HermesDialogue`) will be packaged into a standalone Unreal Engine 5.8 plugin (`Plugins/HermesAgentNPC/`), while removing the ThirdPerson template from the main project.

**Architecture:** The entire `HermesAgentNPC` C++ module (Protocol, Transport, Connection, Actions, Inventory, NPC, UI) and its content assets (`BP_HermesNPC`, `WBP_HermesDialogue`) are placed inside `Plugins/HermesAgentNPC/`. `CanContainContent: true` is configured so that the plugin contains ready-to-use content.

**Tech Stack:** Unreal Engine 5.8, C++17, UE5 Plugin System (`.uplugin`), UE Automation Testing.

## Global Constraints

- Engine: Unreal Engine 5.8.
- Plugin location: `Plugins/HermesAgentNPC/`.
- Manifest setting: `"CanContainContent": true`.
- Exclusions: Remove `ThirdPerson` template from project root `Content/`.
- Automated Tests: All 5 `Hermes.*` automation tests must pass after conversion.

---

### Task 1: 플러그인 폴더 구조 생성 & `.uplugin` 매니페스트 작성

**Files:**
- Create: `Plugins/HermesAgentNPC/HermesAgentNPC.uplugin`

**Interfaces:**
- Consumes: (없음)
- Produces: UE5 에디터 및 UBT가 인식 가능한 플러그인 정의 파일.

- [ ] **Step 1: `.uplugin` 매니페스트 파일 작성**

`Plugins/HermesAgentNPC/HermesAgentNPC.uplugin`:
```json
{
	"FileVersion": 3,
	"Version": 1,
	"VersionName": "1.0",
	"FriendlyName": "Hermes Agent NPC",
	"Description": "Hermes AI Agent Socket Connection & NPC Controller Plugin",
	"Category": "AI",
	"CreatedBy": "Stobee",
	"CanContainContent": true,
	"IsBetaVersion": false,
	"Installed": false,
	"Modules": [
		{
			"Name": "HermesAgentNPC",
			"Type": "Runtime",
			"LoadingPhase": "Default"
		}
	]
}
```

- [ ] **Step 2: Commit**

```bash
git add Plugins/HermesAgentNPC/HermesAgentNPC.uplugin
git commit -m "feat: HermesAgentNPC 플러그인 매니페스트(.uplugin) 작성"
```

---

### Task 2: C++ 소스 코드를 플러그인 위치로 이동 & Build.cs 업데이트

**Files:**
- Create: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/...` (전체 C++ 소스 파일)
- Delete: `Source/HermesAgentNPC/...` (메인 프로젝트 하위 소스)

**Interfaces:**
- Consumes: 기존 `Source/HermesAgentNPC/` 내의 18개 C++ 소스/헤더 파일
- Produces: `Plugins/HermesAgentNPC/Source/HermesAgentNPC/` 위치의 플러그인 C++ 모듈

- [ ] **Step 1: 소스 폴더 전체를 플러그인 소스 디렉토리로 이동**

Move all items from `Source/HermesAgentNPC/` to `Plugins/HermesAgentNPC/Source/HermesAgentNPC/`.

- [ ] **Step 2: 메인 프로젝트에는 최소 모듈 또는 빈 모듈 유지/정리**

Maintain `Source/HermesAgentNPC.Target.cs` and `Source/HermesAgentNPCEditor.Target.cs` and minimal host module if required by `.uproject`.

- [ ] **Step 3: Commit**

```bash
git add Plugins/ Source/
git commit -m "refactor: C++ 소스 코드 전체를 플러그인 모듈(Plugins/HermesAgentNPC/Source)로 이동"
```

---

### Task 3: 핵심 블루프린트 에셋 이동 & ThirdPerson 템플릿 정리

**Files:**
- Move: `Content/NPC/BP_HermesNPC.uasset` ➔ `Plugins/HermesAgentNPC/Content/NPC/BP_HermesNPC.uasset`
- Move: `Content/Widgets/WBP_HermesDialogue.uasset` ➔ `Plugins/HermesAgentNPC/Content/Widgets/WBP_HermesDialogue.uasset`
- Remove: `Content/ThirdPerson/`, `Content/Characters/`, `Content/LevelPrototyping/`, `Content/Maps/`, `Content/Input/`

**Interfaces:**
- Consumes: 에디터에서 생성된 `BP_HermesNPC`, `WBP_HermesDialogue`
- Produces: 플러그인 내에 내장된 완제품 블루프린트 에셋 (`/HermesAgentNPC/NPC/BP_HermesNPC`, `/HermesAgentNPC/Widgets/WBP_HermesDialogue`)

- [ ] **Step 1: 블루프린트 에셋을 플러그인 Content 폴더로 이동**

Move `Content/NPC/` and `Content/Widgets/` to `Plugins/HermesAgentNPC/Content/`.

- [ ] **Step 2: 프로젝트 Content/ 에서 ThirdPerson 템플릿 에셋 제거**

Remove unused ThirdPerson template folders from `Content/`.

- [ ] **Step 3: Commit**

```bash
git add Plugins/ Content/
git commit -m "feat: 핵심 블루프린트 에셋 플러그인 Content로 이동 및 ThirdPerson 템플릿 정리"
```

---

### Task 4: `.uproject` 프로젝트 타겟 설정 업데이트 및 검증

**Files:**
- Modify: `HermesAgentNPC.uproject`

**Interfaces:**
- Consumes: `Plugins/HermesAgentNPC/HermesAgentNPC.uplugin`
- Produces: 플러그인을 정식 로드하도록 구성된 메인 프로젝트

- [ ] **Step 1: `.uproject` 수정**

`HermesAgentNPC.uproject`:
```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "Hermes agent NPC UE5 client",
	"Plugins": [
		{
			"Name": "HermesAgentNPC",
			"Enabled": true
		}
	]
}
```

- [ ] **Step 2: Commit**

```bash
git add HermesAgentNPC.uproject
git commit -m "config: .uproject에 HermesAgentNPC 플러그인 활성화 설정"
```

---

### Task 5: 전체 플러그인 빌드 및 자동화 테스트 검증

**Files:**
- Execute: `Build.bat` & `UnrealEditor-Cmd.exe` (Automation RunTests Hermes)

**Interfaces:**
- Consumes: 플러그인으로 전환된 전체 시스템
- Produces: `Result: Succeeded` 및 `Hermes.*` 자동화 테스트 5종 PASS (`EXIT CODE: 0`)

- [ ] **Step 1: 플러그인 재빌드 실행**

Run:
```cmd
cmd.exe /c '"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex'
```
Expected: `Result: Succeeded`.

- [ ] **Step 2: 자동화 테스트 실행**

Run:
```cmd
cmd.exe /c '"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi'
```
Expected: `**** TEST COMPLETE. EXIT CODE: 0 ****` (5/5 PASS)

- [ ] **Step 3: Commit & PROGRESS 업데이트**

```bash
git add docs/superpowers/plans/PROGRESS.md
git commit -m "docs: 플러그인 전환 완료 및 자동화 테스트 검증"
```
