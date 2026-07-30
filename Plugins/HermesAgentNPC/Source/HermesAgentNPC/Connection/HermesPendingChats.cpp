#include "Connection/HermesPendingChats.h"

void FHermesPendingChats::Add(const FString& Id, double Now)
{
	LastProgress.Add(Id, Now);
}

void FHermesPendingChats::Touch(const FString& Id, double Now)
{
	// 추적 중이 아닌 Id 로 항목을 만들지 않는다. 이미 완료·타임아웃된 발화의
	// 늦은 델타가 새 추적을 시작하면 영영 만료되지 않는 항목이 생긴다.
	if (double* Found = LastProgress.Find(Id))
	{
		*Found = Now;
	}
}

void FHermesPendingChats::Remove(const FString& Id)
{
	LastProgress.Remove(Id);
}

bool FHermesPendingChats::FailById(const FString& Id)
{
	// 추적 중이 아닌 id 는 아무 턴도 건드리지 않는다. 이미 완료·타임아웃된
	// 발화에 대한 늦은 에러가 무관한 턴을 끊는 것을 막는다.
	return LastProgress.Remove(Id) > 0;
}

void FHermesPendingChats::CollectTimedOut(double Now, float Timeout, TArray<FString>& Out)
{
	for (auto It = LastProgress.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() >= (double)Timeout)
		{
			Out.Add(It.Key());
			// 같은 발화가 매 틱 반복 통지되지 않도록 수집과 동시에 제거한다.
			It.RemoveCurrent();
		}
	}
}

void FHermesPendingChats::Clear()
{
	LastProgress.Reset();
}
