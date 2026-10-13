#include "SharpExperimentManager.h"
#include "SharpExperimentValidation.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void ASharpExperimentManager::CancelAutomaticControl()
{
	GetWorldTimerManager().ClearTimer(PreparationTimer);
	GetWorldTimerManager().ClearTimer(AutoStartTimer);
	GetWorldTimerManager().ClearTimer(TrialDurationTimer);
	GetWorldTimerManager().ClearTimer(RepeatTimer);
	bRepeatArmed = false;
}

void ASharpExperimentManager::TryAutomaticPrepare()
{
	FString Reason;
	if (!FSharpExperimentValidation::ValidateRegistry(*this, Reason)) { CancelAutomaticControl(); Reject(Reason); return; }
	if (!FSharpExperimentValidation::ValidateReadiness(*this, Reason))
	{
		if (GetWorld()->GetTimeSeconds() >= PreparationDeadline) { CancelAutomaticControl(); Reject(Reason); }
		return;
	}
	GetWorldTimerManager().ClearTimer(PreparationTimer);
	if (!PrepareGroup(false) || !bAutoStartTrial) { return; }
	AutoStartEarliestTime = -1.0;
	PreparationDeadline = GetWorld()->GetTimeSeconds() + PreparationTimeoutSeconds;
	GetWorldTimerManager().SetTimer(AutoStartTimer, this, &ASharpExperimentManager::TryAutomaticStart, 0.1f, true);
}

void ASharpExperimentManager::TryAutomaticStart()
{
	int32 ConnectedClients = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (Controller && Controller->GetNetConnection()) { ++ConnectedClients; }
	}
	if (ConnectedClients < ExpectedClientCount)
	{
		AutoStartEarliestTime = -1.0;
		if (GetWorld()->GetTimeSeconds() >= PreparationDeadline)
		{
			CancelAutomaticControl(); Reject(TEXT("Timed out waiting for expected client PlayerControllers"));
		}
		return;
	}
	// Warm-up starts after expected clients have server-side PlayerControllers.
	// This is a connection gate, not an acknowledgement of client rendering.
	if (AutoStartEarliestTime < 0) { AutoStartEarliestTime = GetWorld()->GetTimeSeconds() + AutoStartDelaySeconds; }
	if (GetWorld()->GetTimeSeconds() < AutoStartEarliestTime) { return; }
	StartAll();
	GetWorldTimerManager().ClearTimer(AutoStartTimer);
}
