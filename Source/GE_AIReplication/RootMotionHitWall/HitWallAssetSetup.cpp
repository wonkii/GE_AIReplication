#include "HitWallAssetSetup.h"
#if WITH_EDITOR
#include "AnimNotifyState_AttackHitWindow.h"
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

namespace
{
bool SaveAsset(UObject* Asset)
{
    Asset->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset,
        *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
}
UObject* CopyAsset(UObject* Source, const FString& Path)
{
    if (UObject* Existing = LoadObject<UObject>(nullptr, *Path)) return Existing;
    UPackage* Package = CreatePackage(*Path);
    UObject* Result = StaticDuplicateObject(Source, Package, *FPackageName::GetLongPackageAssetName(Path));
    Result->SetFlags(RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Result);
    return Result;
}
}
#endif

bool UHitWallAssetSetup::PrepareAnimationAssets()
{
#if WITH_EDITOR
    const FString Root = TEXT("/Game/RootMotionHitWall/Animations/");
    auto* SourceBP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed"));
    if (!SourceBP) return false;
    auto* BP = Cast<UAnimBlueprint>(CopyAsset(SourceBP, Root + TEXT("ABP_HitWall")));
    // Insert our full-body montage slot immediately before the final output, preserving locomotion.
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        // Keep a single final DefaultSlot; the source locomotion graph may already contain one.
        for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
        {
            auto* OldSlot = Cast<UAnimGraphNode_Slot>(Node);
            if (!OldSlot || OldSlot->Node.SlotName != TEXT("DefaultSlot")) continue;
            auto* SourcePin = OldSlot->FindPin(TEXT("Source"));
            auto* PosePin = OldSlot->FindPin(TEXT("Pose"));
            if (!SourcePin || !PosePin) continue;
            const auto Sources = SourcePin->LinkedTo;
            const auto Destinations = PosePin->LinkedTo;
            SourcePin->BreakAllPinLinks();
            PosePin->BreakAllPinLinks();
            for (auto* From : Sources) for (auto* To : Destinations) From->MakeLinkTo(To);
            FBlueprintEditorUtils::RemoveNode(BP, OldSlot, true);
        }
        for (UEdGraphNode* Node : TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
        {
            auto* Output = Cast<UAnimGraphNode_Root>(Node);
            if (!Output) continue;
            UEdGraphPin* Input = Output->FindPin(TEXT("Result"));
            if (!Input || Input->LinkedTo.Num() != 1) continue;
            if (auto* Existing = Cast<UAnimGraphNode_Slot>(Input->LinkedTo[0]->GetOwningNode()); Existing && Existing->Node.SlotName == TEXT("DefaultSlot")) continue;
            UEdGraphPin* Previous = Input->LinkedTo[0];
            auto* Slot = NewObject<UAnimGraphNode_Slot>(Graph);
            Graph->AddNode(Slot, false, false);
            Slot->CreateNewGuid();
            Slot->PostPlacedNewNode();
            Slot->AllocateDefaultPins();
            Slot->Node.SlotName = TEXT("DefaultSlot");
            Input->BreakAllPinLinks();
            Previous->MakeLinkTo(Slot->FindPinChecked(TEXT("Source")));
            Slot->FindPinChecked(TEXT("Pose"))->MakeLinkTo(Input);
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error || !SaveAsset(BP)) return false;

    // Sockets are shared Skeleton attachment points, created once without changing existing ones.
    USkeleton* Skeleton = BP->TargetSkeleton;
    for (const auto& Pair : TArray<TPair<FName, FName>>{{TEXT("AttackSocket_R"), TEXT("hand_r")}, {TEXT("AttackSocket_L"), TEXT("hand_l")}, {TEXT("AttackSocket_FootR"), TEXT("foot_r")}})
    {
        if (Skeleton->FindSocket(Pair.Key)) continue;
        auto* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
        Socket->SocketName = Pair.Key;
        Socket->BoneName = Pair.Value;
        Skeleton->Sockets.Add(Socket);
    }
    if (!SaveAsset(Skeleton)) return false;
    for (int32 Index = 1; Index <= 3; ++Index)
    {
        const FString Name = FString::Printf(TEXT("AM_PlayerAttack_%02d"), Index);
        const FString Path = Root + Name;
        auto* Existing = LoadObject<UAnimMontage>(nullptr, *Path);
        const FString SourcePath = FString::Printf(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_%02d"), Index);
        auto* Source = LoadObject<UAnimSequenceBase>(nullptr, *SourcePath);
        if (!Source) return false;
        UAnimMontage* Montage;
        if (auto* SourceMontage = Cast<UAnimMontage>(Source)) Montage = Cast<UAnimMontage>(CopyAsset(SourceMontage, Path));
        else
        {
            Montage = Existing ? Existing : NewObject<UAnimMontage>(CreatePackage(*Path), *Name, RF_Public | RF_Standalone);
            Montage->SetSkeleton(Source->GetSkeleton());
            Montage->SlotAnimTracks.Reset();
            FSlotAnimationTrack Track;
            Track.SlotName = TEXT("DefaultSlot");
            FAnimSegment Segment;
            Segment.SetAnimReference(Source, true);
            Segment.AnimEndTime = Source->GetPlayLength();
            Track.AnimTrack.AnimSegments.Add(Segment);
            Montage->SlotAnimTracks.Add(Track);
            Montage->SetCompositeLength(Source->GetPlayLength());
            Montage->AddAnimCompositeSection(TEXT("Default"), 0.f);
            Montage->BlendIn.SetBlendTime(0.1f);
            Montage->BlendOut.SetBlendTime(0.1f);
            if (!Existing) FAssetRegistryModule::AssetCreated(Montage);
        }
        Montage->Notifies.Reset();
        for (auto& Track : Montage->SlotAnimTracks) Track.SlotName = TEXT("DefaultSlot");
        // Initial windows are editable in the montage timeline; sampled-pose validation refines these.
        FAnimNotifyEvent Event;
        Event.NotifyName = TEXT("AttackHitWindow");
        auto* Window = NewObject<UAnimNotifyState_AttackHitWindow>(Montage);
        Window->SocketName = Index == 1 ? TEXT("AttackSocket_R") : Index == 2 ? TEXT("AttackSocket_L") : TEXT("AttackSocket_FootR");
        Event.NotifyStateClass = Window;
        Event.MontageTickType = EMontageNotifyTickType::BranchingPoint;
        Event.bTriggerOnDedicatedServer = true;
        const float Start = (Index == 1 ? 9.f : Index == 2 ? 11.f : 12.f) / 30.f;
        Event.Link(Montage, Start);
        Event.SetDuration(6.f / 30.f);
        Event.EndLink.Link(Montage, Start + Event.GetDuration());
        Montage->Notifies.Add(Event);
        Montage->RefreshCacheData();
        if (!SaveAsset(Montage)) return false;
        UE_LOG(LogTemp, Display, TEXT("[HitWall] Generated %s length=%.3f window=%.3f..%.3f"), *Path, Montage->GetPlayLength(), Start, Start + Event.GetDuration());
    }
    auto* HitSource = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Hit/AM_HitReaction_Front"));
    if (!HitSource) return false;
    auto* Hit = Cast<UAnimMontage>(CopyAsset(HitSource, Root + TEXT("AM_HitReaction_Front")));
    Hit->SlotAnimTracks = HitSource->SlotAnimTracks;
    for (auto& Track : Hit->SlotAnimTracks)
        for (auto& Segment : Track.AnimTrack.AnimSegments)
            if (auto* Sequence = Cast<UAnimSequence>(Segment.GetAnimReference()))
            {
                auto* Copy = Cast<UAnimSequence>(CopyAsset(Sequence, Root + TEXT("AS_HitReaction_Front")));
                Copy->bEnableRootMotion = true;
                Copy->PostEditChange();
                if (!SaveAsset(Copy)) return false;
                Segment.SetAnimReference(Copy);
            }
    for (auto& Track : Hit->SlotAnimTracks) Track.SlotName = TEXT("DefaultSlot");
    return SaveAsset(Hit);
#else
    return false;
#endif
}
