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
	if (bAutoRegisterAsActiveNpc)
	{
		BecomeActiveHermesNpc();
	}
}

void AHermesNPCCharacter::BecomeActiveHermesNpc()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>())
		{
			Conn->RegisterNpc(this);
		}
	}
}

void AHermesNPCCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 대화 중에 NPC 가 사라지면 입력이 UI 에 묶인 채로 남는다. 되돌려 준다.
	if (DialogueWidget && DialogueWidget->IsOpen())
	{
		DialogueWidget->Close();
	}

	// 파괴된 NPC 를 가리키는 핸들러가 남으면 액션이 죽은 액터로 향한다.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UHermesConnectionSubsystem* Conn = GI->GetSubsystem<UHermesConnectionSubsystem>())
		{
			Conn->UnregisterNpc(this);
		}
	}
	Super::EndPlay(EndPlayReason);
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
	if (!DialogueWidget)
	{
		return;
	}

	// 토글로 둔다. 대화 중에는 입력이 UI 로 가므로 이 경로가 닿지 않지만,
	// 게임이 다른 입력 모드를 쓰거나 블루프린트가 직접 부를 때를 위해 남긴다.
	if (DialogueWidget->IsOpen())
	{
		DialogueWidget->Close();
		return;
	}
	DialogueWidget->OpenFor(Conn);
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
