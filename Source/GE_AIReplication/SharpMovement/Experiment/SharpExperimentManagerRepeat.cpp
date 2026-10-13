#include "SharpExperimentManager.h"
#include "SharpEnemyExperimentComponent.h"
#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

void ASharpExperimentManager::ApplyAutomaticNetworkMode()
{
	if (!bAutoRepeat || ModeSequence.IsEmpty() || !bManageAdaptiveForModeSequence) { return; }
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("net.UseAdaptiveNetUpdateFrequency"));
	if (!Variable) { return; }
	if (!bOwnsAdaptiveCVar)
	{
		OriginalAdaptiveValue = Variable->GetInt(); OriginalAdaptivePriority = Variable->GetFlags() & ECVF_SetByMask;
		bOwnsAdaptiveCVar = true;
	}
	Variable->Set(Identity.Mode == ESharpExperimentMode::Adaptive ? 1 : 0, ECVF_SetByConsole);
}

void ASharpExperimentManager::RestoreAutomaticNetworkMode()
{
	if (!bOwnsAdaptiveCVar) { return; }
	for (const ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && Enemy->Experiment->IsObservingMovement()) { return; }
	}
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("net.UseAdaptiveNetUpdateFrequency")))
	{
		Variable->Set(OriginalAdaptiveValue, ECVF_SetByConsole);
		Variable->SetFlags(static_cast<EConsoleVariableFlags>((Variable->GetFlags() & ~ECVF_SetByMask) | OriginalAdaptivePriority));
	}
	bOwnsAdaptiveCVar = false;
}

void ASharpExperimentManager::OnGroupStarted()
{
	if (!bRepeatContinuation)
	{
		RepetitionsStarted = 0;
		SequenceIndex = ModeSequence.IsEmpty() ? 0 : ModeSequence.IndexOfByKey(Identity.Mode);
	}
	bRepeatContinuation = false; ++RepetitionsStarted; bRepeatArmed = true;
	bBatchFailureStopsGroup = bAutoRepeat || TrialDurationSeconds > 0;
	if (TrialDurationSeconds > 0)
	{
		GetWorldTimerManager().SetTimer(TrialDurationTimer, this, &ASharpExperimentManager::CompleteTimedTrial, TrialDurationSeconds, false);
	}
}

void ASharpExperimentManager::CompleteTimedTrial()
{
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && Enemy->Experiment->GetManager() == this) { Enemy->Experiment->Complete(); }
	}
}

void ASharpExperimentManager::OnGroupProgress()
{
	if (!bRepeatArmed)
	{
		if (Summary.Running + Summary.Suspended == 0 && !GetWorldTimerManager().IsTimerActive(RepeatTimer) &&
			!GetWorldTimerManager().IsTimerActive(AutoStartTimer) && !bRepeatContinuation) { RestoreAutomaticNetworkMode(); }
		return;
	}
	if (Summary.Failed > 0)
	{
		CancelAutomaticControl(); RestoreAutomaticNetworkMode();
		// Automatic timed/repeated batches fail as a group; ordinary manual movement stays independent.
		if (bBatchFailureStopsGroup)
		{
			for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy) && Enemy->Experiment->IsObservingMovement()) { Enemy->Experiment->Stop(); } }
		}
		Reject(TEXT("Automatic control cancelled after an Enemy failed")); return;
	}
	if (Summary.Total == 0 || Summary.Completed != Summary.Total) { return; }
	bRepeatArmed = false; GetWorldTimerManager().ClearTimer(TrialDurationTimer);
	float Tail = Settings ? Settings->Recording.PostTrialObservationSeconds : 0.5f;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy)) { Tail = FMath::Max(Tail, Enemy->Experiment->Status.Recording.PostTrialObservationSeconds); } }
	GetWorldTimerManager().SetTimer(RepeatTimer, this, &ASharpExperimentManager::RepeatNextTrial,
		FMath::Max(0.01f, Tail + RepeatDelaySeconds), false);
}

void ASharpExperimentManager::RepeatNextTrial()
{
	// Flush tails before a teleport or identity change, independent of tick ordering.
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy)) { Enemy->Recorder->Finish(TEXT("Completed")); } }
	if (!bAutoRepeat) { RestoreAutomaticNetworkMode(); return; }
	if (RepeatCount < 1 || !FMath::IsFinite(RepeatDelaySeconds) || RepeatDelaySeconds < 0 ||
		(!ModeSequence.IsEmpty() && !ModeSequence.IsValidIndex(SequenceIndex)))
	{
		Reject(TEXT("Repeat configuration changed to invalid values")); RestoreAutomaticNetworkMode(); return;
	}
	if (RepetitionsStarted >= RepeatCount)
	{
		if (ModeSequence.IsEmpty() || SequenceIndex + 1 >= ModeSequence.Num()) { RestoreAutomaticNetworkMode(); return; }
		++SequenceIndex; RepetitionsStarted = 0;
	}
	const ESharpExperimentMode NextMode = ModeSequence.IsEmpty() ? Identity.Mode : ModeSequence[SequenceIndex];
	const ESharpExperimentMode PreviousMode = Identity.Mode;
	Identity.Mode = NextMode;
	if (!PrepareGroup(true)) { Identity.Mode = PreviousMode; CancelAutomaticControl(); RestoreAutomaticNetworkMode(); return; }
	bRepeatContinuation = true;
	AutoStartEarliestTime = -1;
	PreparationDeadline = GetWorld()->GetTimeSeconds() + PreparationTimeoutSeconds;
	GetWorldTimerManager().SetTimer(AutoStartTimer, this, &ASharpExperimentManager::TryAutomaticStart, 0.1f, true);
}
