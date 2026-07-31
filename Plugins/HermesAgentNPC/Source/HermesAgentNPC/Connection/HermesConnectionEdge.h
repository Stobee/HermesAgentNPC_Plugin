#pragma once
#include "CoreMinimal.h"

/**
 * 연결 전이 판정.
 *
 * 게임 스레드는 워커의 연결 상태를 매 틱 폴링해서만 알 수 있다. 그런데
 * 불린 하나로는 "끊겼다가 다시 붙었다"를 표현할 수 없다 — 두 폴링 사이에
 * 끊김과 재연결이 모두 끝나면 값이 참에서 참으로 보여 아무 일도 없던 것이 된다.
 * 그러면 새 연결에 identify 가 나가지 않고, 서버는 신원 없는 연결을 들고 있게 된다.
 *
 * 그래서 불린과 함께 연결 세대(연결이 성립할 때마다 증가)를 같이 본다.
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesConnectionEdge
{
	enum class EDecision : uint8
	{
		/** 변화 없음. */
		Nothing,
		/** 새로 붙었다. identify 를 보내야 한다. */
		Opened,
		/** 끊겼고 아직 붙지 않았다. */
		Closed,
		/** 끊겼다가 다시 붙었다. 끊김 처리와 identify 를 모두 해야 한다. */
		Reopened
	};

	/**
	 * SeenGeneration 은 게임 스레드가 마지막으로 관측한 세대이고
	 * CurrentGeneration 은 워커의 현재 세대다. 둘이 다르면 그 사이에 적어도
	 * 한 번 새 연결이 성립했다는 뜻이다.
	 */
	EDecision Evaluate(bool bWasConnected, uint32 SeenGeneration,
	                   bool bNowConnected, uint32 CurrentGeneration);
}
