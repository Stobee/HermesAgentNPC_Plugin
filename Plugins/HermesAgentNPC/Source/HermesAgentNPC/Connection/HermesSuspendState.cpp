#include "Connection/HermesSuspendState.h"
#include "Transport/HermesResumePolicy.h"
#include "Math/UnrealMathUtility.h"

void FHermesSuspendState::NoteConnectionOpened()
{
	bSuspendedDuringThisConnection = false;
}

void FHermesSuspendState::NoteSuspended(double NowSeconds)
{
	++ConsecutiveSuspends;
	SuspendedAt = NowSeconds;
	bSuspended = true;
	bSuspendedDuringThisConnection = true;
}

void FHermesSuspendState::NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds)
{
	// 탈취로 끝난 연결은 되돌리지 않는다. 걷어차인 것은 재시도 권한을
	// 벌어주지 않는다 — 이 한 줄이 eviction war 를 수렴시킨다.
	if (bSuspendedDuringThisConnection)
	{
		return;
	}
	if (LifetimeSeconds >= static_cast<double>(HealthySeconds))
	{
		ConsecutiveSuspends = 0;
	}
}

float FHermesSuspendState::CooldownRemaining(double NowSeconds, float Initial, float Max) const
{
	if (!bSuspended)
	{
		return 0.f;
	}
	const float Required = HermesResumePolicy::RequiredCooldown(
		ConsecutiveSuspends, Initial, Max);
	const double Elapsed = NowSeconds - SuspendedAt;
	return FMath::Clamp(Required - static_cast<float>(Elapsed), 0.f, Required);
}

bool FHermesSuspendState::TryResume(double NowSeconds, float Initial, float Max)
{
	if (!bSuspended)
	{
		return false;
	}
	if (CooldownRemaining(NowSeconds, Initial, Max) > 0.f)
	{
		return false;
	}
	bSuspended = false;
	return true;
}

void FHermesSuspendState::Reset()
{
	ConsecutiveSuspends = 0;
	SuspendedAt = 0.0;
	bSuspended = false;
	bSuspendedDuringThisConnection = false;
}
