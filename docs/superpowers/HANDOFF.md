# 작업 인계 문서

> 이 파일은 작업이 중단되었을 때 다음 세션이 이어받기 위한 기록이다.
> **태스크를 완료할 때마다 갱신한다.**

- 최종 갱신: 2026-07-30
- 브랜치: `master`

## 지금 상태

**Phase 1 ~ Phase 5 (Task 1 ~ Task 19) 전체 태스크 100% 완료!**

| Phase | 범위 | 상태 |
|---|---|---|
| 프로토콜 문서 | Task 17 (앞당김) | ✅ **완료** |
| 1 — 설정 전역화 | Task 1~3 | ✅ **완료** |
| 2 — 입력 강건성 | Task 4~8 | ✅ **완료** |
| 3 — 프로토콜 v2 코드 | Task 9~13e | ✅ **완료** |
| 4 — TLS | Task 14~16 | ✅ **완료** |
| 5 — 문서·검증 | Task 18~19 | ✅ **완료** |

## ✅ 재개 지점 — 깨끗한 상태

**플러그인 C++ 코드 및 사양 문서 100% 완성.** 24종 자동화 테스트 전원 PASS (`EXIT CODE: 0`).

- **프로젝트 및 서버 준비 완료:**
  - 서버 측 준비물 및 연동 규격: `plugin-integration-guide.md`, `HermesServer_SetupChecklist.html`, `claude_code_prompt_hermes_server.md`
  - 수동 검증 스텁 서버: `docs/testing/hermes_stub_server.py`
  - 수동 검증 환경 구성 지침: `docs/testing/manual-verification-setup.md`
  - **검증용 샘플 에셋이 저장소에 들어왔다** (2026-07-30). 클론 직후 PIE 가 바로 뜬다.
    `L_HermesTest`, `BP_TestMode`, `BP_HermesTestPlayer`, `IA_Interact`, `IMC_Default`

## 이 프로젝트의 위치

플러그인은 **별도로 패키징해 내보낸다.** 이 저장소는 그 플러그인의 정상 구동을
시연하는 **샘플 프로젝트**다. 따라서 두 Content 폴더의 성격이 다르다.

| 위치 | 성격 |
|---|---|
| `Plugins/HermesAgentNPC/Content/` | 패키징에 실려 나간다. 검증 편의용 설정을 넣지 말 것 |
| `Content/` | 샘플에만 남는다. 데모용 메쉬·입력·레벨은 여기에 둔다 |

> 실제로 `BP_HermesNPC` 에 붙어 있던 검증용 `SkeletalCube` 메쉬를 플러그인에서 빼고
> 샘플 레벨의 인스턴스로 옮겼다(`cf09bca`). 같은 실수를 반복하지 말 것.

## 남은 확인 (PIE 필요)

에셋 정합성은 확인했으나 **에디터를 띄워야만 알 수 있는 것이 하나 남았다.**

- 내비메시가 `move_to` 스텁 좌표 `(500, 500, 100)` 을 실제로 덮는지.
  PIE 에서 `P` 키로 초록 내비메시를 확인한다. 덮지 않으면 볼륨을 키우거나
  `docs/testing/hermes_stub_server.py` 의 좌표를 레벨에 맞게 고친다.

## 문서 위치

| 문서 | 경로 |
|---|---|
| 서버 연동 가이드 | `plugin-integration-guide.md` |
| 플러그인 가이드 문서 | `README.md` |
| 기술 사양 HTML 문서 | `HermesAgentNPC_Documentation.html` |
| 프로토콜 계약 | `ue5-socket-protocol.md` |
| 수동 검증 환경 구축 가이드 | `docs/testing/manual-verification-setup.md` |
| 수동 검증용 스텁 서버 | `docs/testing/hermes_stub_server.py` |
| 설계 스펙 (내부) | `docs/superpowers/specs/2026-07-28-hermes-settings-globalization-design.md` |
| 구현 계획서 (내부) | `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md` |

## 커밋 히스토리

```
4ebf01d  docs: Task 16 완료 및 Phase 1~4 구현 종료 인계 기록 갱신
7098f99  feat: OpenSSL 기반 TLS 전송 구현 (Task 16 ✅) — Phase 4 완료
a425829  feat: TLS 검증 정책 결정 순수 로직 분리 (Task 15 ✅)
50823dc  refactor: 전송 계층을 IHermesTransport 로 추상화 (Task 14 ✅)
```

## 테스트 기준선

**2026-07-30 확인: 24종 전부 통과 (exit code 0).**

```
Hermes.ActionParams.Coordinate
Hermes.ActionParams.ItemId
Hermes.ActionParams.Quantity
Hermes.Actions.Dispatcher.Rebind
Hermes.Actions.Dispatcher.Route
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
Hermes.Util.PushBounded
```

## 주요 명령

```powershell
# 빌드
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" HermesAgentNPCEditor Win64 Development -Project="C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -WaitMutex

# 전체 테스트
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Work\HermesAgentNPC\HermesAgentNPC.uproject" -ExecCmds="Automation RunTests Hermes; Quit" -unattended -nopause -nullrhi
```
