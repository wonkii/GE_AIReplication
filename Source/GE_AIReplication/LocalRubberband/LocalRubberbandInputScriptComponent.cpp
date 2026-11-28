#include "LocalRubberbandInputScriptComponent.h"
#include "LocalRubberbandRecordingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogLocalRubberbandScript, Log, All);

namespace LocalRubberbandScriptDetail
{
	FLocalRubberbandScriptStep LrbStep(const TCHAR* Name, ELocalRubberbandStepType Type, float Yaw, float Scale, float Duration)
	{
		FLocalRubberbandScriptStep Result;
		Result.Name = Name; Result.Type = Type; Result.YawDegrees = Yaw; Result.InputScale = Scale; Result.Duration = Duration;
		return Result;
	}

}

using LocalRubberbandScriptDetail::LrbStep;

ULocalRubberbandInputScriptComponent::ULocalRubberbandInputScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

TArray<FLocalRubberbandScriptStep> ULocalRubberbandInputScriptComponent::MakeDefaultSteps()
{
	using T = ELocalRubberbandStepType;
	return {
		LrbStep(TEXT("Forward"),    T::Move,     0.f,   1.f, 2.0f),
		LrbStep(TEXT("Stop"),       T::Move,     0.f,   0.f, 1.0f),
		LrbStep(TEXT("TurnRight"),  T::Move,     90.f,  1.f, 1.5f),
		LrbStep(TEXT("Reverse"),    T::Move,     270.f, 1.f, 1.5f),
		LrbStep(TEXT("Stop"),       T::Move,     0.f,   0.f, 0.5f),
		LrbStep(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		LrbStep(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		LrbStep(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		LrbStep(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		LrbStep(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		LrbStep(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		LrbStep(TEXT("JumpForward"),T::JumpMove, 0.f,   1.f, 1.2f),
		LrbStep(TEXT("Stop"),       T::Move,     0.f,   0.f, 1.0f),
		LrbStep(TEXT("ReturnHome"), T::ReturnHome, 0.f, 1.f, 8.0f),
		LrbStep(TEXT("Rest"),       T::Move,     0.f,   0.f, 1.0f),
	};
}

void ULocalRubberbandInputScriptComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// Input must be added before the movement component consumes it this frame.
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
	}
}

bool ULocalRubberbandInputScriptComponent::StartScript(int32 LoopCount)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bRunning || !Pawn || !Pawn->IsLocallyControlled() || Pawn->GetNetMode() != NM_Client)
	{
		UE_LOG(LogLocalRubberbandScript, Warning, TEXT("InputScript can only run on the locally controlled character (%s)"), *GetNameSafe(GetOwner()));
		return false;
	}
	ActiveSteps = Steps.Num() > 0 ? Steps : MakeDefaultSteps();
	LoopTarget = LoopCount > 0 ? LoopCount : Loops;
	Origin = Pawn->GetActorLocation();
	Loop = 0; StepIndex = 0; bRunning = true;
	SetComponentTickEnabled(true);
	Mark(TEXT("ScriptStart"));
	EnterStep();
	UE_LOG(LogLocalRubberbandScript, Log, TEXT("InputScript started: %d loops x %d steps"), LoopTarget, ActiveSteps.Num());
	return true;
}

void ULocalRubberbandInputScriptComponent::StopScript(FName Reason)
{
	if (!bRunning) { return; }
	LeaveStep();
	bRunning = false;
	SetComponentTickEnabled(false);
	Mark(FString::Printf(TEXT("ScriptStop:%s"), *Reason.ToString()));
	UE_LOG(LogLocalRubberbandScript, Log, TEXT("InputScript stopped (%s)"), *Reason.ToString());
}

void ULocalRubberbandInputScriptComponent::EnterStep()
{
	StepElapsed = 0.f;
	const FLocalRubberbandScriptStep& Current = ActiveSteps[StepIndex];
	if (Current.Type == ELocalRubberbandStepType::JumpMove)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner())) { Character->Jump(); }
	}
	Mark(TEXT("StepEnter"));
}

void ULocalRubberbandInputScriptComponent::LeaveStep()
{
	if (ActiveSteps.IsValidIndex(StepIndex) && ActiveSteps[StepIndex].Type == ELocalRubberbandStepType::JumpMove)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner())) { Character->StopJumping(); }
	}
}

void ULocalRubberbandInputScriptComponent::Mark(const FString& Event) const
{
	ULocalRubberbandRecordingSubsystem* Recorder = GetWorld() ? GetWorld()->GetSubsystem<ULocalRubberbandRecordingSubsystem>() : nullptr;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Recorder || !Pawn) { return; }
	const FName StepName = ActiveSteps.IsValidIndex(StepIndex) ? ActiveSteps[StepIndex].Name : NAME_None;
	Recorder->RecordScriptEvent(*Pawn, Event, Loop, StepIndex, StepName);
}

void ULocalRubberbandInputScriptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!bRunning || !Pawn) { return; }
	if (!Pawn->IsLocallyControlled()) { StopScript(TEXT("LostControl")); return; }

	StepElapsed += DeltaTime;
	const FLocalRubberbandScriptStep& Current = ActiveSteps[StepIndex];
	bool bStepDone = StepElapsed >= Current.Duration;

	if (Current.Type == ELocalRubberbandStepType::ReturnHome)
	{
		const FVector ToHome = FVector(Origin.X - Pawn->GetActorLocation().X, Origin.Y - Pawn->GetActorLocation().Y, 0.f);
		if (ToHome.Size() <= ArriveRadius) { bStepDone = true; }
		else { Pawn->AddMovementInput(ToHome.GetSafeNormal(), Current.InputScale); }
	}
	else if (Current.InputScale > 0.f)
	{
		Pawn->AddMovementInput(FRotator(0.f, Current.YawDegrees, 0.f).Vector(), Current.InputScale);
	}

	if (!bStepDone) { return; }
	LeaveStep();
	if (++StepIndex >= ActiveSteps.Num())
	{
		StepIndex = 0;
		if (++Loop >= LoopTarget) { StepIndex = ActiveSteps.Num() - 1; StopScript(TEXT("Completed")); return; }
	}
	EnterStep();
}
