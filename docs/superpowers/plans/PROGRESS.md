# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-29 (프로토콜 계약 확정 · 단일 NPC 소유권 · 서버 구현자용 문서 세트 완성)
> 브랜치: `feat/settings-globalization-protocol-v2`
> 계획 문서: `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md`

## 완료 & 검증된 태스크 (전체 커밋 완료)

- [x] **Task 1** `UHermesSettings` 설정 클래스 및 `-HermesHost=` / `-HermesPort=` 커맨드라인 오버라이드 구현 (`Hermes.Settings.CommandLineOverride` PASS)
- [x] **Task 2** 하드코딩된 서버 IP/포트/타임아웃/슬롯 상수를 `UHermesSettings` 소비 구조로 전면 전환
- [x] **Task 3** `ISocketSubsystem::GetAddressInfo` 기반 DNS 해석 도입으로 호스트명 지원
- [x] **Task 4** 소멸자 백오프 대기를 100ms 조각 sleep으로 교체하여 PIE 정지 응답성 확보 (Max 100ms 지연)
- [x] **Task 5** 액션 파라미터(좌표/수량/ID) 하드 바운드 검증 및 인벤토리 오버플로 포화 처리 (`Hermes.ActionParams.*`, `Hermes.Inventory.AddSaturates` PASS)
- [x] **Task 6** 액션 토큰 버킷 레이트 리미터 도입 및 타이머 핸들 회수 (`Hermes.RateLimiter.TokenBucket` PASS)
- [x] **Task 7** 인바운드/아웃바운드 큐 상한 및 틱 처리 예산(`MaxInboundFramesPerTick`) 구현
- [x] **Task 8** `identified` 이전 보류 발화 상한(`HermesUtil::PushBounded`, `MaxPendingChats`) 구현 (`Hermes.Util.PushBounded` PASS)
- [x] **Task 18** `README.md` 및 `HermesAgentNPC_Documentation.html` 가이드/사양서 문서 설정 기반 및 안전 사양으로 최신화

## 문서 동기화 (2026-07-29, 계획 외 후속 작업)

- [x] `ue5-socket-protocol.md` 를 실제 `UHermesSettings` 기본값과 동기화 (`ad8e852`)
  - §9 `Client-enforced limits` 신설 — 큐 상한·`MaxPendingChats`·재연결 백오프·라이브니스·TLS 키를 설정명/기본값/위반 시 동작으로 정리
  - §0 에 DNS 호스트명 해석 및 IPv4 우선 선택 명시 (Task 3 반영)
  - §4.7·§4.9·§6 의 숫자에 실제 설정 키 병기, 레이트 리밋이 큐잉 아닌 즉시 거부임을 명시
  - TLS 키(Task 14~16)와 keepalive·chat 타임아웃 키(Task 12·13)가 **선언만 되어 있고 소비 코드가 없음**을 상태 주석으로 표기
- [x] 문서 기본값 오류 2건 수정 — HTML `MaxPendingChats` 100→32, README `Initial Reconnect Delay` 1.0s→0.5s

## 프로토콜 계약 확정 및 단일 NPC 소유권 (2026-07-29, `ff7394f`)

사용자 결정으로 프로토콜의 미정 사항 3건을 확정하고, 플러그인 범위를 단일 NPC로 못박았다.

- [x] **세션 탈취** (§3.4) — 최신 연결이 이긴다. 기존 연결에 `session_taken_over`를 **보내고 나서** 닫는다
- [x] **재전송 없음** (§3.5) — 와이어에 올라간 `chat`은 재전송하지 않고, 끊긴 연결의 응답은 서버가 폐기한다
- [x] **에러 코드 10종 확정** (§5) — 코드별 연결 유지/종료 + 클라이언트 반응을 계약으로 명시, `error.id` 선택 필드 추가
- [x] **단일 NPC 범위 확정** — 프로토콜에 "Scope: one connection, one NPC" 절 추가
- [x] **활성 NPC 소유권 구현** — `ResetHandlers()` 도입, `RegisterNpc`가 교체 방식으로 전환, `UnregisterNpc`/`EndPlay` 해제, `bAutoRegisterAsActiveNpc` + `BecomeActiveHermesNpc()` (`Hermes.Actions.Dispatcher.Rebind` PASS)

### 이로 인해 늘어난 Phase 3 작업 (계획서에 Task 13c~13e로 추가)

- [ ] **Task 13c** 에러 코드 반응 정책 (순수) — `Hermes.Connection.ErrorPolicy`
- [ ] **Task 13d** 종료성 에러 시 재연결 루프 정지 — 현재 클라는 무조건 재시도하므로 방치 시 eviction war 발생
- [ ] **Task 13e** `error.id` 기반 진행 중 턴 즉시 실패 — `Hermes.PendingChats.FailById`

## 턴 직렬화 및 남은 공백 3건 (2026-07-29, `478e473`)

서버 구현자가 프로토콜 파일만으로 막히지 않는지 점검하다 나온 것들이다.

- [x] **턴 직렬화 확정** (§3.6 신설) — 세션당 한 번에 한 턴, 도착 순서대로, 두 턴의 프레임 교차 전송 금지.
  대화 위젯에 전송 게이팅이 없어(`HermesDialogueWidget.cpp:32-43`) 연속 발화가 정상적으로 발생하는데
  서버 동작이 규정되어 있지 않았다. 대기 큐 상한 초과분은 `rate_limited`를 해당 `chat.id`와 함께 회신
- [x] **`chat_id` 용도 명시** (§4.2) — 서버 내부 기록용. 클라이언트는 읽지도 되돌려주지도 않는다
- [x] **모르는 `id`의 `action_event`** (§4.10) — 서버 재시작 후 정상 발생. 에러로 답하지 말고 버린다

> 파급: Task 11에 "델타 `id` 변경 시 누적 버퍼를 버리고 새로 시작" 요구가 추가되었다.

## 로그 카테고리 (2026-07-29, `b0d332b`)

- [x] **`LogHermes` 도입** — 로그 5곳이 전부 `LogTemp`를 쓰고 있어 도입 프로젝트가 Hermes 로그만
  걸러내거나 verbosity를 조절할 수 없었다. 카테고리가 출처를 밝히므로 `[Hermes]` 접두사도 제거
- [x] **계획서 Global Constraints에 규칙 2건 추가** — `LogTemp` 금지, 그리고 "스펙상 불가능한 입력은
  조용히 무시하지 않는다". 해당 지점을 Task 10/11/13/13c/13d로 명시해 각 태스크가 자기 로그를 포함하게 했다

## 서버 구현자용 문서 세트 완성 (2026-07-29)

플러그인의 공개 계약과 이 프로젝트의 서버 작업 지시를 분리하고, 내부 기록이 서버 작업자에게
노출되지 않도록 정리했다.

- [x] **v1 프레이밍 전면 제거** (`bfd1539`) — 플러그인이 미배포라 설치 기반이 없다. "What changed from v1"
  절을 "Non-negotiable properties"로 교체하고, 자격 증명 없는 `identified` 거부 규칙은 v1 종속에서
  분리해 영구 규칙으로 승격
- [x] **Windows 서버 지원** (`bfd1539`) — 스펙 §0에 SPKI 핀 산출의 PowerShell 변형 추가.
  PowerShell 파이프는 바이트가 아니라 텍스트를 나르므로 bash 파이프라인을 그대로 옮기면 DER이 깨진다.
  서버 OS가 프로토콜의 일부가 아님도 명시
- [x] **서버 작업 프롬프트 작성** (`claude_code_prompt_hermes_server.md`) — 아키텍처를 실제에 맞게
  확정(`428dc76`): 에이전트는 Hermes 고정, LLM 백엔드는 llama-server 고정, 재량은 모델 파일·프롬프트·
  컨텍스트 정책. 프로토콜 스펙과 둘이면 자족하도록 모듈 경계까지 흡수(`9581910`)
- [x] **`HermesServer_SetupChecklist.html` 신설** (`19f7656`) — 준비물·사전 설정·모듈 경계·llama 연동·
  재량/고정 구분·현재 클라이언트 미구현 목록·흔한 실패와 원인. 체크 상태는 localStorage에 저장
- [x] **내부 설계 문서 참조 제거** (`53b7084`) — 필요한 내용(모듈 경계 다섯 축, llama 연동 지침)은
  HTML로 옮기고, 프롬프트·HTML 양쪽에서 `docs/superpowers/**` 참조를 걷어냈다.
  모듈 경계는 서버 내부 구조이므로 프로토콜 문서에는 넣지 않았다
- [x] **v1 시절 클라이언트 작업 브리프 삭제** — `claude_code_prompt_ue5_client.md` (원래 untracked)

## 자동화 테스트 결과 (총 13종 전원 PASS, 2026-07-29 확인)

- `Hermes.ActionParams.Coordinate` : PASS
- `Hermes.ActionParams.ItemId` : PASS
- `Hermes.ActionParams.Quantity` : PASS
- `Hermes.Actions.Dispatcher.Rebind` : PASS ← 단일 NPC 확정 (2026-07-29)
- `Hermes.Actions.Dispatcher.Route` : PASS
- `Hermes.Inventory.AddRemove` : PASS
- `Hermes.Inventory.AddSaturates` : PASS
- `Hermes.Protocol.FrameAccumulator.Parse` : PASS
- `Hermes.Protocol.FrameCodec.Encode` : PASS
- `Hermes.Protocol.Messages.Build` : PASS
- `Hermes.RateLimiter.TokenBucket` : PASS
- `Hermes.Settings.CommandLineOverride` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (13/13 PASS)
