#include "Connection/HermesConnectionSubsystem.h"
#include "Connection/HermesUtil.h"
#include "Connection/HermesLiveness.h"
#include "HAL/PlatformTime.h"
#include "Transport/HermesSocketWorker.h"
#include "Protocol/HermesMessages.h"
#include "Actions/HermesActionDispatcher.h"
#include "Actions/MoveToActionHandler.h"
#include "Actions/FollowPlayerActionHandler.h"
#include "Actions/InventoryActionHandler.h"
#include "Actions/ItemTransferActionHandler.h"
#include "HermesSaveGame.h"
#include "HermesLog.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Settings/HermesSettings.h"
#include "Transport/HermesWorkerConfig.h"

void UHermesConnectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UHermesSettings* Settings = GetDefault<UHermesSettings>();

	LoadCredentials();

	// PeerTimeout 이 PingInterval 에 비해 너무 짧으면 정상 연결이 죽은 것으로 오판된다.
	// 값을 강제로 교정하지는 않는다 — 설정자의 의도를 덮어쓰기보다 문제를 드러낸다.
	if (Settings->PeerTimeoutSeconds < Settings->KeepAlivePingIntervalSeconds * 2.f)
	{
		UE_LOG(LogHermes, Warning,
			TEXT("PeerTimeoutSeconds (%.1f) should be at least 2x "
			     "KeepAlivePingIntervalSeconds (%.1f); false disconnects are likely"),
			Settings->PeerTimeoutSeconds, Settings->KeepAlivePingIntervalSeconds);
	}

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

void UHermesConnectionSubsystem::LoadCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	// 신원은 서버가 발급한다. 여기서 만들어내지 않는다 — 클라이언트가 자기
	// 신원을 주장하는 구조면 임의 UUID 로 남의 세션에 접근할 수 있다.
	if (UHermesSaveGame* SG = Cast<UHermesSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		PlayerId     = SG->PlayerId;
		SessionToken = SG->GetSessionToken();   // 난독화 해제

		// 토큰만 손상되었다면 반쪽 자격 증명으로 접속을 시도하지 않는다.
		// MakeIdentify 가 둘 다 있어야 실어 보내므로 자연히 재발급 경로가 된다.
		if (PlayerId.IsEmpty() || SessionToken.IsEmpty())
		{
			PlayerId.Reset();
			SessionToken.Reset();
		}
	}
}

void UHermesConnectionSubsystem::SaveCredentials()
{
	const FString SaveSlot = GetDefault<UHermesSettings>()->SaveSlotName;

	UHermesSaveGame* SG = Cast<UHermesSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UHermesSaveGame::StaticClass()));
	if (!SG)
	{
		UE_LOG(LogHermes, Warning, TEXT("failed to create save game object"));
		return;
	}
	SG->PlayerId = PlayerId;
	SG->SetSessionToken(SessionToken);   // 난독화해 보관

	if (!UGameplayStatics::SaveGameToSlot(SG, SaveSlot, 0))
	{
		// 다음 실행에서 새 신원을 발급받게 된다. 데이터 손실이지 보안 결함은 아니다.
		UE_LOG(LogHermes, Warning, TEXT("failed to save credentials to slot '%s'"), *SaveSlot);
	}
}

void UHermesConnectionSubsystem::RegisterNpc(AHermesNPCCharacter* Npc)
{
	if (!Npc || !Dispatcher) return;

	if (ActiveNpc.IsValid() && ActiveNpc.Get() != Npc)
	{
		// 레벨에 NPC 를 둘 이상 배치했을 때 조용히 이상해지는 것을 막는다.
		// 어느 쪽이 대상인지 로그로 분명히 남긴다.
		UE_LOG(LogHermes, Warning,
			TEXT("활성 NPC 교체: %s -> %s. 이 플러그인은 NPC 한 명만 대상으로 한다."),
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
	if (Worker)
	{
		Worker->EnqueueOutbound(Json);
		LastSendTime = FPlatformTime::Seconds();
	}
}

void UHermesConnectionSubsystem::SendIdentify()
{
	// 자격 증명이 없으면 MakeIdentify 가 신규 발급 요청 형태로 만든다.
	SendJson(HermesJson::MakeIdentify(PlayerId, SessionToken, FString()));
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
		UE_LOG(LogHermes, Warning, TEXT("pending chat overflow, dropped %d oldest"), Dropped);
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

	// 연결 엣지 감지: 새로 연결되면 재-identify 하고 생존 타이머를 초기화한다.
	const bool bNow = Worker->IsConnected();
	const double NowSeconds = FPlatformTime::Seconds();

	if (bNow && !bWasConnected)
	{
		bIdentified  = false;
		LastRecvTime = NowSeconds;   // 초기화하지 않으면 연결 직후 즉시 사망 판정이 난다
		LastSendTime = NowSeconds;
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

	// 연결이 성립한 동안에만 평가한다. 재연결 대기 중에는 수신이 없는 것이 정상이다.
	if (bNow)
	{
		const UHermesSettings* Settings = GetDefault<UHermesSettings>();
		const HermesLiveness::EDecision D = HermesLiveness::Evaluate(
			NowSeconds, LastRecvTime, LastSendTime,
			Settings->KeepAlivePingIntervalSeconds, Settings->PeerTimeoutSeconds);

		if (D == HermesLiveness::EDecision::SendPing)
		{
			SendJson(HermesJson::MakePing(FString::Printf(TEXT("k-%04d"), ++PingCounter)));
		}
		else if (D == HermesLiveness::EDecision::DeclareDead)
		{
			// 조용히 끊긴 연결은 다음 송신 시점까지 드러나지 않는다. 송신은 플레이어가
			// 말을 걸 때 일어나므로, 그때 재연결이 시작되면 가장 나쁜 타이밍이 된다.
			UE_LOG(LogHermes, Warning,
				TEXT("peer silent for %.0fs, treating connection as dead"),
				Settings->PeerTimeoutSeconds);
			Worker->RequestReconnect();
		}
	}
	return true; // 계속 틱
}

void UHermesConnectionSubsystem::HandleFrame(const TSharedPtr<FJsonObject>& Obj)
{
	// 종류를 가리지 않고 모든 수신이 생존 신호다.
	LastRecvTime = FPlatformTime::Seconds();

	FString Type;
	if (!Obj->TryGetStringField(TEXT("type"), Type)) return;

	if (Type == HermesMsg::Identified)
	{
		FString NewPid, NewTok, ChatId;
		if (!HermesJson::ParseIdentified(Obj, NewPid, NewTok, ChatId))
		{
			// v1 호환 모드를 두지 않는다. 호환 모드는 곧 "인증 없이도 동작하는
			// 경로"라 신원 발급의 목적을 무력화한다. 조용히 동작하느니 크게 실패한다.
			UE_LOG(LogHermes, Error,
				TEXT("server did not return session credentials. "
				     "This client requires protocol v2. Closing connection."));
			if (Worker)
			{
				Worker->RequestReconnect();
			}
			return;
		}

		// 최초 발급이거나 서버가 값을 바꿔 준 경우에만 저장한다.
		if (NewPid != PlayerId || NewTok != SessionToken)
		{
			PlayerId     = NewPid;
			SessionToken = NewTok;
			SaveCredentials();
		}

		bIdentified = true;
		FlushPendingChats();
		OnConnectionStateChanged.Broadcast(true);
	}
	else if (Type == HermesMsg::ChatDelta)
	{
		// seq 는 읽지 않는다. 델타는 표시용이고 최종 텍스트가 정본이라 순번 검증이
		// 없어도 화면이 자기 교정된다. 순번을 쓰기 시작하면 유실·재정렬 처리 로직이
		// 따라붙는데 그만한 이득이 없다.
		FString Text, Id;
		Obj->TryGetStringField(TEXT("text"), Text);
		Obj->TryGetStringField(TEXT("id"), Id);
		OnChatDelta.Broadcast(Text, Id);
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
		UE_LOG(LogHermes, Warning, TEXT("server error %s: %s"), *Code, *Msg);
	}
}
