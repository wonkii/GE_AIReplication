#include "RubberbandInputScriptComponent.h"
#include "RubberbandRecordingSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRubberbandScript, Log, All);

namespace
{
	FRubberbandScriptStep Step(const TCHAR* Name, ERubberbandStepType Type, float Yaw, float Scale, float Duration)
	{
		FRubberbandScriptStep Result;
		Result.Name = Name; Result.Type = Type; Result.YawDegrees = Yaw; Result.InputScale = Scale; Result.Duration = Duration;
		return Result;
	}

	URubberbandInputScriptComponent* FindLocalScript(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<URubberbandInputScriptComponent>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs StartCommand(TEXT("rb.Script.Start"),
		TEXT("Start the scripted movement on this window's local character. Optional arg: loop count."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			URubberbandInputScriptComponent* Script = FindLocalScript(World);
			if (!Script) { UE_LOG(LogRubberbandScript, Warning, TEXT("rb.Script.Start: no local character with InputScript in %s"), *GetNameSafe(World)); return; }
			Script->StartScript(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}));

	void SetVariable(const TCHAR* Name, const FString& Value)
	{
		// SetByConsole: values typed earlier in the console have that priority, and lower-priority sets are ignored.
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) { Variable->Set(*Value, ECVF_SetByConsole); }
	}

	FAutoConsoleCommandWithWorldAndArgs RunCommand(TEXT("rb.Run"),
		TEXT("One-shot B run, typed in the window of the client that should move (Client B).\n")
		TEXT("rb.Run <Condition> [Loops=5] [SmoothMode=-1 keep|0 Disabled|1 Linear|2 Exponential] [SmoothTime=-1 keep] [RunId=auto]\n")
		TEXT("Sets rb.Condition/RunId/Smooth.*, rb.Record 1, starts the script after 0.5 s, rb.Record 0 1 s after it completes."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			URubberbandInputScriptComponent* Script = FindLocalScript(World);
			if (!Script || !World) { UE_LOG(LogRubberbandScript, Warning, TEXT("rb.Run: run it in the window of the character that should move")); return; }
			if (Script->IsRunning()) { UE_LOG(LogRubberbandScript, Warning, TEXT("rb.Run: script already running")); return; }
			const FString Condition = Args.Num() > 0 ? Args[0] : FString(TEXT("Unlabeled"));
			const int32 Loops = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
			const int32 Mode = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : -1;
			const FString Time = Args.Num() > 3 ? Args[3] : FString(TEXT("-1"));
			static const TCHAR* ModeNames[] = { TEXT("Dis"), TEXT("Lin"), TEXT("Exp") };
			const FString RunId = Args.Num() > 4 ? Args[4]
				: FDateTime::Now().ToString(TEXT("%m%d-%H%M%S")) + TEXT("_") + (Mode >= 0 && Mode <= 2 ? ModeNames[Mode] : TEXT("Keep"));
			SetVariable(TEXT("rb.Record"), TEXT("0"));
			SetVariable(TEXT("rb.Condition"), Condition);
			SetVariable(TEXT("rb.RunId"), RunId);
			SetVariable(TEXT("rb.Smooth.Mode"), FString::FromInt(Mode));
			SetVariable(TEXT("rb.Smooth.LocationTime"), Time);
			SetVariable(TEXT("rb.Record"), TEXT("1"));
			UE_LOG(LogRubberbandScript, Log, TEXT("rb.Run: RunId=%s Condition=%s Mode=%d SmoothTime=%s -> starting in 0.5 s"), *RunId, *Condition, Mode, *Time);
			// Give every world (server, other clients) a tick to open its recording before the first step mark.
			TWeakObjectPtr<URubberbandInputScriptComponent> Weak(Script);
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak, Loops]()
			{
				if (URubberbandInputScriptComponent* Pending = Weak.Get())
				{
					Pending->bStopRecordingOnComplete = true;
					if (!Pending->StartScript(Loops)) { SetVariable(TEXT("rb.Record"), TEXT("0")); }
				}
			}), 0.5f, false);
		}));

	FAutoConsoleCommandWithWorldAndArgs StopCommand(TEXT("rb.Script.Stop"),
		TEXT("Stop the scripted movement on this window's local character."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (URubberbandInputScriptComponent* Script = FindLocalScript(World)) { Script->StopScript(TEXT("Command")); }
		}));
}

URubberbandInputScriptComponent::URubberbandInputScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

TArray<FRubberbandScriptStep> URubberbandInputScriptComponent::MakeDefaultSteps()
{
	using T = ERubberbandStepType;
	return {
		Step(TEXT("Forward"),    T::Move,     0.f,   1.f, 2.0f),
		Step(TEXT("Stop"),       T::Move,     0.f,   0.f, 1.0f),
		Step(TEXT("TurnRight"),  T::Move,     90.f,  1.f, 1.5f),
		Step(TEXT("Reverse"),    T::Move,     270.f, 1.f, 1.5f),
		Step(TEXT("Stop"),       T::Move,     0.f,   0.f, 0.5f),
		Step(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		Step(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		Step(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		Step(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		Step(TEXT("Zig"),        T::Move,     45.f,  1.f, 0.4f),
		Step(TEXT("Zag"),        T::Move,     315.f, 1.f, 0.4f),
		Step(TEXT("JumpForward"),T::JumpMove, 0.f,   1.f, 1.2f),
		Step(TEXT("Stop"),       T::Move,     0.f,   0.f, 1.0f),
		Step(TEXT("ReturnHome"), T::ReturnHome, 0.f, 1.f, 8.0f),
		Step(TEXT("Rest"),       T::Move,     0.f,   0.f, 1.0f),
	};
}

void URubberbandInputScriptComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// Input must be added before the movement component consumes it this frame.
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
	}
}

bool URubberbandInputScriptComponent::StartScript(int32 LoopCount)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		UE_LOG(LogRubberbandScript, Warning, TEXT("InputScript can only run on the locally controlled character (%s)"), *GetNameSafe(GetOwner()));
		return false;
	}
	ActiveSteps = Steps.Num() > 0 ? Steps : MakeDefaultSteps();
	LoopTarget = LoopCount > 0 ? LoopCount : Loops;
	Origin = Pawn->GetActorLocation();
	Loop = 0; StepIndex = 0; bRunning = true;
	SetComponentTickEnabled(true);
	Mark(TEXT("ScriptStart"));
	EnterStep();
	UE_LOG(LogRubberbandScript, Log, TEXT("InputScript started: %d loops x %d steps"), LoopTarget, ActiveSteps.Num());
	return true;
}

void URubberbandInputScriptComponent::StopScript(FName Reason)
{
	if (!bRunning) { return; }
	LeaveStep();
	bRunning = false;
	SetComponentTickEnabled(false);
	Mark(FString::Printf(TEXT("ScriptStop:%s"), *Reason.ToString()));
	if (bStopRecordingOnComplete && GetWorld())
	{
		bStopRecordingOnComplete = false;
		// Keep recording briefly so the last correction and smoothing settle are captured.
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]()
		{
			SetVariable(TEXT("rb.Record"), TEXT("0"));
			UE_LOG(LogRubberbandScript, Log, TEXT("rb.Run: recording stopped"));
		}), 1.0f, false);
	}
	UE_LOG(LogRubberbandScript, Log, TEXT("InputScript stopped (%s)"), *Reason.ToString());
}

void URubberbandInputScriptComponent::EnterStep()
{
	StepElapsed = 0.f;
	const FRubberbandScriptStep& Current = ActiveSteps[StepIndex];
	if (Current.Type == ERubberbandStepType::JumpMove)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner())) { Character->Jump(); }
	}
	Mark(TEXT("StepEnter"));
}

void URubberbandInputScriptComponent::LeaveStep()
{
	if (ActiveSteps.IsValidIndex(StepIndex) && ActiveSteps[StepIndex].Type == ERubberbandStepType::JumpMove)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner())) { Character->StopJumping(); }
	}
}

void URubberbandInputScriptComponent::Mark(const FString& Event) const
{
	URubberbandRecordingSubsystem* Recorder = GetWorld() ? GetWorld()->GetSubsystem<URubberbandRecordingSubsystem>() : nullptr;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Recorder || !Pawn) { return; }
	const FName StepName = ActiveSteps.IsValidIndex(StepIndex) ? ActiveSteps[StepIndex].Name : NAME_None;
	Recorder->RecordScriptEvent(*Pawn, Event, Loop, StepIndex, StepName);
}

void URubberbandInputScriptComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!bRunning || !Pawn) { return; }
	if (!Pawn->IsLocallyControlled()) { StopScript(TEXT("LostControl")); return; }

	StepElapsed += DeltaTime;
	const FRubberbandScriptStep& Current = ActiveSteps[StepIndex];
	bool bStepDone = StepElapsed >= Current.Duration;

	if (Current.Type == ERubberbandStepType::ReturnHome)
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
