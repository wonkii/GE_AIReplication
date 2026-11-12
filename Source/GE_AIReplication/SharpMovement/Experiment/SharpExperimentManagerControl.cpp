#include "SharpExperimentManager.h"
#include "../Movement/SharpRandomWalkerComponent.h"
#include "EngineUtils.h"
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

bool ASharpExperimentManager::IsRegisteredRandom(const ASharpEnemyCharacter* Enemy) const
{
	return RandomWalkEnemies.ContainsByPredicate([Enemy](const TObjectPtr<ASharpEnemyCharacter>& Registered)
	{
		return Registered.Get() == Enemy;
	});
}

FTransform ASharpExperimentManager::GetRandomWalkStartTransform(ASharpEnemyCharacter* Enemy) const
{
	if (const FTransform* Saved = RandomWalkInitialTransforms.Find(Enemy)) { return *Saved; }
	return IsValid(Enemy) ? Enemy->GetActorTransform() : FTransform::Identity;
}

void ASharpExperimentManager::SetServerOnlyMovementDemo(bool bEnabled)
{
	if (!HasAuthority()) { return; }
	bServerOnlyMovementDemo = bEnabled;
	TArray<ASharpEnemyCharacter*> Targets;
	for (const ASharpSplinePath* Path : Paths) { if (IsValid(Path)) { Targets.AddUnique(Path->AssignedEnemy.Get()); } }
	for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies) { Targets.AddUnique(Enemy); }
	for (ASharpEnemyCharacter* Enemy : Targets)
	{
		if (IsValid(Enemy) && Enemy->GetIsReplicated())
		{
			Enemy->SetReplicateMovement(!bEnabled);
			Enemy->ForceNetUpdate();
		}
	}
}

bool ASharpExperimentManager::AssignRandomWalkIdentities(FString& OutReason)
{
    OutReason.Empty();
    if (!HasAuthority()) { OutReason = TEXT("RandomWalk identity assignment requires server authority"); return false; }
    if (RandomWalkEnemies.IsEmpty()) { return true; }
    if (!bAutoAssignRandomWalkIdentity)
    {
        for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
        {
            if (IsValid(Enemy) && Enemy->RandomWalker) { Enemy->RandomWalker->ClearManagerSeed(); }
        }
        return true;
    }
    if (RandomWalkFirstActorId <= 0 ||
        static_cast<int64>(RandomWalkFirstActorId) + RandomWalkEnemies.Num() - 1 > MAX_int32)
    {
        OutReason = TEXT("RandomWalkFirstActorId and number of enemies exceed valid ActorId range"); return false;
    }
    TSet<const ASharpEnemyCharacter*> RandomTargets;
    for (const ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
    {
        if (!IsValid(Enemy) || Enemy->GetWorld() != GetWorld() || !Enemy->RandomWalker || RandomTargets.Contains(Enemy))
        {
            OutReason = TEXT("RandomWalkEnemies has an invalid/duplicate Enemy or missing RandomWalker"); return false;
        }
        RandomTargets.Add(Enemy);
    }
    // Validate the entire proposed ID range before touching any Actor.
    for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It)
    {
        const ASharpEnemyCharacter* Other = *It;
        if (!RandomTargets.Contains(Other) && Other->ExperimentActorId >= RandomWalkFirstActorId &&
            static_cast<int64>(Other->ExperimentActorId) < static_cast<int64>(RandomWalkFirstActorId) + RandomWalkEnemies.Num())
        {
            OutReason = FString::Printf(TEXT("RandomWalk auto ID range conflicts with %s (ActorId=%d). Change RandomWalkFirstActorId."),
                *Other->GetName(), Other->ExperimentActorId);
            return false;
        }
    }
    for (int32 Index = 0; Index < RandomWalkEnemies.Num(); ++Index)
    {
        ASharpEnemyCharacter* Enemy = RandomWalkEnemies[Index];
        const int32 OneBasedIndex = Index + 1;
        const int32 Id = RandomWalkFirstActorId + Index;
        const int32 Seed = static_cast<int32>(static_cast<uint32>(RandomWalkBaseSeed) + static_cast<uint32>(OneBasedIndex));
        const bool bIdChanged = Enemy->ExperimentActorId != Id;
        Enemy->ExperimentActorId = Id;
        Enemy->RandomWalker->SetManagerSeed(Seed);
        // Auto-prepare polls; do not force movement replication every 100 ms.
        if (bIdChanged)
        {
            Enemy->ForceNetUpdate();
            UE_LOG(LogSharpControl, Display, TEXT("[Sharp][AutoIdentity] Index=%d Enemy=%s ActorId=%d RandomSeed=%d"),
                OneBasedIndex, *Enemy->GetName(), Id, Seed);
        }
    }
    return true;
}

bool ASharpExperimentManager::PrepareGroup(bool bReset)
{
	FString Reason;
    // Optional bounded random arena; Spline targets never receive this setting.
    if (bEnableRandomWalkArena &&
        (!FMath::IsFinite(RandomWalkArenaSizeCm.X) || !FMath::IsFinite(RandomWalkArenaSizeCm.Y) ||
         !FMath::IsFinite(RandomWalkArenaMarginCm) ||
         !FMath::IsFinite(RandomWalkAvoidanceLookAheadCm) ||
         !FMath::IsFinite(RandomWalkWallProbeIntervalSeconds) ||
         RandomWalkArenaSizeCm.X < 1000.0f || RandomWalkArenaSizeCm.Y < 1000.0f ||
         RandomWalkArenaMarginCm < 0.0f || RandomWalkAvoidanceLookAheadCm < 100.0f ||
         RandomWalkWallProbeIntervalSeconds < 0.02f ||
         2.0f * (RandomWalkArenaMarginCm + RandomWalkAvoidanceLookAheadCm) >=
             FMath::Min(RandomWalkArenaSizeCm.X, RandomWalkArenaSizeCm.Y)))
    {
        return Reject(TEXT("Invalid RandomWalk arena size, margin or avoidance probe settings"));
    }
    for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
    {
        if (IsValid(Enemy) && Enemy->RandomWalker)
        {
            Enemy->RandomWalker->ConfigureArena(bEnableRandomWalkArena, GetActorLocation(),
                GetActorRotation().Yaw, RandomWalkArenaSizeCm, RandomWalkArenaMarginCm,
                RandomWalkAvoidanceLookAheadCm, bRandomWalkAvoidWorldStaticWalls,
                RandomWalkWallProbeIntervalSeconds);
        }
    }
    if (!AssignRandomWalkIdentities(Reason) || !FSharpExperimentValidation::ValidateRegistry(*this, Reason) ||
        !FSharpExperimentValidation::ValidateReadiness(*this, Reason)) { return Reject(Reason); }
	if (bReset && Identity.TrialId == MAX_int32) { return Reject(TEXT("TrialId exhausted; start a new run")); }
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy) && !bReset && (Enemy->Experiment->IsExecuting() || Enemy->Experiment->Status.State == ESharpEnemyTrialState::Suspended))
		{
			return Reject(TEXT("Stop or Reset the active group before preparing again"));
		}
	}
	TArray<ASharpEnemyCharacter*> Targets;
	TArray<ASharpSplinePath*> TargetPaths;
	for (ASharpSplinePath* Path : Paths)
	{
		Targets.Add(Path->AssignedEnemy.Get());
		TargetPaths.Add(Path);
	}
	for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
	{
		Targets.Add(Enemy);
		TargetPaths.Add(nullptr);
	}
	for (ASharpEnemyCharacter* Enemy : Targets)
	{
		if (Enemy->Experiment->Status.Generation == MAX_int32) { return Reject(TEXT("Enemy generation exhausted; start a new world")); }
	}
	TArray<FTransform> Transforms;
	if (!FSharpExperimentValidation::BuildGroupStartTransforms(*this, Targets, TargetPaths, Transforms, Reason)) { return Reject(Reason); }
	// Capture first level placement, not a later position reached by RandomWalk.
	for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies)
	{
		if (!RandomWalkInitialTransforms.Contains(Enemy)) { RandomWalkInitialTransforms.Add(Enemy, Enemy->GetActorTransform()); }
	}
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (IsValid(Enemy)) { Enemy->Experiment->Release(); }
	}
	PreparedEnemies.Reset();
	if (bReset) { ++Identity.TrialId; OnRep_Identity(); }
	LastCommandError.Empty();
	for (int32 I = 0; I < Targets.Num(); ++I)
	{
		ASharpEnemyCharacter* Enemy = Targets[I];
		PreparedEnemies.Add(Enemy);
		if (!Enemy->Experiment->Prepare(this, TargetPaths[I], Identity, Transforms[I], Reason))
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
    if ((PreparedEnemies.IsEmpty() && !AssignRandomWalkIdentities(Reason)) ||
        !FSharpExperimentValidation::ValidateRegistry(*this, Reason) ||
        !FSharpExperimentValidation::ValidateReadiness(*this, Reason)) { Reject(Reason); return; }
	if (PreparedEnemies.IsEmpty() && !PrepareGroup(false)) { return; }
	if (PreparedEnemies.Num() != Paths.Num() + RandomWalkEnemies.Num())
	{
		Reject(TEXT("Reset All after changing movement registrations")); return;
	}
	bool bAllRunning = true;
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (!IsValid(Enemy) || Enemy->Experiment->GetManager() != this || !Enemy->Experiment->ValidateBinding(Reason))
		{
			Reject(TEXT("Reset All after changing registrations: ") + Reason); return;
		}
		bAllRunning &= Enemy->Experiment->IsExecuting();
	}
	if (bAllRunning)
	{
		GetWorldTimerManager().ClearTimer(PreparationTimer); GetWorldTimerManager().ClearTimer(AutoStartTimer);
		LastCommandError.Empty(); ForceNetUpdate(); return;
	}
	CancelAutomaticControl();
	// Preflight all participants before issuing any input or policy change.
	ApplyAutomaticNetworkMode();
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (!Enemy->Experiment->CanStart(Reason))
		{
			RestoreAutomaticNetworkMode(); Reject(Enemy->GetName() + TEXT(": ") + Reason); return;
		}
	}
	LastCommandError.Empty();
	for (ASharpEnemyCharacter* Enemy : PreparedEnemies)
	{
		if (!Enemy->Experiment->Start(Reason))
		{
			for (ASharpEnemyCharacter* Target : PreparedEnemies) { if (IsValid(Target)) { Target->Experiment->Stop(); } }
			RestoreAutomaticNetworkMode();
			Reject(TEXT("Group start aborted: ") + Reason); return;
		}
	}
	UE_LOG(LogSharpControl, Display, TEXT("[Sharp][Group] Started RunId=%s TrialId=%d Count=%d ServerTime=%.6f"),
		*Identity.RunId, Identity.TrialId, PreparedEnemies.Num(), GetWorld()->GetTimeSeconds());
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
	Summary.Total = Paths.Num() + RandomWalkEnemies.Num();
	TArray<ASharpEnemyCharacter*> Targets;
	for (ASharpSplinePath* Path : Paths) { Targets.Add(IsValid(Path) ? Path->AssignedEnemy.Get() : nullptr); }
	for (ASharpEnemyCharacter* Enemy : RandomWalkEnemies) { Targets.Add(Enemy); }
	for (ASharpEnemyCharacter* Enemy : Targets)
	{
		if (!IsValid(Enemy) || !IsValid(Enemy->Experiment)) { ++Summary.Failed; continue; }
		switch (Enemy->Experiment->Status.State)
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
