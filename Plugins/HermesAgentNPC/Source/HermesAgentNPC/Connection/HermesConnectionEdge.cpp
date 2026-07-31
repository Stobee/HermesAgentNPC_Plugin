#include "Connection/HermesConnectionEdge.h"

namespace HermesConnectionEdge
{
	EDecision Evaluate(bool bWasConnected, uint32 SeenGeneration,
	                   bool bNowConnected, uint32 CurrentGeneration)
	{
		if (bNowConnected && !bWasConnected)
		{
			return EDecision::Opened;
		}
		if (!bNowConnected && bWasConnected)
		{
			return EDecision::Closed;
		}

		// 여기부터는 불린이 그대로다. 세대가 올라갔다면 두 폴링 사이에 연결이
		// 교체된 것이다. 붙어 있는 쪽만 재연결로 다룬다 — 끊긴 채로 세대만
		// 올랐다면 이미 사라진 연결이라 할 일이 없다.
		if (bNowConnected && CurrentGeneration != SeenGeneration)
		{
			return EDecision::Reopened;
		}
		return EDecision::Nothing;
	}
}
