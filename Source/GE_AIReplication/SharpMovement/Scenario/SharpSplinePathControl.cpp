#include "SharpSplinePath.h"
#include "SharpEnemyPlacement.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpExperimentManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

ASharpExperimentManager* ASharpSplinePath::FindRegisteredManager()
{
	ASharpExperimentManager* Result = nullptr;
	for (TActorIterator<ASharpExperimentManager> It(GetWorld()); It; ++It)
	{
		if (Result) { LastCommandError = TEXT("Only one Manager is supported per world"); return nullptr; }
		Result = *It;
	}
	if (!Result || !Result->IsRegistered(this))
	{
		LastCommandError = TEXT("Add this Spline to ExperimentManager.Paths"); return nullptr;
	}
	return Result;
}

bool ASharpSplinePath::PlaceEditorEnemy()
{
	if (!GetWorld() || GetWorld()->WorldType != EWorldType::Editor || !IsValid(AssignedEnemy))
	{
		LastCommandError = TEXT("Assign a SharpEnemy instance in the editor world"); return false;
	}
	for (TActorIterator<ASharpSplinePath> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->AssignedEnemy == AssignedEnemy)
		{
			LastCommandError = TEXT("This Enemy is already assigned to another Spline"); return false;
		}
	}
	FTransform Start;
	if (!FSharpEnemyPlacement::FindStart(*AssignedEnemy, *this, {}, Start, LastCommandError)) { return false; }
#if WITH_EDITOR
	AssignedEnemy->Modify();
#endif
	return FSharpEnemyPlacement::Place(*AssignedEnemy, Start, LastCommandError);
}

void ASharpSplinePath::SetEnemyAtStart()
{
	LastCommandError.Empty();
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
#if WITH_EDITOR
		const FScopedTransaction Transaction(NSLOCTEXT("SharpMovement", "PlaceEnemy", "Place Enemy at Spline Start"));
		Modify();
#endif
		PlaceEditorEnemy(); return;
	}
	if (ASharpExperimentManager* Manager = FindRegisteredManager())
	{
		if (!Manager->PrepareEnemy(this)) { LastCommandError = Manager->LastCommandError; }
	}
}

void ASharpSplinePath::StartEnemy()
{
	LastCommandError.Empty();
	if (ASharpExperimentManager* Manager = FindRegisteredManager())
	{
		if (!Manager->StartEnemy(this)) { LastCommandError = Manager->LastCommandError; }
	}
}

void ASharpSplinePath::StopEnemy()
{
	LastCommandError.Empty();
	if (ASharpExperimentManager* Manager = FindRegisteredManager())
	{
		if (!Manager->StopEnemy(this)) { LastCommandError = Manager->LastCommandError; }
	}
}

void ASharpSplinePath::ResetEnemy()
{
	LastCommandError.Empty();
	if (ASharpExperimentManager* Manager = FindRegisteredManager())
	{
		if (!Manager->ResetEnemy(this)) { LastCommandError = Manager->LastCommandError; }
	}
}

#if WITH_EDITOR
void ASharpSplinePath::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	const FName PropertyName = Event.GetPropertyName();
	Super::PostEditChangeProperty(Event);
	// Extend the property editor's transaction instead of opening a nested one.
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ASharpSplinePath, AssignedEnemy) && AssignedEnemy &&
		GetWorld() && GetWorld()->WorldType == EWorldType::Editor) { PlaceEditorEnemy(); }
}

void ASharpSplinePath::PostEditUndo()
{
	Super::PostEditUndo();
	LastCommandError.Empty();
	// Enemy transform is part of the original transaction; do not reposition it
	// here or Undo would create another edit and overwrite the restored transform.
}
#endif
