#include "LocalRubberbandRecordingSubsystem.h"
#include "LocalRubberbandCharacter.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "CoreGlobals.h"
namespace LocalRubberbandRecordingDetail
{
    FString Vec(const FVector& V) { return FString::Printf(TEXT("%.6f,%.6f,%.6f"), V.X,V.Y,V.Z); }
    FString Stamp(const UWorld& W)
    {
        const AGameStateBase* GS = W.GetGameState();
        const double Server = W.GetNetMode() == NM_Client && GS ? GS->GetServerWorldTimeSeconds() : W.GetTimeSeconds();
        return FString::Printf(TEXT("%.9f,%.6f,%llu"), FPlatformTime::Seconds(), Server, static_cast<unsigned long long>(GFrameCounter));
    }
    const TCHAR* TimeHeader = TEXT("HostTime,ServerTime,Frame");
}
using namespace LocalRubberbandRecordingDetail;
bool ULocalRubberbandRecordingSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::PIE || Type == EWorldType::Game; }
TStatId ULocalRubberbandRecordingSubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(ULocalRubberbandRecordingSubsystem, STATGROUP_Tickables); }
bool ULocalRubberbandRecordingSubsystem::Matches(const APawn& Pawn) const
{
    return bRecording && Pawn.IsA<ALocalRubberbandCharacter>() && Pawn.GetPlayerState() && Pawn.GetPlayerState()->GetPlayerId() == PlayerId
        && (bClient ? Pawn.GetLocalRole() == ROLE_AutonomousProxy && Pawn.IsLocallyControlled() : Pawn.HasAuthority());
}
bool ULocalRubberbandRecordingSubsystem::Start(const FString& InDirectory, int32 InPlayerId, bool bInClient)
{
    if (bRecording || !GetWorld() || (bInClient ? GetWorld()->GetNetMode()!=NM_Client : GetWorld()->GetNetMode()!=NM_DedicatedServer)) { return false; }
    Directory = InDirectory; PlayerId = InPlayerId; bClient = bInClient; EventId = 0;
    Prefix = bClient ? TEXT("local") : TEXT("server");
    if (!Files.Open(Directory, 2048)) { return false; }
    bRecording = true; LastFlush = FPlatformTime::Seconds();
    const FString Header = FString(TimeHeader) + TEXT(",PlayerId,FrameMs,CapsuleX,CapsuleY,CapsuleZ,MeshX,MeshY,MeshZ,VisualX,VisualY,VisualZ,VelocityX,VelocityY,VelocityZ,MovementMode,CameraValid,CameraX,CameraY,CameraZ,CameraPitch,CameraYaw,CameraRoll");
    Files.Add(Prefix+TEXT("_samples.csv"),Header,FString());
    if (bClient)
    {
        Files.Add(TEXT("local_corrections.csv"), FString(TimeHeader)+TEXT(",EventId,MoveStamp,BeforeX,BeforeY,BeforeZ,TargetX,TargetY,TargetZ,TargetVelocityX,TargetVelocityY,TargetVelocityZ,BaseRelative,ServerMode,SavedMoves"),FString());
        Files.Add(TEXT("local_replays.csv"), FString(TimeHeader)+TEXT(",FirstEvent,LastEvent,BeforeFirstX,BeforeFirstY,BeforeFirstZ,BeforeReplayX,BeforeReplayY,BeforeReplayZ,AfterReplayX,AfterReplayY,AfterReplayZ,NetShiftCm,ReplayResult"),FString());
        Files.Add(TEXT("local_script.csv"), FString(TimeHeader)+TEXT(",Event,Loop,StepIndex,StepName"),FString());
    }
    return Files.IsHealthy();
}
void ULocalRubberbandRecordingSubsystem::Stop(const FString& Reason)
{
    if (!bRecording) { return; }
    const bool bOK = Files.Flush(); Files.Close(); bRecording = false;
    FFileHelper::SaveStringToFile(FString::Printf(TEXT("Reason=%s\nWriterHealthy=%d\nCorrectionCallbacks=%d\nPlayerId=%d\n"),*Reason,bOK?1:0,EventId,PlayerId),
        *FPaths::Combine(Directory,Prefix+TEXT("_status.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void ULocalRubberbandRecordingSubsystem::Deinitialize() { Stop(TEXT("WorldEnded")); Super::Deinitialize(); }
void ULocalRubberbandRecordingSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bRecording) { return; }
    Sample(DeltaTime);
    if (FPlatformTime::Seconds()-LastFlush>=1.0) { Files.Flush(); LastFlush=FPlatformTime::Seconds(); }
}
void ULocalRubberbandRecordingSubsystem::Sample(float DeltaTime)
{
    for (TActorIterator<ALocalRubberbandCharacter> It(GetWorld()); It; ++It)
    {
        const ALocalRubberbandCharacter& C = **It;
        if (!Matches(C)) { continue; }
        const FVector Capsule = C.GetCapsuleComponent()->GetComponentLocation();
        const FVector Mesh = C.GetMesh()->GetComponentLocation();
        const FVector Visual = Mesh-C.GetCapsuleComponent()->GetComponentTransform().TransformVector(C.GetBaseTranslationOffset());
        const APlayerController* PC = Cast<APlayerController>(C.GetController());
        const APlayerCameraManager* Cam = bClient && PC && PC->GetViewTarget()==&C ? PC->PlayerCameraManager : nullptr;
        const FVector Camera = Cam ? Cam->GetCameraLocation() : FVector::ZeroVector;
        const FRotator Rot = Cam ? Cam->GetCameraRotation() : FRotator::ZeroRotator;
        const FString Row = Stamp(*GetWorld()) + FString::Printf(TEXT(",%d,%.6f,"),PlayerId,DeltaTime*1000.0f)+Vec(Capsule)+TEXT(",")+Vec(Mesh)+TEXT(",")+Vec(Visual)+TEXT(",")+Vec(C.GetVelocity())
            +FString::Printf(TEXT(",%d,%d,"),static_cast<int32>(C.GetCharacterMovement()->MovementMode),Cam?1:0)+Vec(Camera)+FString::Printf(TEXT(",%.6f,%.6f,%.6f"),Rot.Pitch,Rot.Yaw,Rot.Roll);
        // Header was registered in Start; Add uses it only for the first write.
        Files.Add(Prefix+TEXT("_samples.csv"),FString(),Row);
        break;
    }
}
int32 ULocalRubberbandRecordingSubsystem::RecordCorrection(const ACharacter& C,float MoveStamp,const FVector& Target,const FVector& TargetVelocity,bool bRelative,uint8 Mode,int32 SavedMoves)
{
    if (!bClient || !Matches(C)) { return 0; }
    const int32 Id=++EventId;
    Files.Add(TEXT("local_corrections.csv"),FString(),Stamp(*GetWorld())+FString::Printf(TEXT(",%d,%.6f,"),Id,MoveStamp)+Vec(C.GetActorLocation())+TEXT(",")+Vec(Target)+TEXT(",")+Vec(TargetVelocity)+FString::Printf(TEXT(",%d,%d,%d"),bRelative?1:0,static_cast<int32>(Mode),SavedMoves));
    return Id;
}
void ULocalRubberbandRecordingSubsystem::RecordReplay(const ACharacter& C,int32 First,int32 Last,const FVector& BeforeFirst,const FVector& BeforeReplay,bool bReplayResult)
{
    if (!bClient || !Matches(C)) { return; }
    const FVector After=C.GetActorLocation();
    Files.Add(TEXT("local_replays.csv"),FString(),Stamp(*GetWorld())+FString::Printf(TEXT(",%d,%d,"),First,Last)+Vec(BeforeFirst)+TEXT(",")+Vec(BeforeReplay)+TEXT(",")+Vec(After)+FString::Printf(TEXT(",%.6f,%d"),FVector::Dist(BeforeFirst,After),bReplayResult?1:0));
}
void ULocalRubberbandRecordingSubsystem::RecordScriptEvent(const APawn& Pawn,const FString& Event,int32 Loop,int32 StepIndex,FName StepName)
{
    if (!bClient || !Matches(Pawn)) { return; }
    Files.Add(TEXT("local_script.csv"),FString(),Stamp(*GetWorld())+TEXT(",")+FSharpRecordingFiles::Csv(Event)+FString::Printf(TEXT(",%d,%d,"),Loop,StepIndex)+FSharpRecordingFiles::Csv(StepName.ToString()));
}
