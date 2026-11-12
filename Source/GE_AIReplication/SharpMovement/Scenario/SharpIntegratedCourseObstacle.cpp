#include "SharpIntegratedCourseObstacle.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ASharpIntegratedCourseObstacle::ASharpIntegratedCourseObstacle()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);

    ObstacleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleMesh"));
    SetRootComponent(ObstacleMesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) { ObstacleMesh->SetStaticMesh(Cube.Object); }
    // A dynamically spawned component cannot be a prebuilt static scene object,
    // but the collision object channel must be WorldStatic for SharpRandomWalker.
    ObstacleMesh->SetMobility(EComponentMobility::Movable);
    ObstacleMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ObstacleMesh->SetCollisionObjectType(ECC_WorldStatic);
    ObstacleMesh->SetCollisionResponseToAllChannels(ECR_Block);
    ObstacleMesh->SetGenerateOverlapEvents(false);
}
