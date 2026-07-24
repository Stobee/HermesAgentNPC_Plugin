#include "NPC/HermesNPCCharacter.h"
#include "NPC/HermesNPCAIController.h"
#include "Inventory/HermesInventoryComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"

AHermesNPCCharacter::AHermesNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AHermesNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Inventory = CreateDefaultSubobject<UHermesInventoryComponent>(TEXT("Inventory"));
}

void AHermesNPCCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFollowing)
	{
		return;
	}

	FollowRepathAccum += DeltaSeconds;
	if (FollowRepathAccum < 0.25f) // 0.25초마다 경로 갱신
	{
		return;
	}
	FollowRepathAccum = 0.f;

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	AAIController* AI = Cast<AAIController>(GetController());
	if (Player && AI)
	{
		AI->MoveToActor(Player, 150.f); // 150cm 근접 유지
	}
}
