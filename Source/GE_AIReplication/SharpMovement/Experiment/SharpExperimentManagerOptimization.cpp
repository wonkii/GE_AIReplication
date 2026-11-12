#include "SharpExperimentManager.h"
#include "SharpExperimentSettings.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpOptimization, Log, All);

void ASharpExperimentManager::ApplyOptimizationProfileToEnemies()
{
	if (!GetWorld() || !GetWorld()->IsGameWorld()) { return; }
	int32 AppliedCount = 0;
	for (TActorIterator<ASharpEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (ASharpEnemyCharacter* Enemy = *It)
		{
			Enemy->ApplyOptimizationProfile(*this);
			++AppliedCount;
		}
	}
	UE_LOG(LogSharpOptimization, Display,
		TEXT("[Sharp][Optimization] World=%s Role=%s Profile=%s AppliedEnemies=%d DistanceRelevancy=%d"),
		*GetWorld()->GetName(), HasAuthority() ? TEXT("Server") : TEXT("Client"),
		*StaticEnum<ESharpExperimentOptimizationPreset>()->GetNameStringByValue(static_cast<int64>(ActiveOptimizationProfile)),
		AppliedCount, HasAuthority() && Settings && ActiveOptimizationProfile == ESharpExperimentOptimizationPreset::Scalability
			&& Settings->bScalabilityEnableDistanceRelevancy);
}
