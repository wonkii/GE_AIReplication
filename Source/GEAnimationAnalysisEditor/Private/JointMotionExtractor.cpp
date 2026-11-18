#include "JointMotionExtractor.h"
#include "AnimPose.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using FObject = TSharedPtr<FJsonObject>;
TSharedPtr<FJsonValue> ObjectValue(const FObject& Object) { return MakeShared<FJsonValueObject>(Object); }
TArray<TSharedPtr<FJsonValue>> Numbers(std::initializer_list<double> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (double Value : Values) Result.Add(MakeShared<FJsonValueNumber>(Value));
    return Result;
}
void Position(const FObject& Object, const TCHAR* Key, const FVector& Value)
{
    Object->SetArrayField(Key, Numbers({Value.X, Value.Y, Value.Z}));
}
void Rotation(const FObject& Object, const TCHAR* Key, const FQuat& Value)
{
    const FQuat Q = Value.GetNormalized();
    Object->SetArrayField(Key, Numbers({Q.X, Q.Y, Q.Z, Q.W}));
}
FString SerializeJson(const FObject& Object)
{
    FString Result;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Result));
    return Result;
}
FString Failure(const FString& Message)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("error"), Message);
    return SerializeJson(Result);
}
}

FString UJointMotionExtractor::Extract(const FString& AssetPath, const FString& MeshPath,
    const FString& Section, double SampleRate, double PlayRate)
{
    if (!FMath::IsFinite(SampleRate) || SampleRate < 10. || SampleRate > 240. ||
        !FMath::IsFinite(PlayRate) || PlayRate < 0.05 || PlayRate > 10.)
        return Failure(TEXT("SampleRate must be 10..240 Hz; PlayRate must be 0.05..10."));
    auto* Asset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!Asset || !Mesh || !Asset->GetSkeleton() || Mesh->GetSkeleton() != Asset->GetSkeleton())
        return Failure(TEXT("Load failed or mesh and animation do not share the same Skeleton."));

    UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
    auto* Montage = Cast<UAnimMontage>(Asset);
    double AssetStart = 0., AssetEnd = Asset->GetPlayLength();
    double SequenceStart = 0., SegmentStart = 0., SegmentRate = 1.;
    if (Asset->RateScale <= 0.f) return Failure(TEXT("Non-positive asset RateScale is unsupported."));
    const double EffectiveRate = PlayRate * Asset->RateScale;
    if (Montage)
    {
        if (Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1)
            return Failure(TEXT("V1 requires one Slot and one Sequence segment; bake complex Montage playback separately."));
        if (Montage->TimeStretchCurve.IsValid()) return Failure(TEXT("Time stretching is unsupported."));
        const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
        Sequence = Cast<UAnimSequence>(Segment.GetAnimReference());
        if (!Sequence || Segment.LoopingCount != 1 || !FMath::IsFinite(Segment.GetValidPlayRate()) || Segment.GetValidPlayRate() <= 0.f)
            return Failure(TEXT("V1 requires a non-looping, positive-rate Sequence segment."));
        if (Segment.AnimStartTime < 0.f || Segment.AnimEndTime > Sequence->GetPlayLength() + UE_KINDA_SMALL_NUMBER ||
            Segment.AnimEndTime <= Segment.AnimStartTime)
            return Failure(TEXT("Invalid source segment time range."));
        SegmentRate = Segment.GetValidPlayRate();
        SegmentStart = Segment.StartPos;
        SequenceStart = Segment.AnimStartTime;
        if (Section.IsEmpty())
        {
            if (Montage->CompositeSections.Num() != 1 || !Montage->CompositeSections[0].NextSectionName.IsNone())
                return Failure(TEXT("Specify Section explicitly for multi-section or linked Montages; one Section is sampled without following links."));
        }
        else
        {
            const int32 Index = Montage->GetSectionIndex(FName(*Section));
            if (Index == INDEX_NONE) return Failure(TEXT("Unknown Section."));
            float Start, End;
            Montage->GetSectionStartAndEndTime(Index, Start, End);
            AssetStart = Start;
            AssetEnd = End;
        }
        if (AssetStart < SegmentStart - UE_KINDA_SMALL_NUMBER || AssetEnd > Segment.GetEndPos() + UE_KINDA_SMALL_NUMBER)
            return Failure(TEXT("Section includes a gap outside the Sequence segment."));
    }
    else if (!Section.IsEmpty()) return Failure(TEXT("Section is only valid for a Montage."));
    if (!Sequence || Sequence->IsValidAdditive()) return Failure(TEXT("V1 supports non-additive Animation Sequences only."));
    if (Sequence->GetSkeleton() != Mesh->GetSkeleton()) return Failure(TEXT("Segment Skeleton differs from the selected mesh."));
    const double Duration = (AssetEnd - AssetStart) / EffectiveRate;
    if (!FMath::IsFinite(Duration) || Duration <= 0. || Duration > 60.)
        return Failure(TEXT("Analysis duration must be greater than zero and at most 60 seconds."));
    const auto SourceTime = [&](double Time)
    {
        return FMath::Clamp(SequenceStart + (Time - SegmentStart) * SegmentRate, 0., double(Sequence->GetPlayLength()));
    };
    const double SourceStart = SourceTime(AssetStart);
    FAnimPoseEvaluationOptions Options;
    Options.EvaluationType = EAnimDataEvalType::Raw;
    Options.bShouldRetarget = true;
    Options.bExtractRootMotion = false;
    Options.bIncorporateRootMotionIntoPose = true;
    Options.OptionalSkeletalMesh = Mesh;
    Options.bEvaluateCurves = false;

    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("schema_version"), 1);
    Result->SetStringField(TEXT("asset"), Asset->GetPathName());
    Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
    Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    Result->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton()->GetPathName());
    Result->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Result->SetStringField(TEXT("section"), Section);
    Result->SetStringField(TEXT("evaluation"), TEXT("Raw; retarget to mesh; root lock ignored; no AnimBP/IK/blend/warp"));
    Result->SetStringField(TEXT("coordinates"), TEXT("cm, seconds, quaternion xyzw; component is asset space, NOT game world"));
    Result->SetNumberField(TEXT("sample_rate"), SampleRate);
    Result->SetNumberField(TEXT("source_fps"), Sequence->GetSamplingFrameRate().AsDecimal());
    Result->SetNumberField(TEXT("play_rate"), PlayRate);
    Result->SetNumberField(TEXT("effective_asset_rate"), EffectiveRate);
    Result->SetNumberField(TEXT("segment_rate"), SegmentRate);
    Result->SetNumberField(TEXT("asset_start"), AssetStart);
    Result->SetNumberField(TEXT("asset_end"), AssetEnd);
    Result->SetNumberField(TEXT("duration"), Duration);
    Result->SetBoolField(TEXT("root_motion_enabled"), Sequence->bEnableRootMotion);
    Result->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
    Result->SetNumberField(TEXT("root_lock_mode"), int32(Sequence->RootMotionRootLock));
    Result->SetBoolField(TEXT("normalized_root_scale"), Sequence->bUseNormalizedRootMotionScale);

    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    TArray<TSharedPtr<FJsonValue>> Bones;
    for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Ref.GetBoneName(Bone).ToString());
        Item->SetNumberField(TEXT("parent"), Ref.GetParentIndex(Bone));
        Bones.Add(ObjectValue(Item));
    }
    Result->SetArrayField(TEXT("bones"), Bones);
    TArray<TSharedPtr<FJsonValue>> Samples;
    const int32 Steps = FMath::CeilToInt(Duration * SampleRate);
    // Bound editor memory use for unusually dense skeletons/long clips.
    if (int64(Steps + 1) * Ref.GetNum() > 2000000)
        return Failure(TEXT("Too many bone samples; use a shorter Section or lower sample rate."));
    for (int32 Index = 0; Index <= Steps; ++Index)
    {
        const double Time = FMath::Min(Index / SampleRate, Duration);
        const double AssetTime = FMath::Min(AssetStart + Time * EffectiveRate, AssetEnd);
        const double SequenceTime = SourceTime(AssetTime);
        FAnimPose Pose;
        UAnimPoseExtensions::GetAnimPoseAtTime(Sequence, SequenceTime, Options, Pose);
        if (!Pose.IsValid()) return Failure(FString::Printf(TEXT("Invalid pose at %.8f"), SequenceTime));
        TArray<FName> PoseBones;
        UAnimPoseExtensions::GetBoneNames(Pose, PoseBones);
        const FTransform Root = UAnimPoseExtensions::GetBonePose(Pose, Ref.GetBoneName(0), EAnimPoseSpaces::World);
        const FTransform RootMotion = Sequence->ExtractRootMotionFromRange(SourceStart, SequenceTime, FAnimExtractContext(SequenceTime, true));
        auto Sample = MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("time"), Time);
        Sample->SetNumberField(TEXT("asset_time"), AssetTime);
        Sample->SetNumberField(TEXT("sequence_time"), SequenceTime);
        Sample->SetNumberField(TEXT("source_frame"), SequenceTime * Sequence->GetSamplingFrameRate().AsDecimal());
        Position(Sample, TEXT("root_position"), Root.GetTranslation());
        Rotation(Sample, TEXT("root_rotation"), Root.GetRotation());
        Position(Sample, TEXT("root_motion_position"), RootMotion.GetTranslation());
        Rotation(Sample, TEXT("root_motion_rotation"), RootMotion.GetRotation());
        TArray<TSharedPtr<FJsonValue>> Joints;
        for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
        {
            const FName Name = Ref.GetBoneName(Bone);
            if (!PoseBones.Contains(Name)) return Failure(TEXT("Evaluated pose is missing mesh bone: ") + Name.ToString());
            const FTransform Local = UAnimPoseExtensions::GetBonePose(Pose, Name, EAnimPoseSpaces::Local);
            const FTransform Component = UAnimPoseExtensions::GetBonePose(Pose, Name, EAnimPoseSpaces::World);
            if (Local.ContainsNaN() || Component.ContainsNaN()) return Failure(TEXT("Non-finite transform."));
            auto Joint = MakeShared<FJsonObject>();
            Position(Joint, TEXT("component"), Component.GetTranslation());
            Position(Joint, TEXT("relative"), Root.InverseTransformPosition(Component.GetTranslation()));
            Position(Joint, TEXT("local_position"), Local.GetTranslation());
            Position(Joint, TEXT("local_scale"), Local.GetScale3D());
            Rotation(Joint, TEXT("local_rotation"), Local.GetRotation());
            Joints.Add(ObjectValue(Joint));
        }
        Sample->SetArrayField(TEXT("joints"), Joints);
        Samples.Add(ObjectValue(Sample));
    }
    Result->SetArrayField(TEXT("samples"), Samples);
    return SerializeJson(Result);
}
