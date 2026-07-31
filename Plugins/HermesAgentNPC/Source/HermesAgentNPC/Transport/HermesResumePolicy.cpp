#include "Transport/HermesResumePolicy.h"
#include "Math/UnrealMathUtility.h"

namespace HermesResumePolicy
{
	float RequiredCooldown(int32 ConsecutiveSuspends, float Initial, float Max)
	{
		// 정지가 없었거나 첫 정지면 기다릴 것이 없다.
		if (ConsecutiveSuspends <= 1)
		{
			return 0.f;
		}

		// 2회째가 Initial, 그 뒤로 2배씩. 
		// float 무한대(inf)도 FMath::Min 에서 올바르게 처리되므로 30 상한은
		// 기술적으로 필수 요소는 아니지만 방어적으로 남겨 둔다.
		const int32 Steps = FMath::Min(ConsecutiveSuspends - 2, 30);
		const float Scaled = Initial * FMath::Pow(2.f, static_cast<float>(Steps));
		return FMath::Min(Scaled, Max);
	}
}
