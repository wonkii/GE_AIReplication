#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharpSplineMovementTypes.h"
#include "SharpSplineFollowerComponent.generated.h"

class ACharacter;
class ASharpSplinePath;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSharpFollowStateChanged, ESharpSplineFollowState, State, const FString&, Reason);

/** Generates server input before CMC; observes actual movement after CMC. Never teleports. */
UCLASS(ClassGroup = (SharpMovement), meta = (BlueprintSpawnableComponent))
class GE_AIREPLICATION_API USharpSplineFollowerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USharpSplineFollowerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sharp Follow")
	FSharpSplineFollowSettings Settings;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Follow|Runtime")
	ESharpSplineFollowState State = ESharpSplineFollowState::Idle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Follow|Runtime")
	float ActualDistance = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Follow|Runtime")
	float TargetDistance = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Follow|Runtime")
	float CrossTrackError = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Sharp Follow|Runtime")
	FString LastFailure;

	UPROPERTY(BlueprintAssignable, Category = "Sharp Follow")
	FSharpFollowStateChanged OnStateChanged;

	bool Prepare(ASharpSplinePath* InPath, FString& OutReason);
	const FSharpSplineFollowSettings& GetPreparedSettings() const { return PreparedSettings; }
	bool Start(FString& OutReason);
	void Suspend();
	bool Resume(FString& OutReason);
	void Reset();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Character;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> Movement;

	UPROPERTY(Transient)
	TObjectPtr<ASharpSplinePath> Path;

	FTransform PreparedPathTransform;
	int32 PreparedSplineVersion = 0;
	FName PreparedPathId;
	int32 PreparedPathRevision = 0;
	FSharpSplineFollowSettings PreparedSettings;
	float OriginalMaxWalkSpeed = 0.0f;
	float ProgressReference = 0.0f;
	float SecondsWithoutProgress = 0.0f;
	bool bSpeedOverridden = false;

	bool ValidateRuntime(FString& OutReason) const;
	void SetState(ESharpSplineFollowState NewState, const FString& Reason = FString());
	void RestoreSpeed();

	UFUNCTION()
	void ObserveMovement(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
};
