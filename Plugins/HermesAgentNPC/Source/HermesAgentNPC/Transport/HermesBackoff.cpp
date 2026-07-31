#include "Transport/HermesBackoff.h"
#include "Math/UnrealMathUtility.h"

namespace HermesBackoff
{
	float NextAfterFailedConnect(float Current, float Initial, float Max)
	{
		// 0 이나 음수로 시작하면 곱해도 0 이라 사다리가 서지 않는다. 그 상태로
		// 두면 대기 없는 무한 재시도가 된다. 다만 곧바로 두 배로 올리면 첫 칸을
		// 건너뛰므로, 초기값 아래에서는 초기값으로 끌어올리기만 한다.
		if (Current < Initial)
		{
			return FMath::Min(Initial, Max);
		}
		return FMath::Min(Current * 2.f, Max);
	}

	bool WasHealthy(double LifetimeSeconds, float HealthySeconds)
	{
		return LifetimeSeconds >= static_cast<double>(HealthySeconds);
	}

	float NextAfterDisconnect(float Current, double LifetimeSeconds,
	                          float HealthySeconds, float Initial, float Max)
	{
		if (WasHealthy(LifetimeSeconds, HealthySeconds))
		{
			// 쓸 만큼 살아 있었다. 일시적 단절이나 서버 재시작이 다음 재연결을
			// 느리게 만들 이유가 없다.
			return Initial;
		}
		// 붙자마자 끊겼다. 접속 실패와 구분할 이유가 없다 — 어느 쪽이든
		// 곧바로 다시 시도해서 나아지지 않는다.
		return NextAfterFailedConnect(Current, Initial, Max);
	}
}
