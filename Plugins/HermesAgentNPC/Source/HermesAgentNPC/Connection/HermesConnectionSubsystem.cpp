#include "Connection/HermesConnectionSubsystem.h"
#include "Connection/HermesUtil.h"
#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesMessages.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/MoveToActionHandler.h"
#include "Actions/FollowPlayerActionHandler.h"
#include "Actions/InventoryActionHandler.h"
#include "Actions/ItemTransferActionHandler.h"
#include "HermesSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Settings/HermesSettings.h"
#include "Transport/HermesWorkerConfig.h"

void UHermesConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	PlayerId = LoadOrCreatePlayerId();

	Dispatcher = NewObject<UHermesActionDispatcher>(this);

	// 워커는 전용 스레드에서 돌므로 UObject를 넘기지 않고 값만 복사해 전달한다.
	FHermesWorkerConfig Cfg;
	Settings->GetResolvedEndpoint(Cfg.Host, Cfg.Port);
	Cfg.InitialReconnectDelay = Settings->InitialReconnectDelay;
	Cfg.MaxReconnectDelay     = Settings->MaxReconnectDelay;
	Cfg.MaxInboundQueueSize   = Settings->MaxInboundQueueSize;
	Cfg.MaxOutboundQueueSize  = Settings->MaxOutboundQueueSize;

	Worker = MakeUnique<FHermesSocketWorker>(Cfg);
	Worker->Start();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHermesConnectionSubsystem::Tick), 0.f);
}

void UHermesConnectionSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	if (Worker)
	{
		Worker->RequestStop();
		Worker.Reset(); // 소멸자에서 스레드 Stop/Kill 처리
	}
	Super::Deinitialize();
}

FString UHermesConnectionSubsystem::LoadOrCreatePlayerId()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		if (!SG->PlayerId.IsEmpty()) return SG->PlayerId;
	}
	UHermesSaveGame* NewSG = Cast<UHermesSaveGame>(UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	NewSG->PlayerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	UGameplayStatics::SaveGameToSlot(NewSG, SaveSlot, 0);
	return NewSG->PlayerId;
}

void UHermesConnectionSubsystem::RegisterNpc(AHermesNPCCharacter* Npc)
{
	if (!Npc || !Dispatcher) return;

	if (ActiveNpc.IsValid() && ActiveNpc.Get() != Npc)
	{
		// 레벨에 NPC 를 둘 이상 배치했을 때 조용히 이상해지는 것을 막는다.
		// 어느 쪽이 대상인지 로그로 분명히 남긴다.
		UE_LOG(LogTemp, Warning,
			TEXT("[Hermes] 활성 NPC 교체: %s -> %s. 이 플러그인은 NPC 한 명만 대상으로 한다."),
			*ActiveNpc->GetName(), *Npc->GetName());
	}

	// 옛 핸들러를 남기면 Dispatch 의 first-match 규칙 때문에 액션이 계속
	// 옛 NPC 로 가고 새 NPC 는 아무것도 받지 못한다.
	Dispatcher->ResetHandlers();

	UMoveToActionHandler* H1 = NewObject<UMoveToActionHandler>(this); H1->Init(Npc); Dispatcher->RegisterHandler(H1);
	UFollowPlayerActionHandler* H2 = NewObject<UFollowPlayerActionHandler>(this); H2->Init(Npc); Dispatcher->RegisterHandler(H2);
	UInventoryActionHandler* H3 = NewObject<UInventoryActionHandler>(this); H3->Init(Npc); Dispatcher->RegisterHandler(H3);
	UItemTransferActionHandler* H4 = NewObject<UItemTransferActionHandler>(this); H4->Init(Npc); Dispatcher->RegisterHandler(H4);

	ActiveNpc = Npc;
}

void UHermesConnectionSubsystem::UnregisterNpc(AHermesNPCCharacter* Npc)
{
	// 활성이 아닌 NPC 가 사라지는 것은 현재 배선과 무관하다. 여기서 무조건
	// 비우면 다른 NPC 의 EndPlay 가 활성 NPC 의 배선을 끊어버린다.
	if (!Npc || ActiveNpc.Get() != Npc) return;

	if (Dispatcher) Dispatcher->ResetHandlers();
	ActiveNpc.Reset();
}

void UHermesConnectionSubsystem::SendJson(const FString& Json)
{
	if (Worker) Worker->EnqueueOutbound(Json);
}

void UHermesConnectionSubsystem::SendIdentify()
{
	SendJson(HermesJson::MakeIdentify(PlayerId, FString()));
}

void UHermesConnectionSubsystem::SendChat(const FString& Text)
{
	const FString Id = FString::Printf(TEXT("c-%04d"), ++ChatCounter);
	const FString Json = HermesJson::MakeChat(Id, Text);

	if (bIdentified)
	{
		SendJson(Json);
		return;
	}

	// identified 를 영영 보내지 않는 피어에 붙으면 여기가 무한히 자란다.
	const int32 Dropped = HermesUtil::PushBounded(
		PendingChats, Json, GetDefault<UHermesSettings>()->MaxPendingChats);
	if (Dropped > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] pending chat overflow, dropped %d oldest"), Dropped);
	}
}

void UHermesConnectionSubsystem::FlushPendingChats()
{
	for (const FString& J : PendingChats) SendJson(J);
	PendingChats.Reset();
}

bool UHermesConnectionSubsystem::Tick(float DeltaTime)
{
	if (!Worker) return true;

	// 연결 엣지 감지: 새로 연결되면 재-identify
	const bool bNow = Worker->IsConnected();
	if (bNow && !bWasConnected)
	{
		bIdentified = false;
		SendIdentify();
	}
	if (!bNow && bWasConnected)
	{
		bIdentified = false;
		OnConnectionStateChanged.Broadcast(false);
	}
	bWasConnected = bNow;

	// 한 틱이 무한정 길어지지 않게 예산을 둔다. 남은 프레임은 다음 틱에서
	// 처리되고, 유입이 예산을 계속 초과하면 워커의 큐 상한이 연결을 끊는다.
	int32 Budget = GetDefault<UHermesSettings>()->MaxInboundFramesPerTick;
	FString Json;
	while (Budget-- > 0 && Worker->DequeueInbound(Json))
	{
		TSharedPtr<FJsonObject> Obj;
		if (HermesJson::Parse(Json, Obj)) HandleFrame(Obj);
	}
	return true; // 계속 틱
}

void UHermesConnectionSubsystem::HandleFrame(const TSharedPtr<FJsonObject>& Obj)
{
	FString Type;
	if (!Obj->TryGetStringField(TEXT("type"), Type)) return;

	if (Type == HermesMsg::Identified)
	{
		bIdentified = true;
		FlushPendingChats();
		OnConnectionStateChanged.Broadcast(true);
	}
	else if (Type == HermesMsg::ChatResponse)
	{
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		OnChatResponse.Broadcast(Text, Id);
	}
	else if (Type == HermesMsg::ActionRequest)
	{
		FHermesActionPayload P;
		Obj->TryGetStringField(TEXT("id"), P.Id);
		Obj->TryGetStringField(TEXT("command"), P.Command);
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("params"), ParamsObj)) P.Params = *ParamsObj;
		else P.Params = MakeShared<FJsonObject>();

		Dispatcher->Dispatch(P, [this](const FString& ResultJson){ SendJson(ResultJson); });
	}
	else if (Type == HermesMsg::Ping)
	{
		FString Id; Obj->TryGetStringField(TEXT("id"), Id);
		SendJson(HermesJson::MakePong(Id));
	}
	else if (Type == HermesMsg::Error)
	{
		FString Code, Msg;
		Obj->TryGetStringField(TEXT("code"), Code);
		Obj->TryGetStringField(TEXT("message"), Msg);
		UE_LOG(LogTemp, Warning, TEXT("[Hermes] error %s: %s"), *Code, *Msg);
	}
}
