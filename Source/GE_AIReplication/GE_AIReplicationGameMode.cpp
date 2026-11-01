// Copyright Epic Games, Inc. All Rights Reserved.

#include "GE_AIReplicationGameMode.h"
#include "LanSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AGE_AIReplicationGameMode::AGE_AIReplicationGameMode()
{
	// stub
}

void AGE_AIReplicationGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Keep the packaged Host out of gameplay until the explicit connect button.
	// Remote players have already pressed connect before reaching this world.
	if (GetWorld()->WorldType == EWorldType::Game && NewPlayer && NewPlayer->IsLocalController())
	{
		const ULanSessionSubsystem* LAN = GetGameInstance()->GetSubsystem<ULanSessionSubsystem>();
		if (LAN && !LAN->HasEnteredGame()) return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}
