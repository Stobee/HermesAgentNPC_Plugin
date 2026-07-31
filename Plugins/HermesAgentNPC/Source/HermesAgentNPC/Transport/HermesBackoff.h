#pragma once
#include "CoreMinimal.h"

/**
 * 재연결 백오프 사다리.
 *
 * 되돌리는 기준이 **"붙었다"가 아니라 "쓸 만큼 살아 있었다"** 라는 것이 요점이다.
 * TCP 접속 성공만으로 사다리를 초기화하면, 접속은 되는데 서버가 곧바로 끊는
 * 상황(자격 증명 거부, bad_frame, 세션 정리 중)에서 매 사이클이 "성공"으로 보여
 * 백오프가 영영 초기값에 머문다. 그러면 재연결이 왕복 지연 속도로 돌며 서버를
 * 두드린다 — 실측으로 45초에 216 회였다.
 *
 * 시간도 상태도 없는 순수 함수라 단독 테스트가 가능하다.
 */
namespace HermesBackoff
{
	/** 접속 자체가 실패했을 때. 사다리를 한 칸 올린다. */
	float NextAfterFailedConnect(float Current, float Initial, float Max);

	/**
	 * 성립했던 연결이 끝났을 때.
	 *
	 * HealthySeconds 이상 살아 있었으면 초기값으로 되돌린다 — 일시적인 네트워크
	 * 단절이나 정상 운영 중의 재시작이 다음 재연결을 느리게 만들면 안 된다.
	 * 그보다 짧게 끊겼으면 접속 실패와 똑같이 사다리를 올린다.
	 */
	float NextAfterDisconnect(float Current, double LifetimeSeconds,
	                          float HealthySeconds, float Initial, float Max);

	/** 이 연결이 사다리를 되돌릴 만큼 살아 있었는가. 대기 여부 판단에 함께 쓴다. */
	bool WasHealthy(double LifetimeSeconds, float HealthySeconds);
}
