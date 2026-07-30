#pragma once
#include "CoreMinimal.h"

/**
 * 진행 중인 발화 추적.
 * 시간을 인자로 주입받는 순수 클래스라 단독 테스트가 가능하다.
 *
 * 델타 수신이 타이머를 갱신하므로 긴 생성은 살아남고 멈춘 생성만 잡힌다.
 * 스트리밍이 이 판정을 정확하게 만들어 주는 지점이다.
 */
class FHermesPendingChats
{
public:
	/** 발화를 추적 목록에 넣는다. 같은 Id 가 있으면 시각만 갱신된다. */
	void Add(const FString& Id, double Now);

	/** 진행 신호(델타 수신). 추적 중이 아닌 Id 는 무시한다. */
	void Touch(const FString& Id, double Now);

	/** 최종 응답 수신. 추적 목록에서 제거한다. */
	void Remove(const FString& Id);

	/**
	 * 타임아웃이 아닌 경로로 턴을 실패시킨다(서버 error 프레임 등).
	 * 추적 중이던 발화였으면 목록에서 제거하고 true 를 반환한다.
	 *
	 * 반환값으로 실제 추적 여부를 알려야 호출자가 없는 턴에 대해 실패를
	 * 통지하지 않을 수 있다. 같은 id 를 두 번 실패시키면 두 번째는 false 다.
	 */
	bool FailById(const FString& Id);

	/**
	 * 마지막 진행으로부터 Timeout 이 지난 항목을 Out 에 담고 목록에서 제거한다.
	 * 제거까지 하는 이유는 같은 발화가 매 틱 반복 통지되는 것을 막기 위함이다.
	 */
	void CollectTimedOut(double Now, float Timeout, TArray<FString>& Out);

	/** 추적 목록을 비운다. 연결이 끊길 때 호출한다. */
	void Clear();

	int32 Num() const { return LastProgress.Num(); }

private:
	TMap<FString, double> LastProgress;
};
