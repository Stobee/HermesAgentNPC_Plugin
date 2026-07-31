#pragma once
#include "CoreMinimal.h"

/**
 * 수신한 대화 프레임을 화면에 반영할지 판정한다.
 *
 * 늦게 도착한 이전 턴의 응답이 현재 화면을 덮어쓰지 않게 하는 것이 목적이다.
 * 서버가 턴 직렬화(프로토콜 §3.6)를 어기고 두 턴을 교차 전송해도 여기서 걸린다.
 *
 * 델타와 응답의 규칙이 다르다. `chat_delta.id` 는 required 지만(§4.9),
 * `chat_response.id` 는 optional 이고(§4.4) **id 가 없는 것은 자발 발화**다 —
 * `action_event` 이후 서버가 먼저 거는 말("도착했어요")이 그것이다.
 * 둘을 같은 규칙으로 다루면 자발 발화가 통째로 사라진다.
 *
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesChatCorrelation
{
	/**
	 * chat_response 를 표시할지.
	 * IncomingId 가 비어 있으면 자발 발화이므로 항상 표시한다.
	 */
	bool ShouldDisplayResponse(const FString& IncomingId, const FString& LastSentId);

	/**
	 * chat_delta 를 표시할지.
	 * 델타는 id 가 반드시 있어야 하므로 비어 있으면 규격 위반으로 보고 버린다.
	 */
	bool ShouldDisplayDelta(const FString& IncomingId, const FString& LastSentId);
}
