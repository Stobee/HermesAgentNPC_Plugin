# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-30 (Task 12 keepalive ping·사망 판정 완료)
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

## 자동화 테스트 결과 (총 17종 전원 PASS)

- `Hermes.ActionParams.Coordinate` : PASS
- `Hermes.ActionParams.ItemId` : PASS
- `Hermes.ActionParams.Quantity` : PASS
- `Hermes.Actions.Dispatcher.Rebind` : PASS
- `Hermes.Actions.Dispatcher.Route` : PASS
- `Hermes.Inventory.AddRemove` : PASS
- `Hermes.Inventory.AddSaturates` : PASS
- `Hermes.Liveness.Evaluate` : PASS
- `Hermes.Protocol.FrameAccumulator.Parse` : PASS
- `Hermes.Protocol.FrameCodec.Encode` : PASS
- `Hermes.Protocol.Messages.Build` : PASS
- `Hermes.Protocol.Messages.IdentifyV2` : PASS
- `Hermes.Protocol.Messages.ParseIdentified` : PASS
- `Hermes.Protocol.Messages.Ping` : PASS
- `Hermes.RateLimiter.TokenBucket` : PASS
- `Hermes.Settings.CommandLineOverride` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (17/17 PASS)
