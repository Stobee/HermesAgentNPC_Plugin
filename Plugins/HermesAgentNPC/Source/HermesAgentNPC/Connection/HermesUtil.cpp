#include "Connection/HermesUtil.h"

namespace HermesUtil
{
	int32 PushBounded(TArray<FString>& Array, const FString& Item, int32 MaxNum)
	{
		Array.Add(Item);

		const int32 Cap = FMath::Max(1, MaxNum);
		int32 Dropped = 0;
		while (Array.Num() > Cap)
		{
			// 대화 맥락상 최신 발화가 살아남는 편이 자연스럽다.
			Array.RemoveAt(0, 1, EAllowShrinking::No);
			++Dropped;
		}
		return Dropped;
	}
}
