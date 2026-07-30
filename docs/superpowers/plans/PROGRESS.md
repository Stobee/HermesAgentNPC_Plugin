# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-30 (Task 16 OpenSSL 기반 TLS 전송 완료 — Phase 1~4 전체 구현 완료)
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
- [x] **Task 11** `chat_delta` 스트리밍 응답 수신 및 누적 표시
- [x] **Task 12** 수신 침묵 기반 사망 판정 및 keepalive ping 전송 (`Hermes.Liveness.Evaluate` PASS)
- [x] **Task 13** 대화 응답 타임아웃 및 발화 id 상관 (`Hermes.PendingChats.Timeout` PASS)
- [x] **Task 13b** `action_event` 비동기 통지 수신 (`Hermes.Protocol.Messages.ActionEvent` PASS)
- [x] **Task 13c** 에러 코드 반응 정책 순수 함수 분리 (`Hermes.Connection.ErrorPolicy` PASS)
- [x] **Task 13d** 종료성 에러 시 재연결 루프 정지
- [x] **Task 13e** `error.id` 기반 진행 중 턴 실패 처리 (`Hermes.PendingChats.FailById` PASS)
- [x] **Task 14** 전송 계층 `IHermesTransport` 추상화 및 `FHermesPlainTransport` 분리
- [x] **Task 15** TLS 검증 정책 결정 순수 로직 분리 (`Hermes.TlsPolicy.*` PASS)
- [x] **Task 16** OpenSSL 기반 TLS 전송 `FHermesTlsTransport` 구현 (SNI, SPKI 핀 검증, CA 검증, SO_KEEPALIVE)

## 자동화 테스트 결과 (총 24종 전원 PASS)

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
- `Hermes.TlsPolicy.ServerName` : PASS
- `Hermes.TlsPolicy.UseTls` : PASS
- `Hermes.TlsPolicy.VerifyMode` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (24/24 PASS)
