#include "GE_AIReplicationPlayerController.h"

#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Experiment/SharpExperimentManager.h"
#include "../Scenario/SharpSplinePath.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpExperimentDebug, Log, All);

void AGE_AIReplicationPlayerController::SharpPrepare()
{
	RequestSharpDebugCommand(ESharpExperimentDebugCommand::Prepare);
}

void AGE_AIReplicationPlayerController::SharpStart()
{
	RequestSharpDebugCommand(ESharpExperimentDebugCommand::Start);
}

void AGE_AIReplicationPlayerController::SharpStop()
{
	RequestSharpDebugCommand(ESharpExperimentDebugCommand::Stop);
}

void AGE_AIReplicationPlayerController::SharpReset()
{
	RequestSharpDebugCommand(ESharpExperimentDebugCommand::Reset);
}

void AGE_AIReplicationPlayerController::SharpStatus()
{
	RequestSharpDebugCommand(ESharpExperimentDebugCommand::Status);
}

void AGE_AIReplicationPlayerController::RequestSharpDebugCommand(ESharpExperimentDebugCommand Command)
{
	if (!IsLocalController() || !GetWorld() || !GetWorld()->IsGameWorld()) { return; }
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	ClientSharpDebugResult(TEXT("[Sharp][Debug] Commands are available in Development/Debug builds only."), false);
#else
	// The PlayerController is owned by this connection; the replicated Manager is not.
	if (HasAuthority()) { ServerSharpDebugCommand_Implementation(Command); }
	else { ServerSharpDebugCommand(Command); }
#endif
}

void AGE_AIReplicationPlayerController::ServerSharpDebugCommand_Implementation(ESharpExperimentDebugCommand Command)
{
#if UE_BUILD_SHIPPING || UE_BUILD_TEST
	ClientSharpDebugResult(TEXT("[Sharp][Debug] Commands are available in Development/Debug builds only."), false);
#else
	if (!HasAuthority() || !GetWorld() || !GetWorld()->IsGameWorld()) { return; }
	ASharpExperimentManager* Manager = nullptr;
	for (TActorIterator<ASharpExperimentManager> It(GetWorld()); It; ++It)
	{
		if (Manager)
		{
			ClientSharpDebugResult(TEXT("[Sharp][Debug] Rejected: only one ExperimentManager is supported per world."), false);
			return;
		}
		Manager = *It;
	}
	if (!Manager)
	{
		ClientSharpDebugResult(TEXT("[Sharp][Debug] Rejected: no loaded ExperimentManager in the server world."), false);
		return;
	}

	const TCHAR* CommandName = nullptr;
	switch (Command)
	{
	case ESharpExperimentDebugCommand::Prepare: CommandName = TEXT("SharpPrepare"); Manager->PrepareAll(); break;
	case ESharpExperimentDebugCommand::Start: CommandName = TEXT("SharpStart"); Manager->StartAll(); break;
	case ESharpExperimentDebugCommand::Stop: CommandName = TEXT("SharpStop"); Manager->StopAll(); break;
	case ESharpExperimentDebugCommand::Reset: CommandName = TEXT("SharpReset"); Manager->ResetAll(); break;
	case ESharpExperimentDebugCommand::Status: CommandName = TEXT("SharpStatus"); break;
	default:
		ClientSharpDebugResult(TEXT("[Sharp][Debug] Rejected: unknown command."), false);
		return;
	}
	if (Command != ESharpExperimentDebugCommand::Status && !Manager->LastCommandError.IsEmpty())
	{
		ClientSharpDebugResult(FString::Printf(TEXT("[Sharp][Debug] %s rejected: %s"), CommandName, *Manager->LastCommandError), false);
		return;
	}

	const FSharpExperimentSummary& Summary = Manager->Summary;
	ClientSharpDebugResult(FString::Printf(
		TEXT("[Sharp][Debug] %s OK | Trial=%d Total=%d Ready=%d Running=%d Stopped=%d Suspended=%d Completed=%d Failed=%d"),
		CommandName, Manager->Identity.TrialId, Summary.Total, Summary.Ready, Summary.Running,
		Summary.Stopped, Summary.Suspended, Summary.Completed, Summary.Failed), true);
	if (Command == ESharpExperimentDebugCommand::Status)
	{
		if (!Manager->LastCommandError.IsEmpty())
		{
			ClientSharpDebugResult(TEXT("[Sharp][Debug] Last command error: ") + Manager->LastCommandError, false);
		}
		for (const ASharpSplinePath* Path : Manager->Paths)
		{
			const ASharpEnemyCharacter* Enemy = IsValid(Path) ? Path->AssignedEnemy.Get() : nullptr;
			if (!IsValid(Enemy) || !Enemy->Experiment)
			{
				ClientSharpDebugResult(FString::Printf(TEXT("[Sharp][Debug] Path=%s has no valid Enemy/Experiment."), *GetNameSafe(Path)), false);
				continue;
			}
			const FSharpEnemyTrialStatus& Status = Enemy->Experiment->Status;
			ClientSharpDebugResult(FString::Printf(
				TEXT("[Sharp][Debug] Path=%s ActorId=%d State=%s Speed=%.1fcm/s Position=(%s) Reason=%s"),
				*Path->PathId.ToString(), Enemy->ExperimentActorId, *UEnum::GetValueAsString(Status.State),
				Enemy->GetVelocity().Size(), *Enemy->GetActorLocation().ToCompactString(), *Status.FailureReason), true);
		}
	}
#endif
}

void AGE_AIReplicationPlayerController::ClientSharpDebugResult_Implementation(const FString& Message, bool bSuccess)
{
	if (bSuccess) { UE_LOG(LogSharpExperimentDebug, Display, TEXT("%s"), *Message); }
	else { UE_LOG(LogSharpExperimentDebug, Warning, TEXT("%s"), *Message); }
	// Also print in the requesting player's console, including on a remote Client.
	ClientMessage(Message);
}
