#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HermesNPCCharacter.generated.h"

class UHermesInventoryComponent;

UCLASS()
class AHermesNPCCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	AHermesNPCCharacter();

	UHermesInventoryComponent* GetInventory() const { return Inventory; }
	void SetFollowPlayer(bool bEnabled) { bFollowing = bEnabled; }
	bool IsFollowing() const { return bFollowing; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category="Hermes")
	void Interact();

	/**
	 * 이 액터를 에이전트가 조종할 NPC 로 지정한다. 플러그인은 NPC 한 명만
	 * 다루므로, 레벨에 후보가 여럿이면 이 함수를 부른 마지막 액터가 대상이 된다.
	 */
	UFUNCTION(BlueprintCallable, Category="Hermes")
	void BecomeActiveHermesNpc();

protected:
	/**
	 * BeginPlay 에서 자동으로 활성 NPC 가 될지 여부. 레벨에 이 클래스의 액터를
	 * 여럿 배치한다면 하나만 켜 두고 나머지는 꺼야 한다. 끈 액터는 나중에
	 * BecomeActiveHermesNpc() 로 직접 지정할 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hermes")
	bool bAutoRegisterAsActiveNpc = true;

	UPROPERTY(EditDefaultsOnly, Category="Hermes")
	TSubclassOf<class UHermesDialogueWidget> DialogueWidgetClass;

private:
	UPROPERTY()
	UHermesDialogueWidget* DialogueWidget = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Hermes")
	UHermesInventoryComponent* Inventory;

	bool bFollowing = false;
	float FollowRepathAccum = 0.f;
};
