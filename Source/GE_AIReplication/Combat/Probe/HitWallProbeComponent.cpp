#include "HitWallProbeComponent.h"

#include "Combat/HitReactionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogHitWallProbe, Log, All);

static TAutoConsoleVariable<int32> CVarHitWallProbe(
	TEXT("hitwall.Probe"), 0,
	TEXT("1 = record every hit reaction (capsule, mesh, floor, wall contact) to Saved/HitWallProbe. Measurement only."));

static TAutoConsoleVariable<FString> CVarHitWallLabel(
	TEXT("hitwall.Label"), TEXT(""),
	TEXT("Free text written on the next recorded reactions, e.g. W2_N0 or LowLedge."));

static TAutoConsoleVariable<float> CVarHitWallSweep(
	TEXT("hitwall.SweepDistance"), 400.f,
	TEXT("How far (cm) to sweep the capsule along the retreat direction at reaction start to find the wall."));

static TAutoConsoleVariable<float> CVarHitWallTail(
	TEXT("hitwall.TailSeconds"), 0.5f,
	TEXT("Seconds to keep recording after the reaction ends."));

namespace HitWallProbe
{
	// Fixed column order; the analysis script reads columns by name.
	static const TCHAR* FrameHeader =
		TEXT("session,role,net_mode,local_role,actor,reaction_id,label,frame,utc_ms,world_t,server_t,dt,")
		TEXT("montage,montage_pos,montage_weight,net_rm,move_mode,")
		TEXT("cap_x,cap_y,cap_z,cap_bottom_z,yaw,vel_x,vel_y,vel_z,")
		TEXT("mesh_x,mesh_y,mesh_z,pelvis_x,pelvis_y,pelvis_z,")
		TEXT("floor_hit,floor_walkable,floor_dist,floor_z,")
		TEXT("wall_hits,wall_nx,wall_ny,wall_nz,wall_actor,overlap,overlap_actor,pawn_hits,pawn_actor,")
		TEXT("foot_l_x,foot_l_y,foot_l_z,foot_r_x,foot_r_y,foot_r_z,ball_l_x,ball_l_y,ball_l_z,ball_r_x,ball_r_y,ball_r_z");

	static const TCHAR* EventHeader =
		TEXT("session,role,actor,reaction_id,event,label,direction,facing_yaw,utc_ms,world_t,")
		TEXT("x,y,z,cap_radius,cap_half_height,max_step_height,walkable_floor_z,")
		TEXT("sweep_dist,wall_x,wall_y,wall_z,wall_nx,wall_ny,wall_nz,wall_actor,wall_step_up,start_penetrating,")
		TEXT("frames,wall_hit_frames,overlap_frames,max_capsule_rise,reason");

	static double UtcMs()
	{
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	}

	static FString F(double V) { return FString::Printf(TEXT("%.3f"), V); }
	static FString V3(const FVector& V) { return FString::Printf(TEXT("%.3f,%.3f,%.3f"), V.X, V.Y, V.Z); }
}

UHitWallProbeComponent::UHitWallProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// After movement and mesh smoothing, so a row is the state shown this frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);
}

bool UHitWallProbeComponent::IsProbeEnabled()
{
	static const bool bCommandLine = FParse::Param(FCommandLine::Get(), TEXT("HitWallProbe"));
	return bCommandLine || CVarHitWallProbe.GetValueOnGameThread() != 0;
}

void UHitWallProbeComponent::BeginPlay()
{
	Super::BeginPlay();
	Character = Cast<ACharacter>(GetOwner());
	HitReaction = GetOwner() ? GetOwner()->FindComponentByClass<UHitReactionComponent>() : nullptr;
	if (!Character || !HitReaction) { return; }

	HitReaction->OnReactionChanged.AddDynamic(this, &ThisClass::HandleReactionChanged);
	Character->OnActorHit.AddDynamic(this, &ThisClass::HandleOwnerHit);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		AddTickPrerequisiteComponent(Movement);
	}
	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		AddTickPrerequisiteComponent(Mesh);
	}
	if (SessionStamp.IsEmpty())
	{
		SessionStamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	}
}

void UHitWallProbeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bRecording) { EndReactionRecord(TEXT("EndPlay")); }
	if (HitReaction) { HitReaction->OnReactionChanged.RemoveDynamic(this, &ThisClass::HandleReactionChanged); }
	if (Character) { Character->OnActorHit.RemoveDynamic(this, &ThisClass::HandleOwnerHit); }
	Files.Close();
	Super::EndPlay(Reason);
}

FString UHitWallProbeComponent::RoleName() const
{
	const UWorld* World = GetWorld();
	if (!World) { return TEXT("Unknown"); }
	switch (World->GetNetMode())
	{
	case NM_Client: return TEXT("Client");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	default: return TEXT("Standalone");
	}
}

bool UHitWallProbeComponent::EnsureFiles()
{
	if (Files.IsHealthy()) { return true; }
	if (bFilesTried) { return false; }
	bFilesTried = true;
	const UWorld* World = GetWorld();
	if (!World) { return false; }
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("HitWallProbe"),
		SessionStamp + TEXT("_") + FSharpRecordingFiles::ProcessRole(*World),
		FPaths::MakeValidFileName(GetOwner()->GetName())));
	if (!Files.Open(Dir, 2048))
	{
		UE_LOG(LogHitWallProbe, Error, TEXT("[HitWallProbe] Cannot open %s"), *Dir);
		return false;
	}
	UE_LOG(LogHitWallProbe, Display, TEXT("[HitWallProbe] Recording to %s"), *Dir);
	return true;
}

void UHitWallProbeComponent::HandleReactionChanged(bool bActive, int32 ReactionId)
{
	if (bActive)
	{
		if (!IsProbeEnabled()) { return; }
		if (bRecording) { EndReactionRecord(TEXT("Superseded")); }
		BeginReactionRecord(ReactionId);
		return;
	}
	if (bRecording && !bInTail)
	{
		bInTail = true;
		TailUntil = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, CVarHitWallTail.GetValueOnGameThread());
	}
}

void UHitWallProbeComponent::HandleOwnerHit(AActor*, AActor* OtherActor, FVector, const FHitResult& Hit)
{
	if (!bRecording || !Character) { return; }
	// Ignore floor contacts; keep anything too steep to stand on (walls, ledge faces).
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const float WalkableZ = Movement ? Movement->GetWalkableFloorZ() : 0.71f;
	if (Hit.ImpactNormal.Z >= WalkableZ) { return; }
	// Another character (e.g. the attacker following its own root motion) is not a wall.
	if (Cast<APawn>(OtherActor))
	{
		++PendingPawnHits;
		PendingPawnActor = GetNameSafe(OtherActor);
		return;
	}
	++PendingWallHits;
	PendingWallNormal = Hit.ImpactNormal;
	PendingWallActor = GetNameSafe(OtherActor);
}

void UHitWallProbeComponent::BeginReactionRecord(int32 ReactionId)
{
	if (!Character || !EnsureFiles()) { return; }
	UWorld* World = GetWorld();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!World || !Capsule || !Movement) { return; }

	bRecording = true; bInTail = false; FrameCount = 0;
	CurrentReactionId = ReactionId;
	CurrentLabel = CVarHitWallLabel.GetValueOnGameThread();
	CurrentDirection = UEnum::GetValueAsString(HitReaction->GetHitDirection());
	CurrentFacingYaw = HitReaction->GetReactionFacingYaw();
	StartLocation = Character->GetActorLocation();
	StartUtcMs = HitWallProbe::UtcMs();
	StartWorldTime = World->GetTimeSeconds();
	PendingWallHits = 0; TotalWallHitFrames = 0; TotalOverlapFrames = 0; MaxCapsuleRise = 0.f;
	PendingPawnHits = 0; TotalPawnHitFrames = 0; ReactionMontage.Reset();

	// The montage retreats along local -X after the actor turns to FacingYaw
	// (HitReactionComponent contract). Sweep that way once to find the wall.
	const FVector Back = -FRotator(0.f, CurrentFacingYaw, 0.f).Vector();
	const FVector SweepStart = StartLocation + FVector(0.f, 0.f, 2.f);
	const FVector SweepEnd = SweepStart + Back * CVarHitWallSweep.GetValueOnGameThread();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HitWallProbeSweep), false, Character);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	FHitResult Wall;
	const bool bWall = World->SweepSingleByChannel(Wall, SweepStart, SweepEnd, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-1.f), Params, Response);

	FString StepUp = TEXT("");
	if (bWall && Wall.GetComponent())
	{
		StepUp = FString::FromInt(int32(Wall.GetComponent()->CanCharacterStepUpOn.GetValue()));
	}
	const float SweepDist = bWall ? float((Wall.Location - SweepStart).Size2D()) : -1.f;

	const FString Row = FString::Join(TArray<FString>{
		FSharpRecordingFiles::Csv(SessionStamp), RoleName(), FSharpRecordingFiles::Csv(Character->GetName()),
		FString::FromInt(ReactionId), TEXT("start"), FSharpRecordingFiles::Csv(CurrentLabel),
		FSharpRecordingFiles::Csv(CurrentDirection), HitWallProbe::F(CurrentFacingYaw),
		HitWallProbe::F(StartUtcMs), HitWallProbe::F(StartWorldTime), HitWallProbe::V3(StartLocation),
		HitWallProbe::F(Capsule->GetScaledCapsuleRadius()), HitWallProbe::F(Capsule->GetScaledCapsuleHalfHeight()),
		HitWallProbe::F(Movement->MaxStepHeight), HitWallProbe::F(Movement->GetWalkableFloorZ()),
		HitWallProbe::F(SweepDist),
		bWall ? HitWallProbe::V3(Wall.ImpactPoint) : FString(TEXT(",,")),
		bWall ? HitWallProbe::V3(Wall.ImpactNormal) : FString(TEXT(",,")),
		FSharpRecordingFiles::Csv(bWall ? GetNameSafe(Wall.GetActor()) : FString()), StepUp,
		(bWall && Wall.bStartPenetrating) ? TEXT("1") : TEXT("0"),
		TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("")}, TEXT(","));
	Files.Add(TEXT("hit_events.csv"), HitWallProbe::EventHeader, Row);

	SetComponentTickEnabled(true);
	WriteFrame(0.f);
}

void UHitWallProbeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bRecording) { SetComponentTickEnabled(false); return; }
	WriteFrame(DeltaTime);
	if (bInTail && GetWorld()->GetTimeSeconds() >= TailUntil) { EndReactionRecord(TEXT("Completed")); }
}

void UHitWallProbeComponent::WriteFrame(float DeltaTime)
{
	UWorld* World = GetWorld();
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!World || !Capsule || !Movement || !Mesh) { return; }

	const FVector Cap = Character->GetActorLocation();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float BottomZ = Cap.Z - HalfHeight;
	MaxCapsuleRise = FMath::Max(MaxCapsuleRise, float(Cap.Z - StartLocation.Z));

	// Remember the reaction montage once, then read it through blend-out too
	// (GetCurrentActiveMontage drops a montage as soon as it starts blending out,
	// while its root motion is still applied at reduced weight).
	FString MontageName; double MontagePos = -1.0, MontageWeight = 0.0;
	if (UAnimInstance* Anim = Mesh->GetAnimInstance())
	{
		if (!ReactionMontage.IsValid())
		{
			ReactionMontage = Anim->GetCurrentActiveMontage();
		}
		if (UAnimMontage* Montage = ReactionMontage.Get())
		{
			if (const FAnimMontageInstance* Instance = Anim->GetInstanceForMontage(Montage))
			{
				MontageName = Montage->GetName();
				MontagePos = Instance->GetPosition();
				MontageWeight = Instance->GetWeight();
			}
		}
	}

	static const FName PelvisName(TEXT("pelvis"));
	const FVector Pelvis = Mesh->GetBoneIndex(PelvisName) != INDEX_NONE
		? Mesh->GetBoneLocation(PelvisName) : Mesh->GetComponentLocation();

	// Feet in world space, after animation and mesh smoothing. Empty if the skeleton lacks the bone.
	const auto Bone = [Mesh](const TCHAR* Name) -> FString
	{
		const FName BoneName(Name);
		return Mesh->GetBoneIndex(BoneName) != INDEX_NONE ? HitWallProbe::V3(Mesh->GetBoneLocation(BoneName)) : FString(TEXT(",,"));
	};
	const FString Feet = FString::Join(TArray<FString>{
		Bone(TEXT("foot_l")), Bone(TEXT("foot_r")), Bone(TEXT("ball_l")), Bone(TEXT("ball_r"))}, TEXT(","));

	const FFindFloorResult& Floor = Movement->CurrentFloor;
	const double FloorZ = Floor.bBlockingHit ? Floor.HitResult.ImpactPoint.Z : 0.0;

	// Capsule shrunk by 2 cm: true only for real penetration, not resting contact.
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HitWallProbeOverlap), false, Character);
	FCollisionResponseParams Response;
	Capsule->InitSweepCollisionParams(Params, Response);
	World->OverlapMultiByChannel(Overlaps, Cap, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
		Capsule->GetCollisionShape(-2.f), Params, Response);
	FString OverlapActor;
	for (const FOverlapResult& Result : Overlaps)
	{
		if (Result.bBlockingHit && Result.GetActor() != Character) { OverlapActor = GetNameSafe(Result.GetActor()); break; }
	}
	const bool bOverlap = !OverlapActor.IsEmpty();
	if (bOverlap) { ++TotalOverlapFrames; }
	if (PendingWallHits > 0) { ++TotalWallHitFrames; }

	const AGameStateBase* GameState = World->GetGameState();
	const double ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	const FVector Velocity = Movement->Velocity;

	const FString Row = FString::Join(TArray<FString>{
		FSharpRecordingFiles::Csv(SessionStamp), RoleName(), FString::FromInt(int32(World->GetNetMode())),
		FString::FromInt(int32(Character->GetLocalRole())), FSharpRecordingFiles::Csv(Character->GetName()),
		FString::FromInt(CurrentReactionId), FSharpRecordingFiles::Csv(CurrentLabel),
		FString::Printf(TEXT("%llu"), uint64(GFrameCounter)), HitWallProbe::F(HitWallProbe::UtcMs()),
		HitWallProbe::F(World->GetTimeSeconds()), HitWallProbe::F(ServerTime), HitWallProbe::F(DeltaTime),
		FSharpRecordingFiles::Csv(MontageName), HitWallProbe::F(MontagePos),
		HitWallProbe::F(MontageWeight),
		Character->IsPlayingNetworkedRootMotionMontage() ? TEXT("1") : TEXT("0"),
		FSharpRecordingFiles::Csv(Movement->GetMovementName()),
		HitWallProbe::V3(Cap), HitWallProbe::F(BottomZ), HitWallProbe::F(Character->GetActorRotation().Yaw),
		HitWallProbe::V3(Velocity), HitWallProbe::V3(Mesh->GetComponentLocation()), HitWallProbe::V3(Pelvis),
		Floor.bBlockingHit ? TEXT("1") : TEXT("0"), Floor.bWalkableFloor ? TEXT("1") : TEXT("0"),
		HitWallProbe::F(Floor.FloorDist), HitWallProbe::F(FloorZ),
		FString::FromInt(PendingWallHits), HitWallProbe::V3(PendingWallNormal),
		FSharpRecordingFiles::Csv(PendingWallActor), bOverlap ? TEXT("1") : TEXT("0"),
		FSharpRecordingFiles::Csv(OverlapActor),
		FString::FromInt(PendingPawnHits), FSharpRecordingFiles::Csv(PendingPawnActor), Feet}, TEXT(","));
	Files.Add(TEXT("hit_frames.csv"), HitWallProbe::FrameHeader, Row);

	++FrameCount;
	if (PendingPawnHits > 0) { ++TotalPawnHitFrames; }
	PendingWallHits = 0; PendingWallNormal = FVector::ZeroVector; PendingWallActor.Reset();
	PendingPawnHits = 0; PendingPawnActor.Reset();
}

void UHitWallProbeComponent::EndReactionRecord(const TCHAR* Reason)
{
	if (!bRecording) { return; }
	bRecording = false; bInTail = false;
	SetComponentTickEnabled(false);
	const FVector End = Character ? Character->GetActorLocation() : FVector::ZeroVector;
	const UWorld* World = GetWorld();

	const FString Row = FString::Join(TArray<FString>{
		FSharpRecordingFiles::Csv(SessionStamp), RoleName(),
		FSharpRecordingFiles::Csv(Character ? Character->GetName() : FString()),
		FString::FromInt(CurrentReactionId), TEXT("end"), FSharpRecordingFiles::Csv(CurrentLabel),
		FSharpRecordingFiles::Csv(CurrentDirection), HitWallProbe::F(CurrentFacingYaw),
		HitWallProbe::F(HitWallProbe::UtcMs()), HitWallProbe::F(World ? World->GetTimeSeconds() : 0.0),
		HitWallProbe::V3(End), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(",,"), TEXT(",,"),
		TEXT(""), TEXT(""), TEXT(""),
		FString::FromInt(FrameCount), FString::FromInt(TotalWallHitFrames), FString::FromInt(TotalOverlapFrames),
		HitWallProbe::F(MaxCapsuleRise), Reason}, TEXT(","));
	Files.Add(TEXT("hit_events.csv"), HitWallProbe::EventHeader, Row);
	Files.Flush();

	// One line per reaction so the result is visible in the Output Log right away.
	UE_LOG(LogHitWallProbe, Display,
		TEXT("[HitWallProbe] %s %s Id=%d Label=%s Frames=%d Retreat=%.1fcm Rise=%.1fcm WallHitFrames=%d OverlapFrames=%d PawnHitFrames=%d (%s)"),
		*RoleName(), Character ? *Character->GetName() : TEXT("?"), CurrentReactionId, *CurrentLabel, FrameCount,
		float(FVector::Dist2D(End, StartLocation)), MaxCapsuleRise, TotalWallHitFrames, TotalOverlapFrames,
		TotalPawnHitFrames, Reason);
}
