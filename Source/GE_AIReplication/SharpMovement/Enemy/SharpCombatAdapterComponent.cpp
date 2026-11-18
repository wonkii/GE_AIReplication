#include "SharpCombatAdapterComponent.h"
#include "SharpEnemyCharacter.h"
#include "Combat/HitReactionComponent.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Prediction/SharpVisualPredictorComponent.h"
#include "Engine/World.h"

void USharpCombatAdapterComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Enemy = Cast<ASharpEnemyCharacter>(GetOwner()))
        Enemy->HitReaction->OnReactionChanged.AddUniqueDynamic(this, &ThisClass::ReactionChanged);
}

void USharpCombatAdapterComponent::ReactionChanged(bool bActive, int32 ReactionId)
{
	if (!GetWorld() || GetWorld()->bIsTearingDown) return;
    if (auto* Enemy = Cast<ASharpEnemyCharacter>(GetOwner()))
    {
        if (Enemy->HasAuthority())
        {
            if (bActive) Enemy->Experiment->BeginCombatSuspension(ReactionId);
            else Enemy->Experiment->EndCombatSuspension(ReactionId);
        }
        Enemy->VisualPredictor->HandleCombatChanged();
    }
}

void USharpCombatAdapterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Enemy = Cast<ASharpEnemyCharacter>(GetOwner()))
        Enemy->HitReaction->OnReactionChanged.RemoveDynamic(this, &ThisClass::ReactionChanged);
    Super::EndPlay(Reason);
}
