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

	UFUNCTION(BlueprintCallable, Category="Hermes")
	void Interact();

protected:
	UPROPERTY(EditDefaultsOnly, Category="Hermes")
	TSubclassOf<class UHermesDialogueWidget> DialogueWidgetClass;

private:
	UPROPERTY()
	UHermesDialogueWidget* DialogueWidget = nullptr;

	UPROPERTY(VisibleAnywhere)
	UHermesInventoryComponent* Inventory;

	bool bFollowing = false;
	float FollowRepathAccum = 0.f;
};
