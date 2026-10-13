#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpMovementDiagnosticsComponent.generated.h"

class ASharpEnemyCharacter;
struct FRepMovement;

// Low-frequency observations only; does not steer, detect, or change replication.
UCLASS(ClassGroup=(SharpMovement))
class GE_AIREPLICATION_API USharpMovementDiagnosticsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USharpMovementDiagnosticsComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sharp Diagnostics") bool bEnableLogging = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sharp Diagnostics", meta=(ClampMin="0.01", Units="s"))
	float LogIntervalSeconds = 0.5f;
	void HandleSessionChanged();
	void ObserveMovementReceipt(const FRepMovement& Received, const FVector& BeforePosition, const FVector& BeforeVelocity, uint8 BeforeMode);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	UPROPERTY(Transient) TObjectPtr<ASharpEnemyCharacter> Enemy;
	int32 Generation = -1;
	int32 Segment = -1;
	uint64 ReceiveCount = 0, IntervalSamples = 0, CorrectionSamples = 0;
	double TotalIntervalMs = 0, MaxIntervalMs = 0, TotalCorrectionCm = 0, MaxCorrectionCm = 0;
	double LastReceiveTime = -1, LastServerLogTime = -1, LastClientLogTime = -1;
	bool bSummaryLogged = false;
	bool bFinalSampleLogged = false;
	bool bHasMovementReceipt = false;
	FString ContextString() const;
	double ServerTime() const;
	bool IsObservationWindow() const;
	void LogCompletionSummary(double LocalTime);
	UFUNCTION() void ObserveServerMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
};
