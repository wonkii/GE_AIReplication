#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "LanSessionSubsystem.generated.h"

class FOnlineSessionSearch;
class SWidget;
class UNetDriver;

UENUM(BlueprintType)
enum class ELanSessionState : uint8
{
	Idle, Creating, Hosting, Searching, Verified, Joining, Traveling, Connected, Leaving
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLanSessionChanged, ELanSessionState, State);

/** Local process coordinator. LAN discovery never initiates travel. */
UCLASS()
class AIREPLICATIONNET_API ULanSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="LAN") void HostServer();
	UFUNCTION(BlueprintCallable, Category="LAN") void VerifyAddress(const FString& Address);
	UFUNCTION(BlueprintCallable, Category="LAN") void ConnectToServer();
	UFUNCTION(BlueprintCallable, Category="LAN") void LeaveServer();
	UFUNCTION(BlueprintCallable, Category="LAN") void InvalidateVerification();
	UFUNCTION(BlueprintPure, Category="LAN") bool CanConnect() const;
	UFUNCTION(BlueprintPure, Category="LAN") ELanSessionState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category="LAN") FString GetStatus() const { return Status; }
	UFUNCTION(BlueprintPure, Category="LAN") FString GetHostAddresses() const { return HostAddresses; }
	bool HasEnteredGame() const { return bEnteredGame; }
	UPROPERTY(BlueprintAssignable, Category="LAN") FLanSessionChanged OnStateChanged;

private:
	void SetState(ELanSessionState NewState, const FString& Message);
	bool Tick(float DeltaTime);
	IOnlineSessionPtr Sessions() const;
	void Created(FName Name, bool bSuccess);
	void Found(bool bSuccess);
	void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
	void Destroyed(FName Name, bool bSuccess);
	void Fail(const FString& Message);
	void ResetWorld();
	void ClearOperationDelegates();
	void NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void TravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);

	ELanSessionState State = ELanSessionState::Idle;
	FString Status, HostAddresses, RequestedAddress, VerifiedAddress, RecoveryMessage;
	TSharedPtr<FOnlineSessionSearch> Search;
	FOnlineSessionSearchResult VerifiedResult;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, DestroyHandle, NetworkHandle, TravelHandle;
	FTSTicker::FDelegateHandle TickHandle;
	TSharedPtr<SWidget> Menu;
	TWeakObjectPtr<class UGameViewportClient> MenuViewport;
	double Deadline = 0.0;
	double VerifiedAt = 0.0;
	bool bEnteredGame = false;
	bool bHost = false;
	bool bMenuVisible = true;
	bool bAppliedMenuVisible = false;
	TWeakObjectPtr<class APlayerController> InputController;
};
