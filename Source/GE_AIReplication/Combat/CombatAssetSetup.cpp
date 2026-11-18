#include "CombatAssetSetup.h"
#if WITH_EDITOR
#include "AnimNotifyState_AttackHitWindow.h"
#include "AnimNotifyState_ComboInputWindow.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool SaveCombatAsset(UObject* Asset)
{
    if (!Asset) return false;
    Asset->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.IsReadOnly(*Filename) && !Files.SetReadOnly(*Filename, false)) return false;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
}
UObject* CopyCombatAsset(UObject* Source, const FString& Path)
{
    if (!Source) return nullptr;
    auto* Result = StaticDuplicateObject(Source, CreatePackage(*Path), *FPackageName::GetLongPackageAssetName(Path));
    Result->SetFlags(RF_Public | RF_Standalone); FAssetRegistryModule::AssetCreated(Result);
    return Result;
}
bool PrepareAnimBP(const TCHAR* SourcePath, const FString& Path)
{
    if (LoadObject<UAnimBlueprint>(nullptr, *Path)) return true;
    auto* BP = Cast<UAnimBlueprint>(CopyCombatAsset(LoadObject<UAnimBlueprint>(nullptr, SourcePath), Path));
    if (!BP) return false;
    BP->TargetSkeleton->RegisterSlotNode(TEXT("CombatFullBody"));
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    bool bInserted = false;
    for (auto* Graph : Graphs)
        for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
        {
            auto* Output = Cast<UAnimGraphNode_Root>(Node);
            if (!Output) continue;
            auto* Input = Output->FindPin(TEXT("Result"));
            if (!Input || Input->LinkedTo.Num() != 1) continue;
            auto* Previous = Input->LinkedTo[0];
            auto* Slot = NewObject<UAnimGraphNode_Slot>(Graph);
            Graph->AddNode(Slot, false, false); Slot->CreateNewGuid(); Slot->PostPlacedNewNode(); Slot->AllocateDefaultPins();
            Slot->Node.SlotName = TEXT("CombatFullBody");
            Input->BreakAllPinLinks(); Previous->MakeLinkTo(Slot->FindPinChecked(TEXT("Source")));
            Slot->FindPinChecked(TEXT("Pose"))->MakeLinkTo(Input); bInserted = true;
        }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    return bInserted && BP->Status != BS_Error && SaveCombatAsset(BP->TargetSkeleton) && SaveCombatAsset(BP);
}
}
#endif

bool UCombatAssetSetup::PrepareAnimationAssets()
{
#if WITH_EDITOR
    const FString Root = TEXT("/Game/Combat/Animations/");
    if (!PrepareAnimBP(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"), Root + TEXT("ABP_PlayerCombat")) ||
        !PrepareAnimBP(TEXT("/Game/Variant_Combat/Anims/ABP_Manny_Combat"), Root + TEXT("ABP_SharpCombat"))) return false;
    auto* Skeleton = LoadObject<USkeleton>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin"));
    if (!Skeleton) return false;
    for (const auto& Pair : TArray<TPair<FName, FName>>{{TEXT("AttackSocket_R"), TEXT("hand_r")}, {TEXT("AttackSocket_L"), TEXT("hand_l")}, {TEXT("AttackSocket_FootR"), TEXT("foot_r")}})
    {
        if (Skeleton->FindSocket(Pair.Key)) continue;
        auto* Socket = NewObject<USkeletalMeshSocket>(Skeleton); Socket->SocketName = Pair.Key; Socket->BoneName = Pair.Value;
        Skeleton->Sockets.Add(Socket);
    }
    if (!SaveCombatAsset(Skeleton)) return false;
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        const FString Name = FString::Printf(TEXT("AM_PlayerAttack_%02d"), Index), Path = Root + Name;
        if (LoadObject<UAnimMontage>(nullptr, *Path)) continue;
        auto* Source = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_%02d"), Index));
        if (!Source) return false;
        auto* Montage = NewObject<UAnimMontage>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone);
        Montage->SetSkeleton(Source->GetSkeleton());
        Montage->SlotAnimTracks.Reset(); // UAnimMontage starts with an empty DefaultSlot; root extraction uses track zero.
        FSlotAnimationTrack Track; Track.SlotName = TEXT("CombatFullBody");
        FAnimSegment Segment; Segment.SetAnimReference(Source, true); Segment.AnimEndTime = Source->GetPlayLength();
        Track.AnimTrack.AnimSegments.Add(Segment); Montage->SlotAnimTracks.Add(Track);
        Montage->SetCompositeLength(Source->GetPlayLength()); Montage->AddAnimCompositeSection(TEXT("Default"), 0.f);
        Montage->BlendIn.SetBlendTime(0.1f); Montage->BlendOut.SetBlendTime(0.1f);
        FAssetRegistryModule::AssetCreated(Montage);
        FAnimNotifyEvent Event; Event.NotifyName = TEXT("AttackHitWindow");
        auto* Window = NewObject<UAnimNotifyState_AttackHitWindow>(Montage);
        Window->SocketName = Index == 1 ? TEXT("AttackSocket_R") : Index == 2 ? TEXT("AttackSocket_L") : TEXT("AttackSocket_FootR");
        Event.NotifyStateClass = Window; Event.MontageTickType = EMontageNotifyTickType::BranchingPoint; Event.bTriggerOnDedicatedServer = true;
        const float Start = (Index == 1 ? 9.f : Index == 2 ? 11.f : 12.f) / 30.f;
        Event.Link(Montage, Start); Event.SetDuration(6.f / 30.f); Event.EndLink.Link(Montage, Start + Event.GetDuration());
        Montage->Notifies.Add(Event); Montage->RefreshCacheData();
        if (!SaveCombatAsset(Montage)) return false;
    }
    const FString HitPath = Root + TEXT("AM_HitReaction_Front");
    if (!LoadObject<UAnimMontage>(nullptr, *HitPath))
    {
        auto* Source = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Hit/AM_HitReaction_Front"));
        auto* Hit = Cast<UAnimMontage>(CopyCombatAsset(Source, HitPath));
        if (!Hit) return false;
        for (auto& Track : Hit->SlotAnimTracks)
        {
            Track.SlotName = TEXT("CombatFullBody");
            for (auto& Segment : Track.AnimTrack.AnimSegments)
                if (auto* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference()))
                {
                    auto* Copy = LoadObject<UAnimSequence>(nullptr, *(Root + TEXT("AS_HitReaction_Front")));
                    if (!Copy)
                    {
                        Copy = Cast<UAnimSequence>(CopyCombatAsset(Sequence, Root + TEXT("AS_HitReaction_Front")));
                        Copy->bEnableRootMotion = true; Copy->PostEditChange();
                        if (!SaveCombatAsset(Copy)) return false;
                    }
                    Segment.SetAnimReference(Copy);
                }
        }
        if (!SaveCombatAsset(Hit)) return false;
    }
    return EnsureAttackNotifyWindows();
#else
    return false;
#endif
}

bool UCombatAssetSetup::EnsureAttackNotifyWindows()
{
#if WITH_EDITOR
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        const FString Path = FString::Printf(TEXT("/Game/Combat/Animations/AM_PlayerAttack_%02d"), Index);
        auto* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
        if (!Montage) return false;
        bool bChanged = false, bHasHitWindow = false, bHasComboWindow = false;
        while (Montage->SlotAnimTracks.Num() > 1 && Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty() &&
            Montage->SlotAnimTracks[0].SlotName == TEXT("DefaultSlot"))
        {
            Montage->SlotAnimTracks.RemoveAt(0);
            bChanged = true;
        }
        float LastHitEnd = 0.f;
        for (const auto& Event : Montage->Notifies)
        {
            if (Cast<UAnimNotifyState_AttackHitWindow>(Event.NotifyStateClass))
            { bHasHitWindow = true; LastHitEnd = FMath::Max(LastHitEnd, Event.GetTime() + Event.GetDuration()); }
            if (Cast<UAnimNotifyState_ComboInputWindow>(Event.NotifyStateClass)) bHasComboWindow = true;
        }
        if (!bHasHitWindow)
        {
            FAnimNotifyEvent Event; Event.NotifyName = TEXT("AttackHitWindow");
            auto* Window = NewObject<UAnimNotifyState_AttackHitWindow>(Montage);
            Window->SocketName = Index == 1 ? TEXT("AttackSocket_R") : Index == 2 ? TEXT("AttackSocket_L") : TEXT("AttackSocket_FootR");
            Event.NotifyStateClass = Window; Event.MontageTickType = EMontageNotifyTickType::BranchingPoint; Event.bTriggerOnDedicatedServer = true;
            const float Start = (Index == 1 ? 9.f : Index == 2 ? 11.f : 12.f) / 30.f;
            Event.Link(Montage, Start); Event.SetDuration(6.f / 30.f); Event.EndLink.Link(Montage, Start + Event.GetDuration());
            LastHitEnd = Start + Event.GetDuration(); Montage->Notifies.Add(Event); bChanged = true;
        }
        if (!bHasComboWindow)
        {
            // Creation defaults only. Runtime timing and next-attack selection come exclusively from the asset's Notify.
            const float End = Montage->GetPlayLength() - FMath::Max(0.12f, Montage->BlendOut.GetBlendTime() + 1.f / 30.f);
            const float Start = FMath::Min(FMath::Max(LastHitEnd + 1.f / 30.f, Montage->GetPlayLength() * 0.55f), End - 0.15f);
            if (Start < 0.f || End <= Start) return false;
            FAnimNotifyEvent Event; Event.NotifyName = TEXT("ComboInputWindow");
            auto* Window = NewObject<UAnimNotifyState_ComboInputWindow>(Montage); Window->NextAttackIndex = Index % 3;
            Event.NotifyStateClass = Window; Event.MontageTickType = EMontageNotifyTickType::BranchingPoint; Event.bTriggerOnDedicatedServer = true;
            Event.Link(Montage, Start); Event.SetDuration(End - Start); Event.EndLink.Link(Montage, End);
            Montage->Notifies.Add(Event); bChanged = true;
        }
        if (bChanged) { Montage->RefreshCacheData(); if (!SaveCombatAsset(Montage)) return false; }
        for (const auto& Event : Montage->Notifies)
        {
            const auto* Combo = Cast<UAnimNotifyState_ComboInputWindow>(Event.NotifyStateClass);
            UE_LOG(LogTemp, Display, TEXT("[CombatAssets] %s Length=%.3f Notify=%s Start=%.3f End=%.3f NextIndex=%d Server=%d"),
                *Montage->GetName(), Montage->GetPlayLength(), *GetNameSafe(Event.NotifyStateClass), Event.GetTime(), Event.GetTime() + Event.GetDuration(),
                Combo ? Combo->NextAttackIndex : INDEX_NONE, Event.bTriggerOnDedicatedServer);
        }
    }
    return true;
#else
    return false;
#endif
}

FString UCombatAssetSetup::DescribeAttackRootMotion()
{
#if WITH_EDITOR
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        auto* Montage = LoadObject<UAnimMontage>(nullptr, *FString::Printf(TEXT("/Game/Combat/Animations/AM_PlayerAttack_%02d"), Index));
        if (!Montage) continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("Montage"), Montage->GetPathName());
        Row->SetBoolField(TEXT("HasRootMotion"), Montage->HasRootMotion());
        Row->SetNumberField(TEXT("TrackCount"), Montage->SlotAnimTracks.Num());
        Row->SetNumberField(TEXT("FirstTrackSegments"), Montage->SlotAnimTracks.IsEmpty() ? 0 : Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num());
        Row->SetStringField(TEXT("ExtractedTranslation"), Montage->ExtractRootMotionFromTrackRange(0.f, Montage->GetPlayLength(), FAnimExtractContext()).GetTranslation().ToString());
        TArray<TSharedPtr<FJsonValue>> Segments;
        for (auto& Track : Montage->SlotAnimTracks)
            for (auto& Segment : Track.AnimTrack.AnimSegments)
            {
                auto Item = MakeShared<FJsonObject>();
                UAnimSequenceBase* Anim = Segment.GetAnimReference();
                Item->SetStringField(TEXT("Slot"), Track.SlotName.ToString());
                Item->SetStringField(TEXT("Animation"), GetPathNameSafe(Anim));
                Item->SetNumberField(TEXT("Start"), Segment.AnimStartTime);
                Item->SetNumberField(TEXT("End"), Segment.AnimEndTime);
                Item->SetNumberField(TEXT("Rate"), Segment.AnimPlayRate);
                Item->SetBoolField(TEXT("HasRootMotion"), Anim && Anim->HasRootMotion());
                if (auto* Sequence = Cast<UAnimSequence>(Anim))
                {
                    Item->SetStringField(TEXT("SequenceExtractedTranslation"), Sequence->ExtractRootMotionFromRange(0.f, Sequence->GetPlayLength(), FAnimExtractContext()).GetTranslation().ToString());
                    Item->SetStringField(TEXT("SkeletonBoneZero"), Sequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(0).ToString());
                    FAnimExtractContext Context; Context.CurrentTime = Sequence->GetPlayLength();
                    FTransform Bone;
                    Sequence->GetBoneTransform(Bone, FSkeletonPoseBoneIndex(0), Context, true);
                    Item->SetStringField(TEXT("RawBoneZeroEnd"), Bone.GetTranslation().ToString());
                    Sequence->GetBoneTransform(Bone, FSkeletonPoseBoneIndex(0), Context, false);
                    Item->SetStringField(TEXT("CompressedBoneZeroEnd"), Bone.GetTranslation().ToString());
                }
                Segments.Add(MakeShared<FJsonValueObject>(Item));
            }
        Row->SetArrayField(TEXT("Segments"), Segments); Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    FString Output; const auto Writer = TJsonWriterFactory<>::Create(&Output); FJsonSerializer::Serialize(Rows, Writer); return Output;
#else
    return TEXT("[]");
#endif
}
