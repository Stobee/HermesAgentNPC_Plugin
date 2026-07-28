#include "Actions/HermesActionParams.h"

namespace HermesParams
{
	bool IsValidCoordinate(double V, float Limit)
	{
		if (!FMath::IsFinite(V))
		{
			return false;
		}
		return FMath::Abs(V) <= (double)Limit;
	}

	bool IsValidQuantity(double V, int32 Max, int32& OutQty)
	{
		if (!FMath::IsFinite(V))
		{
			return false;
		}
		// 소수는 거부한다. quantity 는 프로토콜상 정수다.
		if (V != FMath::TruncToDouble(V))
		{
			return false;
		}
		if (V < 1.0)
		{
			return false;
		}
		const double MaxD = (double)FMath::Max(1, Max);
		if (V > MaxD)
		{
			return false;
		}
		// 위 검사로 1 <= V <= Max 가 보장되고 Max 의 ClampMax 는 2e9 < INT32_MAX 이므로
		// 이 시점의 캐스트는 안전하다.
		OutQty = (int32)V;
		return true;
	}

	bool IsValidItemId(const FString& Id, int32 MaxLen)
	{
		return !Id.IsEmpty() && Id.Len() <= FMath::Max(1, MaxLen);
	}
}
