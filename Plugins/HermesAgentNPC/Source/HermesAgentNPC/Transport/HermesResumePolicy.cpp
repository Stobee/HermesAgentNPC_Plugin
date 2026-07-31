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

		// 2회째가 Initial, 그 뒤로 2배씩. 지수가 커지면 float 이 넘치므로
		// 곱하기 전에 상한을 걸어 둔다.
		const int32 Steps = FMath::Min(ConsecutiveSuspends - 2, 30);
		const float Scaled = Initial * FMath::Pow(2.f, static_cast<float>(Steps));
		return FMath::Min(Scaled, Max);
	}
}
