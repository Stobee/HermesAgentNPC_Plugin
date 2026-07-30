# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-30 (Task 14 전송 계층 추상화 완료 — Phase 4 진행 중)
> 브랜치: `master`
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
- [x] **Task 9** 프로토콜 v2 메시지 계층 구현 (`protocol_version: 2`, `session_token` AES 난독화 저장, `ParseIdentified`, `MakePing`) (`Hermes.Protocol.Messages.IdentifyV2`, `ParseIdentified`, `Ping` PASS)
- [x] **Task 10** 서버 발급 신원 흐름 구현 (클라이언트 `FGuid::NewGuid()` 제거, `LoadCredentials`/`SaveCredentials` 난독화 보관 및 재접속 검증)
- [x] **Task 11** `chat_delta` 스트리밍 응답 수신 및 누적 표시 (`OnChatDelta` 델리게이트, `id` 변경 시 버퍼 폐기 방어, `NativeTick` 틱당 1회 `SetText` 묶음). 자동화 테스트 추가 없음 — 위젯 상태라 Task 19 수동 검증 대상

- [x] **Task 12** 연결 유지와 죽은 연결 탐지 (`HermesLiveness::Evaluate` 시간 주입형 순수 판정, 송신 침묵 20초 시 `ping`, 수신 침묵 60초 시 `RequestReconnect()`, `PeerTimeout < PingInterval*2` 경고) (`Hermes.Liveness.Evaluate` PASS). **Step 10(소켓 `SO_KEEPALIVE`)은 UE 5.8 미지원으로 생략** — 상세 사유는 HANDOFF.md 및 계획서 Task 12 Step 10 참조

- [x] **Task 13** 대화 응답 타임아웃과 발화 `id` 상관 (`FHermesPendingChats` 시간 주입형 추적기, 델타 `Touch` 로 긴 생성 보호, 만료 시 `OnChatFailed` 통지, 연결 단절 시 진행 중 발화 일괄 실패, 위젯은 `GetLastSentChatId()` 와 일치하는 델타·응답만 반영) (`Hermes.PendingChats.Timeout` PASS). Task 11의 `StreamingId` 방어는 더 강한 상관 규칙으로 대체되어 제거

- [x] **Task 13b** `action_event` 로 `move_to` 완료를 비동기 통지 (`MakeActionEvent`, `SendActionEvent`, `ReceiveMoveCompleted` 델리게이트, `action_result` 는 `{started, eta_seconds}` 로 접수만 알림) (`Hermes.Protocol.Messages.ActionEvent` PASS). 계획서에 없던 `AlreadyAtGoal` 경로를 추가 처리 — 완료 콜백이 `MoveToLocation` 안에서 동기로 끝나 델리게이트로 잡을 수 없다

- [x] **Task 13c** 에러 코드 반응 정책을 순수 함수로 분리 (`EHermesErrorReaction`, `HermesErrorPolicy::React`, 프로토콜 §5의 10개 코드 전부 + 빈 문자열·미지 코드가 `LogOnly` 로 떨어지는지 검증) (`Hermes.Connection.ErrorPolicy` PASS). **배선 없음** — `StopReconnect` 는 Task 13d, `FailPendingTurn` 은 Task 13e 가 소비

- [~] **Task 13d** 종료성 에러 시 재연결 루프 정지 (워커 `bReconnectSuspended`, `SuspendReconnect`/`ResumeReconnect`, 백오프 조각 sleep 즉시 이탈, 정지 사유·재개 방법 Error 로그, 의도적 재개용 `UHermesConnectionSubsystem::Reconnect()`). 계획서가 배정하지 않은 `ReIdentify`/`DiscardCredentials`/`ReconnectWithBackoff` 반응도 함께 배선 — 두지 않으면 프로토콜 §5의 `not_identified`·`not_authorized` 계약이 미구현으로 남는다. **코드 Step 1~4·6 완료, Step 5 스텁 서버 수동 확인 미완** (`Content/` 에 맵이 없어 PIE 불가 — HANDOFF.md 참조)

- [x] **Task 13e** `error.id` 기반 진행 중 턴 즉시 실패 (`FHermesPendingChats::FailById` — 추적 중이던 발화만 제거하고 `true` 반환, 중복 실패는 `false`, `id` 없는 에러는 어떤 턴도 건드리지 않음) (`Hermes.PendingChats.FailById` PASS)

- [x] **Task 14** 전송 계층을 `IHermesTransport` 로 추상화 (`FHermesPlainTransport` 가 기존 `FSocket` 코드를 그대로 인계, 워커에서 `FSocket` 참조 완전 제거). **순수 리팩터링** — 프레이밍·큐·백오프는 한 줄도 바뀌지 않았고 테스트 21종이 같은 수·같은 결과로 통과한다. Task 16의 TLS 구현이 끼워질 자리다

## 자동화 테스트 결과 (총 21종 전원 PASS)

- `Hermes.ActionParams.Coordinate` : PASS
- `Hermes.ActionParams.ItemId` : PASS
- `Hermes.ActionParams.Quantity` : PASS
- `Hermes.Actions.Dispatcher.Rebind` : PASS
- `Hermes.Actions.Dispatcher.Route` : PASS
- `Hermes.Connection.ErrorPolicy` : PASS
- `Hermes.Inventory.AddRemove` : PASS
- `Hermes.Inventory.AddSaturates` : PASS
- `Hermes.Liveness.Evaluate` : PASS
- `Hermes.PendingChats.FailById` : PASS
- `Hermes.PendingChats.Timeout` : PASS
- `Hermes.Protocol.FrameAccumulator.Parse` : PASS
- `Hermes.Protocol.FrameCodec.Encode` : PASS
- `Hermes.Protocol.Messages.ActionEvent` : PASS
- `Hermes.Protocol.Messages.Build` : PASS
- `Hermes.Protocol.Messages.IdentifyV2` : PASS
- `Hermes.Protocol.Messages.ParseIdentified` : PASS
- `Hermes.Protocol.Messages.Ping` : PASS
- `Hermes.RateLimiter.TokenBucket` : PASS
- `Hermes.Settings.CommandLineOverride` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (21/21 PASS)
