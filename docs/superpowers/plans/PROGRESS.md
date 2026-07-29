# Hermes UE5 클라이언트 — 작업 진행 기록

> 마지막 업데이트: 2026-07-29 (프로토콜 문서 동기화 및 테스트 기준선 재검증)
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

## 자동화 테스트 결과 (총 12종 전원 PASS, 2026-07-29 재실행 확인)

- `Hermes.ActionParams.Coordinate` : PASS
- `Hermes.ActionParams.ItemId` : PASS
- `Hermes.ActionParams.Quantity` : PASS
- `Hermes.Actions.Dispatcher.Route` : PASS
- `Hermes.Inventory.AddRemove` : PASS
- `Hermes.Inventory.AddSaturates` : PASS
- `Hermes.Protocol.FrameAccumulator.Parse` : PASS
- `Hermes.Protocol.FrameCodec.Encode` : PASS
- `Hermes.Protocol.Messages.Build` : PASS
- `Hermes.RateLimiter.TokenBucket` : PASS
- `Hermes.Settings.CommandLineOverride` : PASS
- `Hermes.Util.PushBounded` : PASS
- `EXIT CODE: 0` (12/12 PASS)
