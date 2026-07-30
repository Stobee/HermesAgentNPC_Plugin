# Hermes 클라이언트 통합 테스트 체크리스트

> **2026-07-30 주의.** 이 문서는 Task 9~13e 이전에 작성되어 일부 항목이 낡았다.
> 검증 환경 구축과 Phase 3 기능의 최신 검증 항목은
> **`docs/testing/manual-verification-setup.md`** 를 정본으로 본다.
> Task 19 착수 시 그 문서의 §4.2 표를 기준으로 이 파일을 갱신할 것.

## 사전
- [ ] 서버 `scripts/ue5_test_client.py`를 같은 LAN에서 실행해 프레이밍이 맞는지 먼저 확인
      (connect → identify → chat → 응답 출력, action_request 자동 응답).

## 클라이언트 검증 (에디터 PIE)
- [ ] PIE 실행 시 로그에 소켓 연결 성공 + `identify` 송신 → `identified` 수신 확인.
- [ ] SaveGame(`HermesPlayer`)에 **서버가 발급한** player_id/session_token이 저장됐는지 확인
      (재실행 시 같은 신원으로 재-identify). Task 10 이후 클라이언트는 UUID를 만들지 않는다.
- [ ] NPC에 다가가 Interact → 대화창 오픈.
- [ ] 채팅 입력(예: "언덕 위로 이동해줘") → "생각 중..." → `chat_response.text` 표시.
- [ ] 서버가 `move_to` action_request 발행 시 NPC가 실제 이동 + `action_result{started, eta_seconds}`
      즉시 회신, **도착 후** `action_event{completed, arrived}` 회신. Task 13b 이후
      `action_result`에 `arrived`를 넣는 것은 프로토콜 §4.5 위반이다.
- [ ] `follow_player(enabled:true)` → NPC가 플레이어 추적. `false` → 정지.
- [ ] `inventory_manage(list)` → `action_result.result.items` 반환.
- [ ] `item_transfer(give/receive)` → 수량 변화 + `{transferred:N}`.
- [ ] 미등록 command 강제 주입 시 `ok:false, error:"unsupported command"` 회신.

## 견고성
- [ ] 서버를 잠시 중단 → 클라이언트가 지수 백오프로 재연결 시도 로그.
- [ ] 서버 재기동 → 자동 재연결 + 동일 player_id 재-identify → 대화 맥락 유지.
- [ ] 서버 `ping` 수신 시 `pong` 회신 확인.
