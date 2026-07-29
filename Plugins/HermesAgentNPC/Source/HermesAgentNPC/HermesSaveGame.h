#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "HermesSaveGame.generated.h"

UCLASS()
class HERMESAGENTNPC_API UHermesSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FString PlayerId;

	/**
	 * 서버가 발급한 세션 자격 증명을 난독화해 보관한다.
	 *
	 * FAES 로 감싸지만 키가 바이너리 안에 있으므로 이것은 암호화가 아니라
	 * 난독화다. 기기 소유자로부터는 보호되지 않으며 그럴 방법도 없다.
	 * 막는 것은 "세이브 파일을 그대로 복사해 남에게 넘기는" 수준까지다.
	 * 진짜 대책은 서버측 단기 토큰·세션 바인딩이며 설계 문서 10.2 에 있다.
	 *
	 * 평문 접근은 GetSessionToken()/SetSessionToken() 으로만 한다.
	 */
	UPROPERTY()
	TArray<uint8> ObfuscatedSessionToken;

	/** 난독화를 풀어 평문 토큰을 돌려준다. 없으면 빈 문자열. */
	FString GetSessionToken() const;

	/** 평문 토큰을 난독화해 보관한다. 빈 문자열이면 저장분을 비운다. */
	void SetSessionToken(const FString& PlainToken);
};
