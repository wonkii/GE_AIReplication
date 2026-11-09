// Copyright Epic Games, Inc. All Rights Reserved.

#include "GE_AIReplication.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Combat/ReactionEnvironmentComponent.h"
#include "ComponentVisualizer.h"
#include "Components/CapsuleComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "GameFramework/Character.h"
#include "Misc/CoreDelegates.h"
#include "SceneManagement.h"
#include "UnrealEdGlobals.h"

namespace
{
class FReactionEnvironmentVisualizer final : public FComponentVisualizer
{
    void DrawVisualization(const UActorComponent* Component, const FSceneView* View,
        FPrimitiveDrawInterface* PDI) override
    {
        const auto* Environment = Cast<UReactionEnvironmentComponent>(Component);
        if (!IsValid(Environment) || !Environment->bDrawDebug) return;

        const auto* Character = Cast<ACharacter>(Environment->GetOwner());
        const auto* Capsule = IsValid(Character) ? Character->GetCapsuleComponent() : nullptr;
        if (!IsValid(Capsule)) return;

        const FVector Center = Capsule->GetComponentLocation();
        const FVector Height(0., 0., Environment->DetectionHalfHeightCm);
        const FLinearColor Color(FColor::Cyan);
        constexpr int32 Sides = 32;
        FVector Previous = Center + FVector(Environment->DetectionRadiusCm, 0., 0.);
        for (int32 Side = 1; Side <= Sides; ++Side)
        {
            const double Angle = 2. * PI * Side / Sides;
            const FVector Next = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.)
                * Environment->DetectionRadiusCm;
            PDI->DrawLine(Previous - Height, Next - Height, Color, SDPG_World);
            PDI->DrawLine(Previous + Height, Next + Height, Color, SDPG_World);
            if (Side % (Sides / 4) == 0)
                PDI->DrawLine(Next - Height, Next + Height, Color, SDPG_World);
            Previous = Next;
        }
    }
};

class FGEAIReplicationModule final : public FDefaultGameModuleImpl
{
    void StartupModule() override
    {
        if (GUnrealEd) RegisterVisualizer();
        else FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FGEAIReplicationModule::RegisterVisualizer);
    }

    void ShutdownModule() override
    {
        FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
        if (GUnrealEd)
            GUnrealEd->UnregisterComponentVisualizer(UReactionEnvironmentComponent::StaticClass()->GetFName());
    }

    void RegisterVisualizer()
    {
        if (GUnrealEd)
            GUnrealEd->RegisterComponentVisualizer(UReactionEnvironmentComponent::StaticClass()->GetFName(),
                MakeShared<FReactionEnvironmentVisualizer>());
    }
};
}
#else
using FGEAIReplicationModule = FDefaultGameModuleImpl;
#endif

IMPLEMENT_PRIMARY_GAME_MODULE(FGEAIReplicationModule, GE_AIReplication, "GE_AIReplication");

DEFINE_LOG_CATEGORY(LogGE_AIReplication)
