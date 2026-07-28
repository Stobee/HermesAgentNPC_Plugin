#include "Connection/HermesConnectionSubsystem.h"
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
	UMoveToActionHandler* H1 = NewObject<UMoveToActionHandler>(this); H1->Init(Npc); Dispatcher->RegisterHandler(H1);
	UFollowPlayerActionHandler* H2 = NewObject<UFollowPlayerActionHandler>(this); H2->Init(Npc); Dispatcher->RegisterHandler(H2);
	UInventoryActionHandler* H3 = NewObject<UInventoryActionHandler>(this); H3->Init(Npc); Dispatcher->RegisterHandler(H3);
	UItemTransferActionHandler* H4 = NewObject<UItemTransferActionHandler>(this); H4->Init(Npc); Dispatcher->RegisterHandler(H4);
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
	if (bIdentified) SendJson(Json);
	else PendingChats.Add(Json); // identified 후 flush
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

	FString Json;
	while (Worker->DequeueInbound(Json))
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
