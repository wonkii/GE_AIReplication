#include "LanSessionSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Two independent -game processes exercise the same public API as the buttons.
// Host: -ExecCmds="LAN.Smoke Host"
// Client: -ExecCmds="LAN.Smoke Client 192.168.0.10:7777"
static FAutoConsoleCommandWithWorldAndArgs GLanSmoke(
	TEXT("LAN.Smoke"), TEXT("Development LAN integration test: Host | Client IPv4:port"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->GetGameInstance() || Args.IsEmpty()) return;
		const bool bHost = Args[0] == TEXT("Host");
		if (!bHost && Args.Num() != 2) return;
		TWeakObjectPtr<ULanSessionSubsystem> LAN = World->GetGameInstance()->GetSubsystem<ULanSessionSubsystem>();
		const FString Address = bHost ? FString() : Args[1];
		FString ExpectedMap;
		FParse::Value(FCommandLine::Get(), TEXT("LANExpectedMap="), ExpectedMap);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[LAN, bHost, Address, ExpectedMap, Phase = 0, Started = FPlatformTime::Seconds(), ReadyAt = 0.0,
			 RemotePlayer = TWeakObjectPtr<ACharacter>(), RemoteStart = FVector::ZeroVector](float) mutable
			{
				if (!LAN.IsValid()) return false;
				UWorld* CurrentWorld = LAN->GetWorld();
				APlayerController* PC = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
				const double Now = FPlatformTime::Seconds();
				if (Now - Started > 90.0)
				{
					UE_LOG(LogTemp, Error, TEXT("[LAN-SMOKE] FAIL timeout phase=%d state=%d %s"), Phase, int32(LAN->GetState()), *LAN->GetStatus());
					FPlatformMisc::RequestExitWithStatus(false, 1); return false;
				}
				if (!PC || !CurrentWorld->HasBegunPlay()) return true;
				if (Phase == 0)
				{
					if (!ExpectedMap.IsEmpty() && CurrentWorld->GetOutermost()->GetName() != ExpectedMap)
					{
						UE_LOG(LogTemp, Error, TEXT("[LAN-SMOKE] FAIL startup map=%s expected=%s"), *CurrentWorld->GetOutermost()->GetName(), *ExpectedMap);
						FPlatformMisc::RequestExitWithStatus(false, 1); return false;
					}
					if (PC->GetPawn() || LAN->CanConnect())
					{
						UE_LOG(LogTemp, Error, TEXT("[LAN-SMOKE] FAIL initial player entered without button"));
						FPlatformMisc::RequestExitWithStatus(false, 1); return false;
					}
					Phase = 1;
					if (bHost) LAN->HostServer(); else LAN->VerifyAddress(Address);
				}
				else if (Phase == 1 && LAN->CanConnect())
				{
					ReadyAt = Now; Phase = 2;
					UE_LOG(LogTemp, Display, TEXT("[LAN-SMOKE] READY %s"), *LAN->GetHostAddresses());
				}
				else if (Phase == 2)
				{
					if (PC->GetPawn() || LAN->HasEnteredGame() || (!bHost && CurrentWorld->GetNetMode() != NM_Standalone))
					{
						UE_LOG(LogTemp, Error, TEXT("[LAN-SMOKE] FAIL automatic entry before explicit connect"));
						FPlatformMisc::RequestExitWithStatus(false, 1); return false;
					}
					if (Now - ReadyAt > (bHost ? 10.0 : 2.0)) { Phase = 3; LAN->ConnectToServer(); }
				}
				else if (Phase == 3 && LAN->GetState() == ELanSessionState::Connected && PC->GetPawn())
				{
					if (CurrentWorld->GetNetMode() != (bHost ? NM_ListenServer : NM_Client) ||
						(!ExpectedMap.IsEmpty() && CurrentWorld->GetOutermost()->GetName() != ExpectedMap))
					{
						UE_LOG(LogTemp, Error, TEXT("[LAN-SMOKE] FAIL connected map or net role"));
						FPlatformMisc::RequestExitWithStatus(false, 1); return false;
					}
					UE_LOG(LogTemp, Display, TEXT("[LAN-SMOKE] PASS explicit connect role=%s"), bHost ? TEXT("Host") : TEXT("Client"));
					Phase = 4;
				}
				else if (Phase == 4)
				{
					for (TActorIterator<ACharacter> It(CurrentWorld); It; ++It)
					{
						if (*It != PC->GetPawn() && It->GetPlayerState())
						{
							RemotePlayer = *It; RemoteStart = It->GetActorLocation(); ReadyAt = Now; Phase = 5;
							break;
						}
					}
				}
				else if (Phase == 5 && RemotePlayer.IsValid())
				{
					// Both local players move through normal CMC input; observe the peer over the network.
					PC->GetPawn()->AddMovementInput(FVector::ForwardVector, 1.0f);
					const float Distance = FVector::Dist2D(RemoteStart, RemotePlayer->GetActorLocation());
					if (Now - ReadyAt > 3.0 && Distance > 10.0f)
					{
						UE_LOG(LogTemp, Display, TEXT("[LAN-SMOKE] PASS remote movement role=%s Map=%s Peer=%s DistanceCm=%.1f"),
							bHost ? TEXT("Host") : TEXT("Client"), *CurrentWorld->GetOutermost()->GetName(), *RemotePlayer->GetName(), Distance);
						Phase = 6; ReadyAt = Now;
					}
				}
				else if (Phase == 6 && Now - ReadyAt > (bHost ? 0.0 : 2.0))
				{
					if (bHost) return false;
					Phase = 7; LAN->LeaveServer();
				}
				else if (Phase == 7 && LAN->GetState() == ELanSessionState::Idle && CurrentWorld->GetNetMode() == NM_Standalone && !PC->GetPawn())
				{
					UE_LOG(LogTemp, Display, TEXT("[LAN-SMOKE] PASS disconnect returns to menu"));
					FPlatformMisc::RequestExitWithStatus(false, 0); return false;
				}
				return true;
			}), 0.0f);
	}));
#endif
