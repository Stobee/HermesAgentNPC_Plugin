# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-31 (스텁 14/14 + 실서버 연동 검증, 클라이언트·서버 버그 수정)
> 브랜치: `master`
> 계획 문서: `docs/superpowers/plans/2026-07-28-hermes-settings-protocol-v2.md`

## 완료 & 검증된 태스크 (전체 100% 완료)

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
- [x] **Task 19** 24종 자동화 테스트 및 통합 시스템 검증 완료
- [x] **검증 환경** PIE 수동 검증용 샘플 에셋을 저장소에 포함 (`1068a43`)
- [x] **Task 20** 재연결 시 `identify` 누락 수정 — 연결 세대 도입 및 판정 분리
      (`Hermes.Connection.Edge` PASS). 배경은 `manual-verification-setup.md` §7.1
- [x] **Task 21** 대화창 입력 모드·커서 누락 및 중복 바인딩 수정. 배경은 §7.2
- [x] **Task 22** 검증용 콘솔 명령(`Hermes.Interact`/`Chat`/`Status`)과 헤드리스
      하네스 정비. Shipping 빌드에는 컴파일되지 않는다
- [x] **스텁 수정** `silent_after_identify` 가 pong 을 돌려줘 침묵하지 않던 문제. §7.3
- [x] **Task 23** 실서버 연동 준비 — 프레임 트레이스(`Hermes.Trace.FormatFrame` PASS,
      `session_token` 마스킹), 하네스 `-Endpoint` 모드, TLS 절차 문서(§8)
- [x] **Task 24** 자발 발화(id 없는 `chat_response`) 표시 수정 — 상관 규칙을
      `HermesChatCorrelation` 으로 분리 (`Hermes.Chat.Correlation` PASS). §7.6
- [x] **스텁 추가** `unprompted_speech` 시나리오 — §4.4 자발 발화를 스텁으로
      재현할 수 없던 공백을 메웠다
- [x] **문서** 작은 모델 주의사항을 플러그인 측 문서에 추가
      (`README.md`, `HermesAgentNPC_Documentation.html` §08)

## ✅ 스텁 14개 시나리오 전부 실행 검증 (2026-07-31)

`docs/testing/run-headless-verification.ps1` 로 스텁 서버에 실제로 붙여 확인했다.
`-game` 모드지만 `L_HermesTest` 가 그대로 로드되어 NPC·내비메시·대화 위젯이 모두
살아 있으므로 같은 코드를 지난다. 실행 명령과 관측된 증거는
`docs/testing/manual-verification-setup.md` §4.0 에 있다.

**14/14 통과.** `happy`, `move_to`(실제 이동·`action_event{arrived}`), `unknown_command`,
`server_busy`, `error_without_id`, `stale_delta`, `interleaved_turns`, `chat_timeout`,
`silent_after_identify`, `session_taken_over`, `unsupported_version_error`,
`not_authorized`, `bad_frame`, `unprompted_speech`.

이 과정에서 버그 5건(클라이언트 3건 + 스텁 2건)을 찾아 고쳤다. 실서버 연동에서는 서버 측 버그도 함께 찾아 고쳤다(HermesLlamaServer 저장소).

## ⬜ 아직 남은 것

- [ ] **`bUseTLS=True` 전환** — `Config/DefaultGame.ini` 가 아직 `False` 다.
      계획서 694행이 지정한 임시값이며, 서버 인증서 준비가 선행되어야 한다
- [ ] **`Reconnect()` 블루프린트 노출** — 게임에 재접속 UI 가 생기는 시점에
      `UFUNCTION(BlueprintCallable)` 부착 (의도적 보류)
- [ ] **위젯이 눈에 어떻게 보이는지** — 동작은 전부 자동 검증되지만 레이아웃·글자
      크기·창 위치는 로그로 드러나지 않는다. PIE 에서 한 번 보면 되는 확인이다
- [ ] **에러로 인한 재연결의 백오프 부재** — 판단 필요. 배경은
      `manual-verification-setup.md` §7.4

## 자동화 테스트 결과 (총 27종 전원 PASS)

- `Hermes.ActionParams.Coordinate` : PASS
- `Hermes.ActionParams.ItemId` : PASS
- `Hermes.ActionParams.Quantity` : PASS
- `Hermes.Actions.Dispatcher.Rebind` : PASS
- `Hermes.Actions.Dispatcher.Route` : PASS
- `Hermes.Chat.Correlation` : PASS
- `Hermes.Connection.Edge` : PASS
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
- `Hermes.Trace.FormatFrame` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (27/27 PASS, 2026-07-31 재확인)
