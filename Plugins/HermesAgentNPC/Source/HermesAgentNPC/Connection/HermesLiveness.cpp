#include "Connection/HermesLiveness.h"

namespace HermesLiveness
{
	EDecision Evaluate(double Now, double LastRecvTime, double LastSendTime,
		float PingInterval, float PeerTimeout)
	{
		// 죽은 연결에 ping 을 보내봐야 의미가 없으므로 사망 판정을 먼저 한다.
		if (Now - LastRecvTime >= (double)PeerTimeout)
		{
			return EDecision::DeclareDead;
		}
		if (Now - LastSendTime >= (double)PingInterval)
		{
			return EDecision::SendPing;
		}
		return EDecision::Nothing;
	}
}
