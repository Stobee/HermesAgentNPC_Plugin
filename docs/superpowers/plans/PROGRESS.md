# Hermes UE5 클라이언트 — 작업 진행 기록 (이어서 하기용)

> 마지막 업데이트: 2026-07-24 (세션 중단 시점)
> 브랜치: `feat/hermes-ue5-client`
> 실행 방식: inline (executing-plans 스킬), 계획 문서: `docs/superpowers/plans/2026-07-24-hermes-ue5-client.md`

## 환경 메모 (재개 시 바로 사용)

- 엔진: `C:\Program Files\Epic Games\UE_5.8`
- 빌드 커맨드:
  ```
  "/c/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -WaitMutex 2>&1 | grep -E "Result:|error C|: error|fatal error" | tail -40
  ```
  - 주의: 빌드 래퍼는 실패해도 exit 0을 반환할 수 있음 → 반드시 `Result: Succeeded/Failed` 라인을 확인할 것.
- 자동화 테스트 커맨드(예: Protocol):
  ```
  "/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Work/HermesAgentNPC/HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes.Protocol; Quit" -unattended -nopause -nullrhi
  ```
  - 결과는 stdout이 아니라 로그 파일에 있음: `C:\Work\HermesAgentNPC\Saved\Logs\HermesAgentNPC.log`
  - 확인 grep: `grep -E "Test Completed|Found [0-9]+ automation|TEST COMPLETE" <log>`
  - 필터 예: `Hermes.Protocol`, `Hermes.Actions`, `Hermes.Inventory`, 전체는 `Hermes`

## 완료 & 검증된 태스크 (커밋됨)

- [x] **Task 1** 프로젝트 스캐폴딩 — 빌드 성공
  - 해결한 이슈: 설치형 엔진 빌드환경 공유 충돌 → 두 Target.cs에 `bOverrideBuildEnvironment = true` 추가.
  - Build.cs에 `PublicIncludePaths.Add(ModuleDirectory);` 추가 → 하위폴더 `#include "Subdir/X.h"` 해석.
- [x] **Task 2** `FHermesFrameCodec::Encode` — 테스트 `Hermes.Protocol.FrameCodec.Encode` PASS
- [x] **Task 3** `FFrameAccumulator` — 테스트 `Hermes.Protocol.FrameAccumulator.Parse` PASS
- [x] **Task 4** 메시지 상수/JSON 헬퍼 — 테스트 `Hermes.Protocol.Messages.Build` PASS
- [x] **Task 5** `FHermesSocketWorker` (FRunnable, 재연결) — 빌드 성공
- [x] **Task 6** 디스패처 + `IHermesActionHandler` — 테스트 `Hermes.Actions.Dispatcher.Route` PASS

## 진행 중 (WIP, 빌드 실패 상태) — 여기서 재개

- [~] **Task 7** 인벤토리 + 액션 핸들러 4종 / **Task 8** NPC 캐릭터 + AIController
  - 작성 완료된 파일: `Inventory/HermesItem.h`, `Inventory/HermesInventoryComponent.{h,cpp}`,
    `Inventory/HermesInventory.spec.cpp`, `NPC/HermesNPCCharacter.{h,cpp}`, `NPC/HermesNPCAIController.{h,cpp}`,
    `Actions/{MoveTo,FollowPlayer,Inventory,ItemTransfer}ActionHandler.{h,cpp}`

### ★ 현재 블로커 (재개 시 첫 작업)

빌드 실패:
```
Actions/MoveToActionHandler.cpp(35): error C2039: 'Failed' is not a member of 'EPathFollowingRequestResult'
	if (R == EPathFollowingRequestResult::Failed)
```
- 원인 추정: UE5.8에서 `EPathFollowingRequestResult`의 열거자 접근/정의가 예상과 다름.
  `EPathFollowingRequestResult`는 `AITypes.h`에 **없음** (확인함). 정의 위치를 찾던 중 중단됨.
- **다음 단계**:
  1. 정의 위치 확인: `grep -rln "EPathFollowingRequestResult" "…/UE_5.8/Engine/Source/Runtime/"`
     (후보: `Navigation/PathFollowingComponent.h`, `AIController.h`).
  2. 실제 열거자 이름 확인 후 `MoveToActionHandler.cpp`의 실패 판정 수정.
     - 유력 후보: 값이 `EPathFollowingRequestResult::Type`이며 `Failed/AlreadyAtGoal/RequestSuccessful`.
       접근이 안 되면 해당 헤더 include 누락일 수 있음 → `#include "Navigation/PathFollowingComponent.h"` 추가 시도.
     - 대안: 성공 판정으로 뒤집기 — `if (R != EPathFollowingRequestResult::RequestSuccessful && R != EPathFollowingRequestResult::AlreadyAtGoal)` 를 실패로 처리.
  3. 재빌드 → 성공 시 `Hermes.Inventory.AddRemove` 테스트 실행(PASS 기대).
  4. Task 7·8 커밋.

## 남은 태스크

- [ ] **Task 9** `UHermesConnectionSubsystem` (+ `HermesSaveGame.h`) — 워커 수명/핸드셰이크/재연결 재-identify/타입 라우팅/SaveGame player_id
- [ ] **Task 10** `UHermesDialogueWidget` + NPC `Interact`/`BeginPlay`에서 `RegisterNpc` 배선
- [ ] **Task 11** `integration-checklist.md` 작성

## 완료 후 마무리

- 전체 자동화 테스트: `-ExecCmds="Automation RunTests Hermes; Quit"` 로 `Hermes.*` 전부 PASS 확인.
- `finishing-a-development-branch` 스킬로 마무리(머지/PR 옵션 제시).
- (선택) 사용자 관심사: **나중에 플러그인으로 전환** 가능하도록 레이어 분리 유지됨. 필요 시 "플러그인 전환" 태스크 추가.

## 커밋 규약

- 커밋 메시지 한국어, 끝에:
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
- 사용자는 **모든 답변을 한국어로** 원함.
