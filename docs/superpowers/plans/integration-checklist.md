# Hermes 클라이언트 통합 테스트 체크리스트

## 사전
- [ ] 서버 `scripts/ue5_test_client.py`를 같은 LAN에서 실행해 프레이밍이 맞는지 먼저 확인
      (connect → identify → chat → 응답 출력, action_request 자동 응답).

## 클라이언트 검증 (에디터 PIE)
- [ ] PIE 실행 시 로그에 소켓 연결 성공 + `identify` 송신 → `identified` 수신 확인.
- [ ] SaveGame(`HermesPlayer`)에 player_id가 저장됐는지 확인(재실행 시 동일 UUID).
- [ ] NPC에 다가가 Interact → 대화창 오픈.
- [ ] 채팅 입력(예: "언덕 위로 이동해줘") → "생각 중..." → `chat_response.text` 표시.
- [ ] 서버가 `move_to` action_request 발행 시 NPC가 실제 이동 + `action_result{arrived:true}` 회신(15초 내).
- [ ] `follow_player(enabled:true)` → NPC가 플레이어 추적. `false` → 정지.
- [ ] `inventory_manage(list)` → `action_result.result.items` 반환.
- [ ] `item_transfer(give/receive)` → 수량 변화 + `{transferred:N}`.
- [ ] 미등록 command 강제 주입 시 `ok:false, error:"unsupported command"` 회신.

## 견고성
- [ ] 서버를 잠시 중단 → 클라이언트가 지수 백오프로 재연결 시도 로그.
- [ ] 서버 재기동 → 자동 재연결 + 동일 player_id 재-identify → 대화 맥락 유지.
- [ ] 서버 `ping` 수신 시 `pong` 회신 확인.
