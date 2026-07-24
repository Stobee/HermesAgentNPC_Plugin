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
};
