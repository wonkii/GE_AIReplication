#include "SharpExperimentManager.h"
#include "SharpExperimentValidation.h"
#include "SharpExperimentSettings.h"
#include "SharpEnemyExperimentComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Scenario/SharpSplinePath.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpControl, Log, All);

bool ASharpExperimentManager::Reject(const FString& Reason)
{
	LastCommandError = Reason;
	UE_LOG(LogSharpControl, Warning, TEXT("[Sharp][Group] Rejected Manager=%s Reason=%s"), *GetName(), *Reason);
	if (HasAuthority()) { ForceNetUpdate(); }
	return false;
}

bool ASharpExperimentManager::CanControl()
{
	return (GetWorld() && GetWorld()->IsGameWorld() && HasActorBegunPlay() && HasAuthority()) ||
		Reject(TEXT("Use the running Standalone/PIE SERVER Manager instance for trial control"));
}

bool ASharpExperimentManager::IsRegistered(const ASharpSplinePath* Path) const
{
	return Paths.Contains(Path);
}

bool ASharpExperimentManager::PrepareGroup(bool bReset)
{
	FString Reason;
	if (!FSharpExperimentValidation::ValidateRegistry(*this, Reason) || !FSharpExperimentValidation::ValidateReadiness(*this, Reason)) { return Reject(Reason); }
	if (bReset && Identity.TrialId == MAX_int32) { return Reject(TEXT("TrialId exhausted; start a new run")); }
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && !bReset && (Enemy->Experiment->IsExecuting() || Enemy->Experiment->Status.State == ESharpEnemyTrialState::Suspended))
		{
			return Reject(TEXT("Stop or Reset the active group before preparing again"));
		}
	}
	TArray<ASharpSplinePath*> Targets;
	for (ASharpSplinePath* Path : Paths)
	{
		if (Path->AssignedEnemy->Experiment->Status.Generation == MAX_int32) { return Reject(TEXT("Enemy generation exhausted; start a new world")); }
		Targets.Add(Path);
	}
	TArray<FTransform> Transforms;
	if (!FSharpExperimentValidation::BuildStartTransforms(*this, Targets, Transforms, Reason)) { return Reject(Reason); }
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy)) { Enemy->Experiment->Release(); }
	}
	PreparedEnemies.Reset();
	if (bReset) { ++Identity.TrialId; OnRep_Identity(); }
	LastCommandError.Empty();
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		ASharpEnemyCharacter* Enemy = Targets[I]->AssignedEnemy;
		PreparedEnemies.Add(Enemy);
		if (!Enemy->Experiment->Prepare(this, Targets[I], Identity, Transforms[I], Reason))
		{
			for (ASharpEnemyCharacter* Prepared : PreparedEnemies) { if (IsValid(Prepared)) { Prepared->Experiment->Fail(TEXT("Group preparation aborted: ") + Reason); } }
			RefreshSummary(); return Reject(Reason);
		}
	}
	RecordingConfiguration = Settings->Recording;
	RefreshSummary(); return true;
}

void ASharpExperimentManager::PrepareAll()
{
	if (!CanControl()) { return; }
	CancelAutomaticControl(); PrepareGroup(false);
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
}

void ASharpExperimentManager::ResetAll()
{
	if (!CanControl()) { return; }
	CancelAutomaticControl();
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
	// Reset always cancels motion, even if a newly blocked start prevents
	// placement. Failed preflight leaves locations and identifiers unchanged.
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy)) { Enemy->Experiment->Stop(); } }
	PrepareGroup(true);
}

void ASharpExperimentManager::StartAll()
{
	if (!CanControl()) { return; }
	FString Reason;
	if (!FSharpExperimentValidation::ValidateRegistry(*this, Reason) || !FSharpExperimentValidation::ValidateReadiness(*this, Reason)) { Reject(Reason); return; }
	if (PreparedEnemies.IsEmpty() && !PrepareGroup(false)) { return; }
	bool bAllRunning = true;
	for (ASharpSplinePath* Path : Paths)
	{
		USharpEnemyExperimentComponent* Session = Path->AssignedEnemy->Experiment;
		if (!Session->Owns(this, Path) || !Session->ValidateBinding(Reason)) { Reject(TEXT("Reset All after changing registrations: ") + Reason); return; }
		bAllRunning &= Session->IsExecuting();
	}
	if (bAllRunning)
	{
		GetWorldTimerManager().ClearTimer(PreparationTimer); GetWorldTimerManager().ClearTimer(AutoStartTimer);
		LastCommandError.Empty(); ForceNetUpdate(); return;
	}
	CancelAutomaticControl();
	// Preflight all participants before issuing any input or policy change.
	ApplyAutomaticNetworkMode();
	for (ASharpSplinePath* Path : Paths)
	{
		if (!Path->AssignedEnemy->Experiment->CanStart(Reason))
		{
			RestoreAutomaticNetworkMode(); Reject(Path->GetName() + TEXT(": ") + Reason); return;
		}
	}
	LastCommandError.Empty();
	for (ASharpSplinePath* Path : Paths)
	{
		if (!Path->AssignedEnemy->Experiment->Start(Reason))
		{
			for (ASharpSplinePath* Target : Paths) { Target->AssignedEnemy->Experiment->Stop(); }
			RestoreAutomaticNetworkMode();
			Reject(TEXT("Group start aborted: ") + Reason); return;
		}
	}
	UE_LOG(LogSharpControl, Display, TEXT("[Sharp][Group] Started RunId=%s TrialId=%d Count=%d ServerTime=%.6f"),
		*Identity.RunId, Identity.TrialId, Paths.Num(), GetWorld()->GetTimeSeconds());
	OnGroupStarted();
	RefreshSummary();
}

void ASharpExperimentManager::StopAll()
{
	if (!CanControl()) { return; }
	CancelAutomaticControl();
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy)) { Enemy->Experiment->Stop(); } }
	LastCommandError.Empty(); RefreshSummary();
}

bool ASharpExperimentManager::PrepareSingle(ASharpSplinePath* Path, bool bReset)
{
    if (!CanControl()) { return false; }
    CancelAutomaticControl();
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
    if (bReset)
    {
        for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
        {
            if (IsValid(Enemy) && Enemy->Experiment->GetPath() == Path) { Enemy->Experiment->Stop(); }
        }
    }
    FString Reason;
    if (!FSharpExperimentValidation::ValidateTarget(*this, Path, Reason) ||
        !FSharpExperimentValidation::ValidateTargetReadiness(Path, Reason)) { return Reject(Reason); }
    USharpEnemyExperimentComponent* Session = Path->AssignedEnemy->Experiment;
    if (!bReset && (Session->IsExecuting() || Session->Status.State == ESharpEnemyTrialState::Suspended))
    { return Reject(TEXT("Stop or Reset Enemy before preparing it")); }
    if (Session->Status.Generation == MAX_int32) { return Reject(TEXT("Enemy generation exhausted; start a new world")); }
    TArray<FTransform> Transforms;
    if (!FSharpExperimentValidation::BuildStartTransforms(*this, {Path}, Transforms, Reason)) { return Reject(Reason); }
    // Retire the prior Enemy binding if the author assigned another Enemy to
    // this Path while stopped. Never retain two sessions for one Spline.
    for (ASharpEnemyCharacter* Previous : PreparedEnemies)
    {
        if (IsValid(Previous) && Previous != Path->AssignedEnemy && Previous->Experiment->GetPath() == Path)
        { Previous->Experiment->Release(); }
    }
    PreparedEnemies.RemoveAll([this](const ASharpEnemyCharacter* Enemy)
        { return !IsValid(Enemy) || Enemy->Experiment->GetManager() != this; });
    PreparedEnemies.AddUnique(Path->AssignedEnemy);
    if (!Session->Prepare(this, Path, Identity, Transforms[0], Reason)) { RefreshSummary(); return Reject(Reason); }
	RecordingConfiguration = Settings->Recording;
    LastCommandError.Empty(); RefreshSummary(); return true;
}

bool ASharpExperimentManager::PrepareEnemy(ASharpSplinePath* Path)
{
    return PrepareSingle(Path, false);
}

bool ASharpExperimentManager::StartEnemy(ASharpSplinePath* Path)
{
	if (!CanControl()) { return false; }
	CancelAutomaticControl();
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
	if (!IsValid(Path) || !IsRegistered(Path) || !IsValid(Path->AssignedEnemy)) { return Reject(TEXT("Invalid registered Spline/Enemy")); }
	USharpEnemyExperimentComponent* Session = Path->AssignedEnemy->Experiment;
	if (!Session->Owns(this, Path)) { return Reject(TEXT("Prepare or Reset Enemy first")); }
	FString Reason;
	if (!FSharpExperimentValidation::ValidateTarget(*this, Path, Reason)) { return Reject(Reason); }
	if (!Session->Start(Reason)) { return Reject(Reason); }
	LastCommandError.Empty(); RefreshSummary(); return true;
}

bool ASharpExperimentManager::StopEnemy(ASharpSplinePath* Path)
{
	if (!CanControl()) { return false; }
	CancelAutomaticControl();
	RestoreAutomaticNetworkMode(); bRepeatContinuation = false;
	if (!IsValid(Path)) { return Reject(TEXT("Invalid Spline")); }
	bool bFound = false;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && Enemy->Experiment->GetPath() == Path) { Enemy->Experiment->Stop(); bFound = true; }
	}
	if (!bFound) { return Reject(TEXT("No prepared session for this Spline")); }
	LastCommandError.Empty(); RefreshSummary(); return true;
}

bool ASharpExperimentManager::ResetEnemy(ASharpSplinePath* Path)
{
    return PrepareSingle(Path, true);
}

void ASharpExperimentManager::SuspendTrial()
{
	if (!CanControl()) { return; }
	CancelAutomaticControl();
	bRepeatContinuation = false;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies) { if (IsValid(Enemy)) { Enemy->Experiment->SuspendInput(); } }
	LastCommandError.Empty();
	RefreshSummary();
}

void ASharpExperimentManager::ResumeTrial()
{
	if (!CanControl()) { return; }
	CancelAutomaticControl();
	bRepeatContinuation = false;
	FString Reason;
	TArray<USharpEnemyExperimentComponent*> Targets;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && Enemy->Experiment->Status.State == ESharpEnemyTrialState::Suspended)
		{
			if (!Enemy->Experiment->CanStart(Reason)) { Reject(Reason); return; }
			Targets.Add(Enemy->Experiment);
		}
	}
	for (USharpEnemyExperimentComponent* Target : Targets)
	{
		if (!Target->ResumeInput(Reason))
		{
			for (USharpEnemyExperimentComponent* Started : Targets) { Started->SuspendInput(); }
			Reject(Reason); return;
		}
	}
	LastCommandError.Empty();
	RefreshSummary();
}

void ASharpExperimentManager::RefreshSummary()
{
	if (!HasAuthority()) { return; }
	Summary = FSharpExperimentSummary();
	Summary.Total = Paths.Num();
	for (ASharpSplinePath* Path : Paths)
	{
		if (!IsValid(Path) || !IsValid(Path->AssignedEnemy)) { ++Summary.Failed; continue; }
		switch (Path->AssignedEnemy->Experiment->Status.State)
		{
		case ESharpEnemyTrialState::Ready: ++Summary.Ready; break;
		case ESharpEnemyTrialState::Running: ++Summary.Running; break;
		case ESharpEnemyTrialState::Stopped: ++Summary.Stopped; break;
		case ESharpEnemyTrialState::Suspended: ++Summary.Suspended; break;
		case ESharpEnemyTrialState::Completed: ++Summary.Completed; break;
		case ESharpEnemyTrialState::Failed: ++Summary.Failed; break;
		default: break;
		}
	}
	ForceNetUpdate();
	OnGroupProgress();
}
