#pragma once
#include "CoreMinimal.h"

/**
 * 토큰 버킷 레이트 리미터.
 * 시간을 인자로 주입받아 엔진·타이머에 의존하지 않으므로 단독 테스트가 가능하다.
 */
class FHermesRateLimiter
{
public:
	/** 용량과 초당 충전량을 PerSecond 로 함께 설정한다. */
	void Configure(int32 PerSecond);

	/** NowSeconds 기준으로 토큰을 채우고 1개를 소비한다. 소비 실패 시 false. */
	bool TryConsume(double NowSeconds);

private:
	double Tokens          = 0.0;
	double Capacity        = 0.0;
	double RefillPerSecond = 0.0;
	double LastRefillTime  = 0.0;
	bool   bInitialized    = false;
};
