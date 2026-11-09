#include "ReactionWarpTools.h"
#include "Animation/AnimMontage.h"
#include "MotionWarpingComponent.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"

FString UReactionWarpTools::BindPolicy(UHitWallReactionLibrary* Library, UReactionWarpPolicy* Policy, FName CandidateId)
{
    if (!Library || !Policy) return TEXT("MissingLibraryOrPolicy");
    FHitWallReactionCandidate C;
    int32 Count=0;
    for (const auto& Entry : Library->Candidates) if (Entry.CandidateId==CandidateId) { C=Entry; ++Count; }
    if (Count!=1) return TEXT("CandidateMissingOrAmbiguous");
    Policy->CandidateId=C.CandidateId; Policy->SourceHash=C.SourceHash; Policy->AnalysisHash=C.AnalysisHash;
    Policy->AnalysisBinding=ReactionWarp::AnalysisBinding(C); Policy->ApprovedPolicyHash.Reset(); Policy->Phases.Reset();
    for (int32 I=0; I<C.Phases.Num(); ++I)
    {
        FReactionPhaseWarpPolicy Phase; Phase.PhaseId=I; Phase.AssetStartSeconds=C.Phases[I].AssetStartSeconds;
        Phase.AssetEndSeconds=C.Phases[I].AssetEndSeconds; Phase.SemanticLabel=C.Phases[I].Label;
        Phase.PreservationWeight=I==C.Phases.Num()/2 ? 1.f : 3.f;
        Policy->Phases.Add(Phase);
    }
    return ReactionWarp::Validate(C,*Policy,true);
}
FString UReactionWarpTools::PolicyDigest(UReactionWarpPolicy* Policy) { return Policy ? ReactionWarp::PolicyHash(*Policy) : FString(); }
FString UReactionWarpTools::RunSelfTest() { FString Report; ReactionWarp::SelfTest(Report); return Report; }

FString UReactionWarpTools::Inspect(UHitWallReactionLibrary* Library, UReactionWarpPolicy* Policy,
    float BudgetCm, EReactionWarpMode Mode, bool bAllowDraft)
{
    auto Root=MakeShared<FJsonObject>();
    FHitWallReactionCandidate C;
    int32 Count=0;
    if (Library && Policy) for (const auto& Entry : Library->Candidates) if (Entry.CandidateId==Policy->CandidateId) { C=Entry; ++Count; }
    if (Count!=1) Root->SetStringField(TEXT("error"),TEXT("CandidateMissingOrAmbiguous"));
    else
    {
        Root->SetObjectField(TEXT("candidate"),FJsonObjectConverter::UStructToJsonObject(C));
        auto PolicyObject=MakeShared<FJsonObject>();
        FJsonObjectConverter::UStructToJsonObject(Policy->GetClass(),Policy,PolicyObject);
        Root->SetObjectField(TEXT("policy"),PolicyObject);
        auto Plan=ReactionWarp::Build(C,*Policy,BudgetCm,Mode,bAllowDraft);
        Root->SetObjectField(TEXT("plan"),FJsonObjectConverter::UStructToJsonObject(Plan));
        auto* Montage=Cast<UAnimMontage>(C.Animation.LoadSynchronous());
        FString SourceError;
        if (!Montage) SourceError=TEXT("MontageNotLoaded");
        else for (const auto& S : C.Trajectory)
            if (!UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage,0.f,S.AssetTimeSeconds).Equals(S.RootMotion,0.05))
            { SourceError=TEXT("CookedRootTrackMismatch"); break; }
        Root->SetStringField(TEXT("cookedTrackError"),SourceError);
        Root->SetStringField(TEXT("runtimePolicyError"),ReactionWarp::Validate(C,*Policy,false));
        TArray<TSharedPtr<FJsonValue>> Samples;
        for (int32 I=0; I<=180; ++I)
        {
            const float T=C.DurationSeconds*I/180.f;
            const FVector Raw=ReactionWarp::Integrate(C.Trajectory,{},0.f,T);
            const FVector Planned=ReactionWarp::Integrate(C.Trajectory,Plan.Keys,0.f,T);
            auto Sample=MakeShared<FJsonObject>(); Sample->SetNumberField(TEXT("time"),T);
            Sample->SetNumberField(TEXT("scale"),ReactionWarp::ScaleAt(Plan.Keys,T));
            Sample->SetNumberField(TEXT("rawX"),Raw.X); Sample->SetNumberField(TEXT("rawY"),Raw.Y);
            Sample->SetNumberField(TEXT("plannedX"),Planned.X); Sample->SetNumberField(TEXT("plannedY"),Planned.Y);
            Samples.Add(MakeShared<FJsonValueObject>(Sample));
        }
        Root->SetArrayField(TEXT("samples"),Samples);
    }
    FString Text; FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)); return Text;
}
