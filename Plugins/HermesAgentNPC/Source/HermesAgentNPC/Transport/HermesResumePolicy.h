#pragma once
#include "CoreMinimal.h"

/**
 * 의도적 재개(Reconnect)에 필요한 대기 시간.
 *
 * 종료성 에러로 재연결이 정지될 때마다 다음 재개까지의 대기가 커진다.
 * eviction war — 같은 신원의 두 인스턴스가 서로를 영원히 걷어내는 상황 — 을
 * 수렴시키기 위한 것이다. 되받아칠수록 느려지므로 한쪽이 먼저 포기한다.
 *
 * **첫 정지는 대기가 없다.** 정당하게 한 번 밀려난 사람을 벌하면 안 된다.
 * 대가는 되받아치기를 반복할 때만 발생한다.
 *
 * 워커의 자동 재연결 백오프(HermesBackoff)와 합치지 않는다. 사다리 모양은
 * 닮았지만 대상이 다르다 — 저쪽은 워커가 스스로 도는 것이고 이쪽은 게임이
 * 의도적으로 부르는 것이라, 초기값·상한·증가 시점이 모두 다르다.
 *
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesResumePolicy
{
	/**
	 * ConsecutiveSuspends 는 1부터 센다(첫 정지가 1).
	 * 1이면 0, 2면 Initial, 3이면 2배… 상한은 Max.
	 */
	float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max);
}
