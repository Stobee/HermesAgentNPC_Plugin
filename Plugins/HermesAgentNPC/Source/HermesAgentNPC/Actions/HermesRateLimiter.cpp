#include "Actions/HermesRateLimiter.h"

void FHermesRateLimiter::Configure(int32 PerSecond)
{
	Capacity        = (double)FMath::Max(1, PerSecond);
	RefillPerSecond = Capacity;
	Tokens          = Capacity;   // 시작은 가득 찬 상태
	LastRefillTime  = 0.0;
	bInitialized    = false;
}

bool FHermesRateLimiter::TryConsume(double NowSeconds)
{
	if (!bInitialized)
	{
		LastRefillTime = NowSeconds;
		bInitialized   = true;
	}

	const double Elapsed = NowSeconds - LastRefillTime;
	if (Elapsed > 0.0)
	{
		Tokens = FMath::Min(Capacity, Tokens + Elapsed * RefillPerSecond);
	}
	// 시간이 역행해도 기준을 갱신해 둔다. 그래야 이후 정상 시각에서
	// 거대한 Elapsed 가 계산되어 토큰이 폭증하는 일이 없다.
	LastRefillTime = NowSeconds;

	if (Tokens >= 1.0)
	{
		Tokens -= 1.0;
		return true;
	}
	return false;
}
