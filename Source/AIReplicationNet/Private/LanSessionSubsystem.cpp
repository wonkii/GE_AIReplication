#include "LanSessionSubsystem.h"

#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	const FName ProjectKey(TEXT("GE_LAN_PROTOCOL"));
	const FString Protocol(TEXT("GE_AIReplication_LAN_v1"));
	constexpr double OperationSeconds = 20.0;
	constexpr double VerificationSeconds = 30.0;

	// Accept numeric IPv4 only. Never pass arbitrary travel options or map paths from UI.
	bool ParseAddress(FString Text, FString& Out)
	{
		Text.TrimStartAndEndInline();
		for (TCHAR Character : Text)
			if ((Character < TEXT('0') || Character > TEXT('9')) && Character != TEXT('.') && Character != TEXT(':')) return false;
		if (!Text.Contains(TEXT(":"))) Text += TEXT(":7777");
		FString Host, Port;
		if (!Text.Split(TEXT(":"), &Host, &Port) || Port.IsEmpty() || Port.Len() > 5) return false;
		TArray<FString> Octets;
		Host.ParseIntoArray(Octets, TEXT("."), false);
		if (Octets.Num() != 4) return false;
		for (const FString& Octet : Octets) if (Octet.IsEmpty() || Octet.Len() > 3) return false;
		FIPv4Endpoint Endpoint;
		if (!FIPv4Endpoint::Parse(Text, Endpoint) || Endpoint.Port == 0 ||
			Endpoint.Address.Value == 0 || Endpoint.Address.Value == MAX_uint32 ||
			Endpoint.Address.IsMulticastAddress()) return false;
		Out = Endpoint.ToString();
		return true;
	}

	class SLanMenu : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SLanMenu) {} SLATE_END_ARGS()
		void Construct(const FArguments&, ULanSessionSubsystem* InOwner)
		{
			Owner = InOwner;
			ChildSlot
			.HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(24)
			[
				SNew(SBox).WidthOverride(620)
				[
					SNew(SBorder).Padding(20)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
						[ SNew(STextBlock).Text(FText::FromString(TEXT("LAN 서버 / 클라이언트"))) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
						[ SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Owner.IsValid() ? Owner->GetStatus() : FString()); }) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
						[ SNew(SButton).Text(FText::FromString(TEXT("Host: 서버 열기")))
							.IsEnabled_Lambda([this] { return Owner.IsValid() && Owner->GetState() == ELanSessionState::Idle; })
							.OnClicked_Lambda([this] { if (Owner.IsValid()) Owner->HostServer(); return FReply::Handled(); }) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
						[ SNew(SEditableTextBox).IsReadOnly(true).Text_Lambda([this] { return FText::FromString(Owner.IsValid() ? Owner->GetHostAddresses() : FString()); })
							.HintText(FText::FromString(TEXT("서버를 열면 공유할 IPv4:포트가 표시됩니다."))) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
						[ SAssignNew(Address, SEditableTextBox).HintText(FText::FromString(TEXT("Client: Host가 알려준 IPv4:포트 입력")))
							.IsEnabled_Lambda([this] { return Owner.IsValid() && (Owner->GetState() == ELanSessionState::Idle || Owner->GetState() == ELanSessionState::Verified); })
							.OnTextChanged_Lambda([this](const FText&) { if (Owner.IsValid()) Owner->InvalidateVerification(); }) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
						[ SNew(SButton).Text(FText::FromString(TEXT("LAN에서 주소 확인")))
							.IsEnabled_Lambda([this] { return Owner.IsValid() && (Owner->GetState() == ELanSessionState::Idle || Owner->GetState() == ELanSessionState::Verified); })
							.OnClicked_Lambda([this] { if (Owner.IsValid()) Owner->VerifyAddress(Address->GetText().ToString()); return FReply::Handled(); }) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
						[ SNew(SButton).Text(FText::FromString(TEXT("서버 접속")))
							.IsEnabled_Lambda([this] { return Owner.IsValid() && Owner->CanConnect(); })
							.OnClicked_Lambda([this] { if (Owner.IsValid()) Owner->ConnectToServer(); return FReply::Handled(); }) ]
						+ SVerticalBox::Slot().AutoHeight()
						[ SNew(SButton).Text(FText::FromString(TEXT("서버 종료 / 나가기")))
							.IsEnabled_Lambda([this] { return Owner.IsValid() && (Owner->GetState() == ELanSessionState::Hosting || Owner->GetState() == ELanSessionState::Connected); })
							.OnClicked_Lambda([this] { if (Owner.IsValid()) Owner->LeaveServer(); return FReply::Handled(); }) ]
					]
				]
			];
		}
	private:
		TWeakObjectPtr<ULanSessionSubsystem> Owner;
		TSharedPtr<SEditableTextBox> Address;
	};
}

void ULanSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Status = TEXT("Host는 서버를 열어 주소를 공유하세요. Client는 주소를 확인한 뒤 서버 접속을 누르세요.");
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
	NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::NetworkFailure);
	TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::TravelFailure);
}

IOnlineSessionPtr ULanSessionSubsystem::Sessions() const
{
	return Online::GetSessionInterface(GetWorld());
}

void ULanSessionSubsystem::SetState(ELanSessionState NewState, const FString& Message)
{
	State = NewState;
	bMenuVisible = State != ELanSessionState::Connected;
	Status = Message;
	Deadline = (State == ELanSessionState::Creating || State == ELanSessionState::Searching ||
		State == ELanSessionState::Joining || State == ELanSessionState::Traveling || State == ELanSessionState::Leaving)
		? FPlatformTime::Seconds() + OperationSeconds : 0.0;
	UE_LOG(LogTemp, Display, TEXT("[LAN] State=%d %s"), static_cast<int32>(State), *Message);
	OnStateChanged.Broadcast(State);
}

void ULanSessionSubsystem::HostServer()
{
	if (State != ELanSessionState::Idle) return;
	UWorld* World = GetWorld();
	IOnlineSessionPtr Session = Sessions();
	if (!World || World->WorldType != EWorldType::Game || World->GetNetMode() != NM_Standalone || !Session.IsValid())
	{ SetState(ELanSessionState::Idle, TEXT("독립 실행 게임에서 LAN 서비스가 필요합니다.")); return; }
	if (Session->GetNamedSession(NAME_GameSession))
	{ Fail(TEXT("기존 세션을 정리합니다. 정리 후 다시 시도하세요.")); return; }

	// Listen first: NULL records the live NetDriver port when creating its advertisement.
	SetState(ELanSessionState::Creating, TEXT("LAN 서버를 여는 중..."));
	FURL URL = World->URL;
	URL.AddOption(TEXT("listen"));
	if (!World->Listen(URL)) { Fail(TEXT("게임 포트를 열지 못했습니다. 포트 사용 여부를 확인하세요.")); return; }
	bHost = true;
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.NumPublicConnections = 8;
	Settings.Set(ProjectKey, Protocol, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	CreateHandle = Session->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
	if (!Session->CreateSession(0, NAME_GameSession, Settings) && State == ELanSessionState::Creating) Fail(TEXT("LAN 세션 생성 요청에 실패했습니다."));
}

void ULanSessionSubsystem::Created(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || State != ELanSessionState::Creating) return;
	ClearOperationDelegates();
	if (!bSuccess) { Fail(TEXT("LAN 서버 광고에 실패했습니다.")); return; }
	FString Resolved;
	auto Session = Sessions();
	if (!Session.IsValid() || !Session->GetResolvedConnectString(NAME_GameSession, Resolved))
	{ Fail(TEXT("서버 주소를 확인하지 못했습니다.")); return; }
	HostAddresses = Resolved;
	SetState(ELanSessionState::Hosting, TEXT("서버가 열렸습니다. 같은 LAN의 주소를 Client에게 알려주세요. Host도 '서버 접속'을 눌러 플레이를 시작하세요."));
}

void ULanSessionSubsystem::InvalidateVerification()
{
	if (State == ELanSessionState::Verified)
	{
		VerifiedResult = FOnlineSessionSearchResult();
		VerifiedAddress.Empty();
		SetState(ELanSessionState::Idle, TEXT("주소가 변경되었습니다. LAN에서 다시 확인하세요."));
	}
}

void ULanSessionSubsystem::VerifyAddress(const FString& Address)
{
	if (State != ELanSessionState::Idle && State != ELanSessionState::Verified) return;
	InvalidateVerification();
	if (!ParseAddress(Address, RequestedAddress))
	{ SetState(ELanSessionState::Idle, TEXT("올바른 IPv4:포트를 입력하세요. 예: 192.168.0.10:7777")); return; }
	auto Session = Sessions();
	if (!Session.IsValid()) { SetState(ELanSessionState::Idle, TEXT("LAN 서비스를 사용할 수 없습니다.")); return; }
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 100;
	Search->TimeoutInSeconds = 5.0f;
	SetState(ELanSessionState::Searching, TEXT("같은 LAN에서 입력한 주소의 서버를 확인하는 중..."));
	FindHandle = Session->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::Found));
	if (!Session->FindSessions(0, Search.ToSharedRef()) && State == ELanSessionState::Searching) Fail(TEXT("LAN 검색 요청에 실패했습니다."));
}

void ULanSessionSubsystem::Found(bool bSuccess)
{
	if (State != ELanSessionState::Searching) return;
	ClearOperationDelegates();
	auto Session = Sessions();
	if (bSuccess && Search.IsValid() && Session.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			FString Tag, Address;
			if (Result.IsValid() && Result.Session.SessionSettings.Get(ProjectKey, Tag) && Tag == Protocol &&
				Session->GetResolvedConnectString(Result, NAME_GamePort, Address) && Address == RequestedAddress)
			{
				if (Result.Session.NumOpenPublicConnections <= 0)
				{ SetState(ELanSessionState::Idle, TEXT("서버가 가득 찼습니다.")); return; }
				VerifiedResult = Result;
				VerifiedAddress = Address;
				VerifiedAt = FPlatformTime::Seconds();
				SetState(ELanSessionState::Verified, TEXT("서버를 확인했습니다: ") + Address + TEXT(" — '서버 접속'을 누르세요."));
				return;
			}
		}
	}
	SetState(ELanSessionState::Idle, bSuccess ? TEXT("해당 주소의 호환 서버를 찾지 못했습니다. Host 주소와 LAN/방화벽을 확인하세요.") : TEXT("LAN 검색에 실패했습니다. 다시 시도하세요."));
}

bool ULanSessionSubsystem::CanConnect() const
{
	return State == ELanSessionState::Hosting || (State == ELanSessionState::Verified &&
		FPlatformTime::Seconds() - VerifiedAt < VerificationSeconds && VerifiedResult.IsValid());
}

void ULanSessionSubsystem::ConnectToServer()
{
	if (!CanConnect()) return;
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (!PC) { Fail(TEXT("로컬 플레이어를 찾지 못했습니다.")); return; }
	if (bHost)
	{
		bEnteredGame = true;
		if (AGameModeBase* GM = GetWorld()->GetAuthGameMode()) GM->RestartPlayer(PC);
		SetState(ELanSessionState::Connected, TEXT("Host 플레이 시작. 서버 종료는 Esc를 누르세요."));
		return;
	}
	auto Session = Sessions();
	if (!Session.IsValid()) { Fail(TEXT("LAN 서비스를 사용할 수 없습니다.")); return; }
	SetState(ELanSessionState::Joining, TEXT("확인된 서버에 참가하는 중..."));
	JoinHandle = Session->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
	if (!Session->JoinSession(0, NAME_GameSession, VerifiedResult) && State == ELanSessionState::Joining) Fail(TEXT("서버 참가 요청에 실패했습니다."));
}

void ULanSessionSubsystem::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
	if (Name != NAME_GameSession || State != ELanSessionState::Joining) return;
	ClearOperationDelegates();
	FString Address;
	auto Session = Sessions();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (Result != EOnJoinSessionCompleteResult::Success || !Session.IsValid() || !PC ||
		!Session->GetResolvedConnectString(NAME_GameSession, Address) || Address != VerifiedAddress)
	{ Fail(TEXT("서버 참가에 실패했습니다. 주소를 다시 확인하세요.")); return; }
	SetState(ELanSessionState::Traveling, TEXT("서버 연결 및 게임 월드 로딩 중..."));
	PC->ClientTravel(Address, TRAVEL_Absolute);
}

void ULanSessionSubsystem::ClearOperationDelegates()
{
	if (auto Session = Sessions(); Session.IsValid())
	{
		Session->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Session->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Session->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}
	CreateHandle.Reset(); FindHandle.Reset(); JoinHandle.Reset();
}

void ULanSessionSubsystem::Fail(const FString& Message)
{
	RecoveryMessage = Message;
	LeaveServer();
}

void ULanSessionSubsystem::LeaveServer()
{
	if (State == ELanSessionState::Leaving) return;
	const bool bWasSearching = State == ELanSessionState::Searching;
	ClearOperationDelegates();
	SetState(ELanSessionState::Leaving, TEXT("세션을 정리하는 중..."));
	auto Session = Sessions();
	if (Session.IsValid())
	{
		if (bWasSearching) Session->CancelFindSessions();
		if (Session->GetNamedSession(NAME_GameSession))
		{
			DestroyHandle = Session->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::Destroyed));
			if (Session->DestroySession(NAME_GameSession)) return;
			Session->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			DestroyHandle.Reset();
		}
	}
	ResetWorld();
}

void ULanSessionSubsystem::Destroyed(FName Name, bool bSuccess)
{
	if (Name != NAME_GameSession || State != ELanSessionState::Leaving) return;
	if (auto Session = Sessions(); Session.IsValid()) Session->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	DestroyHandle.Reset();
	if (!bSuccess) RecoveryMessage = TEXT("세션 정리에 실패했습니다. 다시 시도하거나 프로그램을 재시작하세요.");
	ResetWorld();
}

void ULanSessionSubsystem::ResetWorld()
{
	bEnteredGame = false;
	bHost = false;
	HostAddresses.Empty(); VerifiedAddress.Empty(); Search.Reset(); VerifiedResult = FOnlineSessionSearchResult();
	const FString Message = RecoveryMessage.IsEmpty() ? TEXT("연결을 종료했습니다. 다시 서버를 열거나 주소를 확인하세요.") : RecoveryMessage;
	RecoveryMessage.Empty();
	SetState(ELanSessionState::Idle, Message);
	// Reload locally to close the NetDriver and release all remote players/pawns.
	if (UWorld* World = GetWorld())
		UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)), true);
}

bool ULanSessionSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::Game || IsRunningDedicatedServer()) return true;
	if (Deadline > 0.0 && FPlatformTime::Seconds() > Deadline)
	{
		if (State == ELanSessionState::Leaving)
		{
			Deadline = 0.0;
			Status = TEXT("세션 정리가 지연되고 있습니다. 완료를 기다리거나 프로그램을 재시작하세요.");
		}
		else Fail(TEXT("요청 시간이 초과되었습니다. 연결을 정리한 뒤 다시 시도하세요."));
	}
	if (State == ELanSessionState::Verified && !CanConnect()) InvalidateVerification();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (State == ELanSessionState::Traveling && World->GetNetMode() == NM_Client && PC && PC->PlayerState && PC->GetPawn())
	{
		bEnteredGame = true;
		SetState(ELanSessionState::Connected, TEXT("서버 접속 완료. 나가기는 Esc를 누르세요."));
	}
	if (UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient(); Viewport && PC)
	{
		if (!Menu.IsValid() || MenuViewport.Get() != Viewport)
		{
			if (Menu.IsValid() && MenuViewport.IsValid()) MenuViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
			Menu = SNew(SLanMenu, this);
			MenuViewport = Viewport;
			Viewport->AddViewportWidgetContent(Menu.ToSharedRef(), 100);
		}
		if (State == ELanSessionState::Connected && PC->WasInputKeyJustPressed(EKeys::Escape)) bMenuVisible = !bMenuVisible;
		Menu->SetVisibility(bMenuVisible ? EVisibility::Visible : EVisibility::Collapsed);
		const bool bShow = bMenuVisible;
		if (InputController.Get() != PC || bAppliedMenuVisible != bShow)
		{
			InputController = PC;
			bAppliedMenuVisible = bShow;
			PC->bShowMouseCursor = bShow;
			if (bShow) PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
			else PC->SetInputMode(FInputModeGameOnly());
			PC->SetIgnoreMoveInput(bShow); PC->SetIgnoreLookInput(bShow);
		}
	}
	return true;
}

void ULanSessionSubsystem::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	if (World == GetWorld() && State != ELanSessionState::Idle && State != ELanSessionState::Leaving) Fail(TEXT("네트워크 오류: ") + Error);
}

void ULanSessionSubsystem::TravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (World == GetWorld() && State != ELanSessionState::Idle && State != ELanSessionState::Leaving) Fail(TEXT("맵 이동 오류: ") + Error);
}

void ULanSessionSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	ClearOperationDelegates();
	if (auto Session = Sessions(); Session.IsValid())
	{
		Session->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		if (Session->GetNamedSession(NAME_GameSession)) Session->DestroySession(NAME_GameSession);
	}
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkHandle); GEngine->OnTravelFailure().Remove(TravelHandle); }
	if (Menu.IsValid() && MenuViewport.IsValid()) MenuViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	Menu.Reset();
	Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLanAddressValidationTest, "GE.LAN.AddressValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLanAddressValidationTest::RunTest(const FString& Parameters)
{
	FString Address;
	TestTrue(TEXT("Default game port"), ParseAddress(TEXT("192.168.1.10"), Address));
	TestEqual(TEXT("Canonical address"), Address, FString(TEXT("192.168.1.10:7777")));
	TestTrue(TEXT("Trim whitespace"), ParseAddress(TEXT(" 192.168.1.10:7780 "), Address));
	for (const FString Invalid : { TEXT(""), TEXT("localhost"), TEXT("192.168..1"), TEXT("192.168.1.300"),
		TEXT("192.168.1.1:0"), TEXT("192.168.1.1:65536"), TEXT("192.168.1.1:7777?listen"), TEXT("192.168.x.1:7777"),
		TEXT("192.168.1.1:7777abc"), TEXT("0.0.0.0:7777"), TEXT("255.255.255.255:7777"), TEXT("224.0.0.1:7777") })
		TestFalse(*Invalid, ParseAddress(Invalid, Address));
	return true;
}
#endif
