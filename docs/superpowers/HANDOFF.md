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
| 5 — 문서·검증 | Task 18~19 | ✅ **완료** (24/24 자동화 테스트 + 스텁 서버 규격 검증) |

## ✅ 재개 지점 — 깨끗한 상태

**플러그인 C++ 코드 및 사양 문서 100% 완성.** 24종 자동화 테스트 전원 PASS (`EXIT CODE: 0`).
2026-07-30에 스텁 서버(`hermes_stub_server.py`) 기반 v2 소켓 프로토콜(`happy`, `session_taken_over` 등) 와이어 연동 검증을 성공적으로 마쳤다.

- **프로젝트 및 서버 준비 완료:**
  - 서버 측 준비물 및 연동 규격: `plugin-integration-guide.md`, `HermesServer_SetupChecklist.html`, `claude_code_prompt_hermes_server.md`
  - 수동 검증 스텁 서버: `docs/testing/hermes_stub_server.py`
  - 수동 검증 환경 구성 지침: `docs/testing/manual-verification-setup.md`
  - **검증용 샘플 에셋이 저장소에 존재한다.** 클론 직후 에디터 PIE 가 실행 가능하다.
    `L_HermesTest`, `BP_TestMode`, `BP_HermesTestPlayer`, `IA_Interact`, `IMC_Default`

## 이 프로젝트의 위치

플러그인은 **별도로 패키징해 내보낸다.** 이 저장소는 그 플러그인의 정상 구동을
시연하는 **샘플 프로젝트**다. 따라서 두 Content 폴더의 성격이 다르다.

| 위치 | 성격 |
|---|---|
| `Plugins/HermesAgentNPC/Content/` | 패키징에 실려 나간다. 검증 편의용 설정을 넣지 말 것 |
| `Content/` | 샘플에만 남는다. 데모용 메쉬·입력·레벨은 여기에 둔다 |

## ⬜ 남아있는 프로젝트 연동 과제 2건 (조건부 실작업)

**C++ 구현은 끝났다**(소스 전체에 `TODO`/`FIXME` 0건). 실제 서버 구축 시 수행할 2건:

### (1) `bUseTLS` 를 `True` 로 전환 — 실제 SSL 서버 구축 후

`Config/DefaultGame.ini` 가 개발 편의를 위해 아래로 설정되어 있다.

```ini
Host=192.168.0.111
Port=8770
bUseTLS=False
```

- Shipping 빌드는 `HermesConnectionSubsystem.cpp` 가 TLS 를 강제하므로 배포 사고는 없다.
- 실제 백엔드 SSL 서버(또는 SPKI 핀)가 구축되는 시점에 `DefaultGame.ini`의 `bUseTLS=True` 전환 및 `TlsPinnedPublicKeyHashes`를 설정한다.

### (2) `Reconnect()` 블루프린트 노출 — 게임 재접속 UI 설계 시

`HermesConnectionSubsystem.h` 에서 `public` 이지만 `UFUNCTION` 이 아니다.
프로토콜 §3.4 가 이 호출을 *의도적 행위*로 규정하므로, 아무 데서나 부를 수 없게 보류해 뒀다.
**게임에 재접속 UI(버튼)가 생기는 시점에 `UFUNCTION(BlueprintCallable)` 을 정식으로 붙인다.**

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
c2a4d28  docs: 스텁 서버 와이어 검증 완료 및 브랜치 정리
4f4c9e9  docs: Phase 1~5 전체 태스크 완료 및 통합 검증 완료 인계 기록 갱신 (Task 19 ✅)
4ebf01d  docs: Task 16 완료 및 Phase 1~4 구현 종료 인계 기록 갱신
7098f99  feat: OpenSSL 기반 TLS 전송 구현 (Task 16 ✅) — Phase 4 완료
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
