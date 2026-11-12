#include "SharpExperimentManager.h"
#include "SharpEnemyExperimentComponent.h"
#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Measurement/SharpMovementRecorderComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpBatch, Log, All);

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
    StopIntegratedKnockback();
    IntegratedKnockbackPulseIndex = 0;
    if (bEnableIntegratedMovementCourse && !RandomWalkEnemies.IsEmpty())
    {
        GetWorldTimerManager().SetTimer(IntegratedKnockbackTimer, this,
            &ASharpExperimentManager::ApplyIntegratedKnockbackPulse,
            IntegratedKnockbackPulseIntervalSeconds, true, 2.0f);
    }
	if (!bRepeatContinuation)
	{
		RepetitionsStarted = 0;
		SequenceIndex = ModeSequence.IsEmpty() ? 0 : ModeSequence.IndexOfByKey(Identity.Mode);
	}
	bRepeatContinuation = false; ++RepetitionsStarted; bRepeatArmed = true;
	bBatchFailureStopsGroup = bAutoRepeat || TrialDurationSeconds > 0;
	if (bAllModesBatchActive)
	{
		const int32 BatchTrial = SequenceIndex * RepeatCount + RepetitionsStarted;
		const FString ModeName = StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Identity.Mode));
		UE_LOG(LogSharpBatch, Display, TEXT("[Sharp][Batch] START %d/%d Mode=%s Repeat=%d/%d RunId=%s TrialId=%d"),
			BatchTrial, ModeSequence.Num() * RepeatCount, *ModeName, RepetitionsStarted, RepeatCount,
			*Identity.RunId, Identity.TrialId);
	}
	if (TrialDurationSeconds > 0)
	{
		GetWorldTimerManager().SetTimer(TrialDurationTimer, this, &ASharpExperimentManager::CompleteTimedTrial, TrialDurationSeconds, false);
	}
}

void ASharpExperimentManager::CompleteTimedTrial()
{
    StopIntegratedKnockback();
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
    StopIntegratedKnockback();
	bRepeatArmed = false; GetWorldTimerManager().ClearTimer(TrialDurationTimer);
	if (bAllModesBatchActive)
	{
		const int32 BatchTrial = SequenceIndex * RepeatCount + RepetitionsStarted;
		UE_LOG(LogSharpBatch, Display, TEXT("[Sharp][Batch] COMPLETE %d/%d RunId=%s TrialId=%d Mode=%s"),
			BatchTrial, ModeSequence.Num() * RepeatCount, *Identity.RunId, Identity.TrialId,
			*StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Identity.Mode)));
	}
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
		if (ModeSequence.IsEmpty() || SequenceIndex + 1 >= ModeSequence.Num())
		{
			if (bAllModesBatchActive)
			{
				UE_LOG(LogSharpBatch, Display, TEXT("[Sharp][Batch] ALL COMPLETE: %d/%d trials, RunId=%s"),
					ModeSequence.Num() * RepeatCount, ModeSequence.Num() * RepeatCount, *Identity.RunId);
			}
			RestoreAutomaticNetworkMode(); return;
		}
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
