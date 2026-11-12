#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SharpIntegratedCourseObstacle.generated.h"

class UStaticMeshComponent;

// A real, blocking WorldStatic obstacle. Spawned by the server at the same
// positions for every network policy in a Batch; replicated to every client.
UCLASS()
class GE_AIREPLICATION_API ASharpIntegratedCourseObstacle : public AActor
{
    GENERATED_BODY()
public:
    ASharpIntegratedCourseObstacle();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sharp Experiment|Integrated Course")
    TObjectPtr<UStaticMeshComponent> ObstacleMesh;
};
