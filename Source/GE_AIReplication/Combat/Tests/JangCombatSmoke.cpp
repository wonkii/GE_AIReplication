#if !UE_BUILD_SHIPPING
#include "Combat/MeleeAttackComponent.h"
#include "Combat/HitReactionComponent.h"
#include "GE_AIReplicationCharacter.h"
#include "SharpMovement/Enemy/SharpEnemyCharacter.h"
#include "SharpMovement/Experiment/SharpEnemyExperimentComponent.h"
#include "SharpMovement/Experiment/SharpExperimentManager.h"
#include "SharpMovement/Prediction/SharpVisualPredictorComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"

namespace
{
// These fixtures only change the transient game world, never the saved map or BP defaults.
struct FJangCombatSmoke : TSharedFromThis<FJangCombatSmoke>
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UGameInstance> GameInstance;
    TWeakObjectPtr<AGE_AIReplicationCharacter> Player;
    TWeakObjectPtr<ASharpEnemyCharacter> Enemy;
    TWeakObjectPtr<ASharpExperimentManager> Manager;
    TWeakObjectPtr<ATargetPoint> Attacker;
    TWeakObjectPtr<AStaticMeshActor> Wall;
    bool bClient = false, bFailed = false, bSawClientReaction = false;
    bool bObserver = false;
    int32 DirectionCase = 0;
    uint8 ObservedDirections = 0;
    uint8 PresentedDirections = 0;
    TMap<TWeakObjectPtr<ASharpEnemyCharacter>, int32> ObservedReactions;
    TMap<TWeakObjectPtr<ASharpEnemyCharacter>, FVector> ClientReactionStarts;
    FVector ExpectedFacing;
    int32 Stage = 0, Segment = 0, Generation = 0, HitsBefore = 0, ClientRequests = 0;
    double Began = FPlatformTime::Seconds(), StageTime = 0, ClientNextRequest = 0;
    FVector ReactionStart;
    FVector AttackPlayerStart;
    float AttackPlayerMaxDistance = 0.f;
    TMap<int32, double> ObservedInputWindows;
    TSet<int32> RequestedComboIds;
    bool bEarlyInputSent = false, bLateInputSent = false, bSawThirdInputWindow = false, bSawAttackCollision = false;

    void DriveNotifyInputs()
    {
        auto* Attack = Player->MeleeAttack.Get();
        if (ClientRequests == 0 && Elapsed() > 0.6)
        { Attack->RequestAttack(); ++ClientRequests; return; }
        const int32 Id = Attack->GetPlayedAttackId();
        if (Id == 1 && !Attack->IsComboInputWindowOpen() && !bEarlyInputSent && Elapsed() > 0.72)
        { Attack->RequestAttack(); bEarlyInputSent = true; }
        if (Id <= 2 && Id > 0 && Attack->IsComboInputWindowOpen() && !RequestedComboIds.Contains(Id))
        {
            double* OpenedAt = ObservedInputWindows.Find(Id);
            if (!OpenedAt) { ObservedInputWindows.Add(Id, World->GetTimeSeconds()); return; }
            if (World->GetTimeSeconds() - *OpenedAt >= 0.06)
            {
                Attack->RequestAttack(); Attack->RequestAttack(); // One window stores only one next input.
                RequestedComboIds.Add(Id); ++ClientRequests;
            }
        }
        if (Id == 3 && Attack->IsComboInputWindowOpen()) bSawThirdInputWindow = true;
        if (Id == 3 && bSawThirdInputWindow && !Attack->IsComboInputWindowOpen() && Attack->IsAttacking() && !bLateInputSent)
        { Attack->RequestAttack(); bLateInputSent = true; }
    }

    bool Check(bool bCondition, const TCHAR* Label)
    {
        if (!bCondition) { UE_LOG(LogTemp, Error, TEXT("[JangCombatSmoke] FAIL %s Stage=%d"), Label, Stage); bFailed = true; }
        return bCondition;
    }
    void Next(int32 NewStage) { Stage = NewStage; StageTime = World->GetTimeSeconds(); }
    double Elapsed() const { return World->GetTimeSeconds() - StageTime; }
    void FaceTarget(bool bFront = true)
    {
        const FVector Forward = Enemy->GetActorForwardVector();
        const FVector Position = Enemy->GetActorLocation() + Forward * (bFront ? 150.f : -150.f);
        if (Attacker.IsValid()) Attacker->SetActorLocation(Position);
        if (Player.IsValid())
        {
            Player->GetCharacterMovement()->StopMovementImmediately();
            const FRotator Rotation = (Enemy->GetActorLocation() - Position).Rotation();
            Player->SetActorLocationAndRotation(Position, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
            if (auto* PC = Cast<APlayerController>(Player->GetController())) PC->SetControlRotation(Rotation);
            Player->ForceNetUpdate();
        }
    }
    bool DeliverHit()
    {
        FaceTarget();
        FCombatHit Hit; Hit.Attacker = Attacker.Get(); Hit.AttackId = 100 + Stage;
        return ICombatHitReceiver::Execute_ReceiveCombatHit(Enemy->HitReaction, Hit);
    }
    bool Tick(float)
    {
        if (bClient && GameInstance.IsValid()) World = GameInstance->GetWorld();
        if (!World.IsValid()) return false;
        if (FPlatformTime::Seconds() - Began > 70) Check(false, TEXT("Timeout"));
        if (bFailed) { FPlatformMisc::RequestExitWithStatus(false, 1); return false; }
        if (bClient) return TickClient();
        if (Stage == 0)
        {
            for (TActorIterator<ASharpExperimentManager> It(World.Get()); It; ++It) Manager = *It;
            for (TActorIterator<ASharpEnemyCharacter> It(World.Get()); It; ++It)
                if (It->ExperimentActorId == 1) Enemy = *It;
            if (!Manager.IsValid() || !Enemy.IsValid() || Enemy->Experiment->Status.State != ESharpEnemyTrialState::Ready) return true;
            for (auto It = World->GetPlayerControllerIterator(); It; ++It)
            {
                auto* PC = It->Get();
                if (!PC->GetPawn() && World->GetNetMode() == NM_Standalone) World->GetAuthGameMode()->RestartPlayer(PC);
                if (auto* Character = Cast<AGE_AIReplicationCharacter>(PC->GetPawn())) { Player = Character; break; }
            }
            if (!Player.IsValid()) return true;
            if (!Check(Player->MeleeAttack->bCombatEnabled && Enemy->HitReaction->bCombatEnabled, TEXT("Existing BP components enabled"))) return true;
            FaceTarget();
            UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] Ready Target=%s Position=%s"), *Enemy->GetName(), *Enemy->GetActorLocation().ToString());
            ReactionStart = Enemy->GetActorLocation(); AttackPlayerStart = Player->GetActorLocation(); Next(1);
        }
        else if (Stage == 1)
        {
            // Both roles follow real animation Notify windows, rather than test timer-based buffering.
            if (World->GetNetMode() == NM_Standalone) DriveNotifyInputs();
            Check(Player->MeleeAttack->IsHitWindowOpen() == Player->MeleeAttack->IsAttackCollisionEnabled(), TEXT("Collision is enabled exactly inside hit Notify"));
            bSawAttackCollision |= Player->MeleeAttack->IsAttackCollisionEnabled();
            AttackPlayerMaxDistance = FMath::Max(AttackPlayerMaxDistance, FVector::Dist2D(AttackPlayerStart, Player->GetActorLocation()));
            if (Elapsed() < 4.5) return true;
            Check(Player->MeleeAttack->GetPlayedAttackId() == 3 && !Player->MeleeAttack->IsAttacking() && !Player->MeleeAttack->IsHitWindowOpen() &&
                !Player->MeleeAttack->IsComboInputWindowOpen() && !Player->MeleeAttack->HasBufferedComboInput(), TEXT("Notify-gated three-hit chain and complete window cleanup"));
            Check(bSawAttackCollision, TEXT("Hit Notify activated actual query collision"));
            Check(AttackPlayerMaxDistance > 20.f, TEXT("Attack Root Motion moves the player capsule"));
            UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] PlayerRootMotionDistanceCm=%.3f"), AttackPlayerMaxDistance);
            if (World->GetNetMode() == NM_Standalone) Check(bEarlyInputSent && bLateInputSent && RequestedComboIds.Num() == 2, TEXT("Early/late clicks rejected and one input per window"));
            Check(Player->MeleeAttack->GetAcceptedHitCount() >= 1 && Enemy->HitReaction->GetReactionId() >= 1, TEXT("Socket Notify sweep hits existing Sharp Enemy"));
            Check(FVector::Dist2D(ReactionStart, Enemy->GetActorLocation()) > 20, TEXT("Reaction root motion moves capsule"));
            Check(Enemy->Experiment->Status.State == ESharpEnemyTrialState::Ready, TEXT("Ready Enemy does not auto-start after reaction"));
            Player->MeleeAttack->RequestAttack(); Next(11);
        }
        else if (Stage == 11 && Player->MeleeAttack->IsComboInputWindowOpen())
        {
            Player->MeleeAttack->RequestAttack();
            Check(Player->MeleeAttack->HasBufferedComboInput(), TEXT("Cancel fixture has a Notify-buffered input"));
            Player->MeleeAttack->CancelAttack(); Next(12);
        }
        else if (Stage == 12 && Elapsed() > 1.3)
        {
            Check(Player->MeleeAttack->GetPlayedAttackId() == 4 && !Player->MeleeAttack->IsAttacking() &&
                !Player->MeleeAttack->IsAttackCollisionEnabled() && !Player->MeleeAttack->IsComboInputWindowOpen() &&
                !Player->MeleeAttack->HasBufferedComboInput(), TEXT("Interrupted Notify clears collision and discards pending next attack"));
            Attacker = World->SpawnActor<ATargetPoint>();
            Manager->StartAll(); Segment = Enemy->Experiment->Status.Segment; Generation = Enemy->Experiment->Status.Generation;
            Next(2);
        }
        else if (Stage == 2 && Elapsed() > 0.1)
        {
            ReactionStart = Enemy->GetActorLocation();
            Check(DeliverHit(), TEXT("Running Enemy accepts frontal hit"));
            Check(Enemy->Experiment->Status.State == ESharpEnemyTrialState::Suspended, TEXT("Spline suspends during reaction"));
            Check(!DeliverHit(), TEXT("Repeated hit cannot restart reaction")); Next(3);
        }
        else if (Stage == 3 && Elapsed() > 1.1)
        {
            Check(!Enemy->HitReaction->IsReacting() && Enemy->Experiment->IsExecuting(), TEXT("Spline resumes after montage"));
            Check(Enemy->Experiment->Status.Segment == Segment && Enemy->Experiment->Status.Generation == Generation, TEXT("Reaction preserves recording generation and segment"));
            Check(Enemy->Experiment->SuspendForImpulse(0.15f), TEXT("Impulse suspension setup"));
            Check(DeliverHit(), TEXT("Combat can overlap an existing impulse suspension")); Next(4);
        }
        else if (Stage == 4 && Elapsed() > 1.1)
        {
            Check(Enemy->Experiment->IsExecuting() && !Enemy->HitReaction->IsReacting(), TEXT("Impulse timer cannot resume early or strand combat suspension"));
            Check(DeliverHit(), TEXT("Stop cancellation setup")); Manager->StopAll(); Next(5);
        }
        else if (Stage == 5 && Elapsed() > 1.1)
        {
            Check(!Enemy->HitReaction->IsReacting() && Enemy->Experiment->Status.State == ESharpEnemyTrialState::Stopped, TEXT("Stop cancels reaction without stale resume"));
            Manager->StartAll(); Next(6);
        }
        else if (Stage == 6 && Elapsed() > 0.1)
        {
            Check(DeliverHit(), TEXT("Reset cancellation setup"));
            // Do not let the transient test player obstruct the map's validated start capsule.
            Player->SetActorLocation(FVector(0, 0, 400), false, nullptr, ETeleportType::TeleportPhysics);
            Manager->ResetAll(); Next(7);
        }
        else if (Stage == 7 && Elapsed() > 1.1)
        {
            Check(!Enemy->HitReaction->IsReacting() && Enemy->Experiment->Status.State == ESharpEnemyTrialState::Ready && Enemy->Experiment->Status.Generation > Generation, TEXT("Reset cancels old reaction and preserves Ready state"));
            Check(DeliverHit(), TEXT("Montage interruption setup"));
            Enemy->GetMesh()->GetAnimInstance()->Montage_Stop(0.f);
            Next(71);
        }
        else if (Stage == 71 && Elapsed() > 0.1)
        {
            Check(!Enemy->HitReaction->IsReacting(), TEXT("Montage interruption releases reaction"));
            FaceTarget();
            Wall = World->SpawnActor<AStaticMeshActor>();
            Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            Wall->SetActorLocationAndRotation(Enemy->GetActorLocation() + Enemy->GetActorForwardVector() * 75.f, Enemy->GetActorRotation());
            Wall->SetActorScale3D(FVector(0.1f, 3.f, 4.f));
            HitsBefore = Player->MeleeAttack->GetAcceptedHitCount(); Player->MeleeAttack->RequestAttack(); Next(8);
        }
        else if (Stage == 8 && Elapsed() > 1.6)
        {
            Check(Player->MeleeAttack->GetAcceptedHitCount() == HitsBefore, TEXT("Wall blocks melee hit"));
            if (Wall.IsValid()) Wall->Destroy();
            FaceTarget(false); Player->MeleeAttack->RequestAttack(); Next(9);
        }
        else if (Stage == 9 && Elapsed() > 1.6)
        {
            Check(Player->MeleeAttack->GetAcceptedHitCount() > HitsBefore, TEXT("Rear socket sweep hit accepted without frontal gate"));
            Player->SetActorLocation(FVector(0, 0, 400), false, nullptr, ETeleportType::TeleportPhysics);
            Manager->StopAll(); Next(90);
        }
        else if (Stage == 90 && Elapsed() > 0.15)
        {
            // Rotate the fixture itself to catch accidental world-axis classification.
            Enemy->SetActorRotation(FRotator(0.f, 37.f, 0.f));
            const float Offsets[] = { 20.f, 160.f, -80.f, 100.f };
            const ECombatHitDirection Directions[] = { ECombatHitDirection::Front, ECombatHitDirection::Back,
                ECombatHitDirection::Left, ECombatHitDirection::Right };
            ExpectedFacing = FRotator(0.f, 37.f + Offsets[DirectionCase], 0.f).Vector();
            ReactionStart = Enemy->GetActorLocation();
            Attacker->SetActorLocation(ReactionStart + ExpectedFacing * 150.f);
            FCombatHit Hit; Hit.Attacker = Attacker.Get(); Hit.AttackId = 200 + DirectionCase;
            Check(ICombatHitReceiver::Execute_ReceiveCombatHit(Enemy->HitReaction, Hit), TEXT("Directional hit accepted"));
            Check(Enemy->HitReaction->GetHitDirection() == Directions[DirectionCase], TEXT("Direction uses pre-hit local axes"));
            Check(FVector::DotProduct(Enemy->GetActorForwardVector(), ExpectedFacing) > 0.999f, TEXT("Victim faces exact attack source before root motion"));
            Next(91);
        }
        else if (Stage == 91 && Elapsed() > 1.2)
        {
            Check(!Enemy->HitReaction->IsReacting(), TEXT("Directional montage finishes"));
            const FVector Displacement = Enemy->GetActorLocation() - ReactionStart;
            Check(FVector::DotProduct(Displacement, ExpectedFacing) < -20.f, TEXT("Root motion retreats away from source"));
            UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] DirectionCase=%d RetreatCm=%.2f"), DirectionCase, -FVector::DotProduct(Displacement, ExpectedFacing));
            if (++DirectionCase < 4) Next(90);
            else { Manager->ResetAll(); Manager->StartAll(); Next(10); }
        }
        else if (Stage == 10 && Elapsed() > 8.f)
        {
            Check(Manager->Summary.Completed == 3 && Manager->Summary.Failed == 0, TEXT("Existing three-Enemy Spline/Impulse trial completes"));
            if (!bFailed)
            {
                UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] PASS Server Attacks=%d AcceptedHits=%d Generation=%d Segment=%d"),
                    Player->MeleeAttack->GetPlayedAttackId(), Player->MeleeAttack->GetAcceptedHitCount(), Enemy->Experiment->Status.Generation, Enemy->Experiment->Status.Segment);
                FPlatformMisc::RequestExitWithStatus(false, 0); return false;
            }
        }
        return true;
    }
    bool TickClient()
    {
        for (TActorIterator<ASharpEnemyCharacter> It(World.Get()); It; ++It)
        {
            if (It->HitReaction->IsReacting())
            {
                bSawClientReaction = true;
                Check(!It->VisualPredictor->OwnsPresentation(), TEXT("Client reaction uses native CMC presentation"));
                const int32 Id = It->HitReaction->GetPlayedReactionId();
                int32& LastId = ObservedReactions.FindOrAdd(TWeakObjectPtr<ASharpEnemyCharacter>(*It));
                if (Id > LastId)
                {
                    LastId = Id;
                    ClientReactionStarts.Add(TWeakObjectPtr<ASharpEnemyCharacter>(*It), It->GetActorLocation());
                    ObservedDirections |= uint8(1 << uint8(It->HitReaction->GetHitDirection()));
                    Check(FMath::IsFinite(It->HitReaction->GetReactionFacingYaw()), TEXT("Replicated reaction yaw is finite"));
                    UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] ClientDirection=%d Id=%d FacingYaw=%.2f"),
                        int32(It->HitReaction->GetHitDirection()), Id, It->HitReaction->GetReactionFacingYaw());
                    if (ObservedDirections == 15) UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] PASS FourDirections"));
                }
                const FVector Facing = FRotator(0.f, It->HitReaction->GetReactionFacingYaw(), 0.f).Vector();
                const FVector* Start = ClientReactionStarts.Find(TWeakObjectPtr<ASharpEnemyCharacter>(*It));
                const uint8 Bit = uint8(1 << uint8(It->HitReaction->GetHitDirection()));
                if (Start && !(PresentedDirections & Bit) && FVector::DotProduct(It->GetActorLocation() - *Start, Facing) < -20.f &&
                    FVector::DotProduct(It->GetActorForwardVector(), Facing) > 0.99f)
                {
                    PresentedDirections |= Bit;
                    UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] ClientRetreatDirection=%d"), int32(It->HitReaction->GetHitDirection()));
                    if (PresentedDirections == 15) UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] PASS DirectionalPresentation"));
                }
            }
        }
        if (bObserver) return true;
        if (auto* PC = World->GetFirstPlayerController()) Player = Cast<AGE_AIReplicationCharacter>(PC->GetPawn());
        if (!Player.IsValid()) return true;
        if (Stage == 0 && Player->GetActorLocation().X > 4000)
        { AttackPlayerStart = Player->GetActorLocation(); Next(1); ClientNextRequest = World->GetTimeSeconds() + 0.6; }
        if (Stage == 1)
        {
            DriveNotifyInputs();
            AttackPlayerMaxDistance = FMath::Max(AttackPlayerMaxDistance, FVector::Dist2D(AttackPlayerStart, Player->GetActorLocation()));
        }
        if (Stage == 1 && Elapsed() > 5)
        {
            Check(AttackPlayerMaxDistance > 20.f, TEXT("Owning client attack moves the player capsule"));
            UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] ClientPlayerRootMotionDistanceCm=%.3f"), AttackPlayerMaxDistance);
            Check(Player->MeleeAttack->GetPlayedAttackId() >= 3 && bSawClientReaction && bEarlyInputSent && bLateInputSent && RequestedComboIds.Num() == 2,
                TEXT("Client RPC uses Notify windows, rejects early/late input and replicates reactions"));
            if (!bFailed) { UE_LOG(LogTemp, Display, TEXT("[JangCombatSmoke] PASS Client")); Stage = 2; }
        }
        // Keep the owned pawn connected throughout the server's cancellation/regression checks.
        return true;
    }
};

static FAutoConsoleCommandWithWorldAndArgs GJangCombatSmoke(
    TEXT("Combat.Smoke"), TEXT("Development integration validation on Lv_Jang: Server | Client | Observer"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() != 1 || (Args[0] != TEXT("Server") && Args[0] != TEXT("Client") && Args[0] != TEXT("Observer"))) return;
        auto Test = MakeShared<FJangCombatSmoke>(); Test->World = World; Test->GameInstance = World->GetGameInstance(); Test->bClient = Args[0] != TEXT("Server");
        Test->bObserver = Args[0] == TEXT("Observer");
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Delta) { return Test->Tick(Delta); }));
    }));
}
#endif
