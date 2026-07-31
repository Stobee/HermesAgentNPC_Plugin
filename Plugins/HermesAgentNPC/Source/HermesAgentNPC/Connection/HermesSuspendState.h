#pragma once
#include "CoreMinimal.h"

/**
 * 재연결 정지 상태와 재개 승인 판정.
 *
 * 사다리를 올리는 것과 내리는 것을 한곳에서 다룬다. 핵심 규칙은 하나다 —
 * **탈취로 끝난 연결은 사다리를 되돌리지 않는다.** 되돌리는 것은 정지 없이
 * 끝난 건강한 연결뿐이다.
 *
 * 그래야 eviction war 가 수렴한다. 싸우는 동안 이쪽 연결은 항상 탈취로
 * 끝나므로 사다리가 계속 오르고, 결국 한쪽이 오래 기다리게 되어 다른 쪽이
 * 이긴다. 의미도 분명하다 — 걷어차인 것은 재시도 권한을 벌어주지 않는다.
 *
 * 현재 시각을 인자로 받는다. 전역 시계를 읽지 않으므로 단독 테스트가 가능하다.
 */
struct FHermesSuspendState
{
	/** 새 연결이 성립했다. 이 연결에서 정지가 있었는지를 다시 센다. */
	void NoteConnectionOpened();

	/** 종료성 에러로 정지되었다. 사다리를 한 칸 올린다. */
	void NoteSuspended(double NowSeconds);

	/**
	 * 연결이 끝났다. 정지 없이 끝났고 충분히 살아 있었으면 사다리를 되돌린다.
	 * LifetimeSeconds 는 그 연결이 유지된 시간이다.
	 */
	void NoteConnectionClosed(double LifetimeSeconds, float HealthySeconds);

	bool IsSuspended() const { return bSuspended; }

	/** 지금 재개가 거부될 때 남은 대기(초). 정지 상태가 아니면 0. */
	float CooldownRemaining(double NowSeconds, float Initial, float Max) const;

	/**
	 * 재개를 시도한다. 승인되면 정지를 풀고 true.
	 * 정지 상태가 아니거나 쿨다운 중이면 아무것도 하지 않고 false.
	 */
	bool TryResume(double NowSeconds, float Initial, float Max);

	/**
	 * 모든 상태를 초기값으로 되돌린다. 소유자(연결 서브시스템 등)가 사라질 때 쓴다.
	 * 소유자가 사라진 뒤에도 남은 참조가 질의를 보내면, 초기화하지 않은 경우
	 * "정지 중"이 영원히 고정된 답으로 남는다 — Worker 가 이미 없어 재개할
	 * 방법도 없는데 정지만 계속 보고하는 것은 null-safety 축에서 퇴행이다.
	 */
	void Reset();

private:
	/** 연속 정지 횟수. 첫 정지가 1. */
	int32 ConsecutiveSuspends = 0;
	/** 마지막으로 정지된 시각. 대기는 이 시점부터 잰다. */
	double SuspendedAt = 0.0;
	bool bSuspended = false;
	/** 지금 연결에서 정지가 있었는가. 사다리를 되돌릴지 가르는 값이다. */
	bool bSuspendedDuringThisConnection = false;
};
