#include "HitWallPlayerCharacter.h"
#include "HitWallEnemyCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

AHitWallPlayerCharacter::AHitWallPlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    AttackBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AttackBox"));
    AttackBox->SetupAttachment(GetMesh());
    AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AttackBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    AttackBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    AttackBox->SetGenerateOverlapEvents(true);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->bEnableUpdateRateOptimizations = false;
}

void AHitWallPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    InitialTransform = GetActorTransform();
    AttackBox->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnAttackOverlap);
    if (auto* Anim = GetMesh()->GetAnimInstance()) Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    if ((HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("HitWallSmoke"))) ||
        (GetNetMode() == NM_Client && FParse::Param(FCommandLine::Get(), TEXT("HitWallNetworkSmoke"))))
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("HitWallNoWall")))
            for (TActorIterator<AActor> It(GetWorld()); It; ++It) if (It->ActorHasTag(TEXT("HitWallWall"))) It->Destroy();
        const bool bQueueTest = FParse::Param(FCommandLine::Get(), TEXT("HitWallQueue"));
        const TArray<float> Delays = bQueueTest ? TArray<float>{1.f, 1.1f, 1.2f} : TArray<float>{1.f, 3.2f, 5.4f};
        for (float Delay : Delays)
        {
            FTimerHandle Handle;
            GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
            { ServerResetTargets(); ServerRequestAttack(); }), Delay, false);
            SmokeTimers.Add(Handle);
        }
        FTimerHandle Finish;
        GetWorldTimerManager().SetTimer(Finish, FTimerDelegate::CreateWeakLambda(this, [this, bQueueTest]()
        {
            int32 Hits = 0;
            bool bMoved = false;
            for (TActorIterator<AHitWallEnemyCharacter> It(GetWorld()); It; ++It)
            {
                Hits += It->GetReactionCount();
                bMoved |= It->GetActorLocation().X > 20.f;
                UE_LOG(LogTemp, Display, TEXT("[HitWallSmoke] EnemyFinal=%s"), *It->GetActorLocation().ToString());
            }
            const bool bPassed = LastPlayedId == 3 && (bQueueTest ? Hits >= 1 : Hits == 3) && bMoved && !bAttacking && !bHitWindow;
            UE_LOG(LogTemp, Display, TEXT("[HitWallSmoke] %s Attacks=%d Hits=%d Queue=%d"), bPassed ? TEXT("PASS") : TEXT("FAIL"), LastPlayedId, Hits, bQueueTest);
            FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
        }), 8.f, false);
        SmokeTimers.Add(Finish);
    }
}

void AHitWallPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindKey(EKeys::R, IE_Pressed, this, &ThisClass::HitWallReset);
    if (auto* Enhanced = Cast<UEnhancedInputComponent>(Input); Enhanced && AttackAction)
        Enhanced->BindAction(AttackAction, ETriggerEvent::Started, this, &ThisClass::AttackPressed);
    if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->IsLocalController())
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
            if (auto* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/RootMotionHitWall/Input/IMC_HitWall.IMC_HitWall")))
                Subsystem->AddMappingContext(Context, 1);
}

void AHitWallPlayerCharacter::AttackPressed() { ServerRequestAttack(); }
void AHitWallPlayerCharacter::HitWallReset() { ServerResetTargets(); }
void AHitWallPlayerCharacter::ServerResetTargets_Implementation()
{
    if (bAttacking) return;
    for (TActorIterator<AHitWallEnemyCharacter> It(GetWorld()); It; ++It) It->ResetReaction();
    GetCharacterMovement()->StopMovementImmediately();
    SetActorTransform(InitialTransform, false, nullptr, ETeleportType::TeleportPhysics);
}
void AHitWallPlayerCharacter::ServerRequestAttack_Implementation()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastRequestTime < 0.05) return;
    LastRequestTime = Now;
    if (bAttacking) { QueuedAttacks = FMath::Min(QueuedAttacks + 1, 2); return; }
    StartNextAttack();
}

void AHitWallPlayerCharacter::StartNextAttack()
{
    if (!AttackMontages.IsValidIndex(NextAttack) || !AttackMontages[NextAttack]) return;
    AlreadyHit.Reset();
    EndHitWindow();
    GetCharacterMovement()->StopMovementImmediately();
    const int32 Index = NextAttack;
    NextAttack = (NextAttack + 1) % AttackMontages.Num();
    MulticastAttack(Index, ++AttackId);
}

void AHitWallPlayerCharacter::MulticastAttack_Implementation(int32 Index, int32 Id)
{
    if (Id <= LastPlayedId || !AttackMontages.IsValidIndex(Index)) return;
    LastPlayedId = Id;
    auto* Anim = GetMesh()->GetAnimInstance();
    if (!Anim) return;
    bAttacking = true;
    if (Anim->Montage_Play(AttackMontages[Index]) <= 0.f)
    {
        bAttacking = false;
        QueuedAttacks = 0;
        UE_LOG(LogTemp, Error, TEXT("[HitWall] Attack montage failed: %s"), *GetNameSafe(AttackMontages[Index]));
        return;
    }
    FOnMontageEnded Delegate;
    Delegate.BindUObject(this, &ThisClass::AttackEnded);
    Anim->Montage_SetEndDelegate(Delegate, AttackMontages[Index]);
    UE_LOG(LogTemp, Display, TEXT("[HitWall] Attack Id=%d Index=%d Role=%d"), Id, Index + 1, int32(GetLocalRole()));
}

void AHitWallPlayerCharacter::AttackEnded(UAnimMontage*, bool bInterrupted)
{
    EndHitWindow();
    bAttacking = false;
    if (bInterrupted) QueuedAttacks = 0;
    if (HasAuthority() && QueuedAttacks > 0) { --QueuedAttacks; StartNextAttack(); }
}

void AHitWallPlayerCharacter::BeginHitWindow(FName Socket, FVector Extent, FVector Offset)
{
    if (!HasAuthority() || !bAttacking || !GetMesh()->DoesSocketExist(Socket)) return;
    AttackBox->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
    AttackBox->SetRelativeLocation(Offset);
    AttackBox->SetBoxExtent(Extent);
    PreviousBoxLocation = AttackBox->GetComponentLocation();
    bHitWindow = true;
    AttackBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CheckHitWindow();
}

void AHitWallPlayerCharacter::EndHitWindow()
{
    bHitWindow = false;
    AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AHitWallPlayerCharacter::CheckHitWindow()
{
    if (!HasAuthority() || !bHitWindow) return;
    // Query explicitly: enabling collision while already overlapping must also count.
    TArray<FOverlapResult> Results;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HitWallBox), false, this);
    GetWorld()->OverlapMultiByObjectType(Results, AttackBox->GetComponentLocation(), AttackBox->GetComponentQuat(),
        FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(AttackBox->GetScaledBoxExtent()), Params);
    for (const auto& Result : Results) TryHit(Result.GetActor());
    // Moving bones can cross a target between server ticks. Sweep the attached box's traveled path too.
    TArray<FHitResult> SweptHits;
    GetWorld()->SweepMultiByObjectType(SweptHits, PreviousBoxLocation, AttackBox->GetComponentLocation(),
        AttackBox->GetComponentQuat(), FCollisionObjectQueryParams(ECC_Pawn),
        FCollisionShape::MakeBox(AttackBox->GetScaledBoxExtent()), Params);
    for (const auto& Hit : SweptHits) TryHit(Hit.GetActor());
    PreviousBoxLocation = AttackBox->GetComponentLocation();
    if (bDrawAttackBox) DrawDebugBox(GetWorld(), AttackBox->GetComponentLocation(), AttackBox->GetScaledBoxExtent(),
        AttackBox->GetComponentQuat(), FColor::Red, false, 0.05f);
}

void AHitWallPlayerCharacter::OnAttackOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{ TryHit(Other); }

void AHitWallPlayerCharacter::TryHit(AActor* Other)
{
    auto* Enemy = Cast<AHitWallEnemyCharacter>(Other);
    if (!HasAuthority() || !bHitWindow || !Enemy || AlreadyHit.Contains(Other)) return;
    FHitResult Wall;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HitWallOcclusion), false, this);
    Params.AddIgnoredActor(Enemy);
    if (GetWorld()->LineTraceSingleByChannel(Wall, GetActorLocation(), Enemy->GetActorLocation(), ECC_Visibility, Params)) return;
    AlreadyHit.Add(Other);
    Enemy->ReceiveAttack(this, AttackId);
}

void AHitWallPlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    CheckHitWindow();
}
void AHitWallPlayerCharacter::DoMove(float Right, float Forward) { if (!bAttacking) Super::DoMove(Right, Forward); }
void AHitWallPlayerCharacter::DoJumpStart() { if (!bAttacking) Super::DoJumpStart(); }
bool AHitWallPlayerCharacter::CanJumpInternal_Implementation() const { return !bAttacking && Super::CanJumpInternal_Implementation(); }
void AHitWallPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    EndHitWindow();
    for (auto Handle : SmokeTimers) GetWorldTimerManager().ClearTimer(Handle);
    Super::EndPlay(Reason);
}
