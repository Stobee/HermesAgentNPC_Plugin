#include "NPC/HermesNPCCharacter.h"
#include "NPC/HermesNPCAIController.h"
#include "Inventory/HermesInventoryComponent.h"
#include "UI/HermesDialogueWidget.h"
#include "Connection/HermesConnectionSubsystem.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"
#include "Settings/HermesSettings.h"

AHermesNPCCharacter::AHermesNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AHermesNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Inventory = CreateDefaultSubobject<UHermesInventoryComponent>(TEXT("Inventory"));
}

void AHermesNPCCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>())
		{
			Conn->RegisterNpc(this);
		}
	}
}

void AHermesNPCCharacter::Interact()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI || !DialogueWidgetClass) return;
	UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>();
	if (!Conn) return;
	if (!DialogueWidget)
	{
		DialogueWidget = CreateWidget<UHermesDialogueWidget>(GI, DialogueWidgetClass);
	}
	if (DialogueWidget)
	{
		DialogueWidget->OpenFor(Conn);
	}
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
		AI->MoveToActor(Player, GetDefault<UHermesSettings>()->FollowDistance);
	}
}
