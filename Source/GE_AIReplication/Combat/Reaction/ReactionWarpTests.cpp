#include "ReactionWarpPlan.h"
#include "Combat/ReactionMultiPhaseWarpModifier.h"
#include "Misc/AutomationTest.h"

bool ReactionWarp::SelfTest(FString& Report)
{
    int32 Failures=0, Checks=0;
    const auto Check=[&](bool Pass,const FString& Name)
    { ++Checks; if (!Pass) { ++Failures; Report+=TEXT("FAIL ")+Name+TEXT("\n"); } };
    auto* P=NewObject<UReactionWarpPolicy>();
    FHitWallReactionCandidate C;
    const auto Fixture=[&](int32 Count)
    {
        C={}; P->Phases.Reset(); P->ApprovedPolicyHash.Reset();
        C.CandidateId=TEXT("Numeric"); C.SourceHash=TEXT("test-source"); C.AnalysisHash=TEXT("test-analysis");
        C.DurationSeconds=1.f;
        for (int32 I=0; I<=60; ++I)
        {
            FHitWallTrajectorySample S; S.AssetTimeSeconds=S.TimeSeconds=I/60.f;
            S.RootMotion=FTransform(FVector(100.*S.AssetTimeSeconds,0.,0.)); C.Trajectory.Add(S);
        }
        for (int32 I=0; I<Count; ++I)
        {
            FHitWallMotionInterval Phase; Phase.AssetStartSeconds=I/float(Count); Phase.AssetEndSeconds=(I+1)/float(Count); C.Phases.Add(Phase);
            FReactionPhaseWarpPolicy Policy; Policy.PhaseId=I; Policy.AssetStartSeconds=Phase.AssetStartSeconds;
            Policy.AssetEndSeconds=Phase.AssetEndSeconds; Policy.PreservationWeight=I%2 ? 1.f : 3.f; P->Phases.Add(Policy);
        }
        P->CandidateId=C.CandidateId; P->SourceHash=C.SourceHash; P->AnalysisHash=C.AnalysisHash;
        P->AnalysisBinding=AnalysisBinding(C);
    };
    for (int32 Count : {1,2,3,5,16})
    {
        Fixture(Count);
        auto R=Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true);
        Check(R.Result==EReactionWarpResult::Planned,FString::Printf(TEXT("%d phases feasible"),Count));
        Check(FMath::Abs(Integrate(C.Trajectory,R.Keys,0.f,1.f).X-75.)<0.01,TEXT("Equality"));
        for (float FPS : {30.f,60.f,120.f,1.f,7.f})
        {
            FVector Total=FVector::ZeroVector;
            float T=0.f;
            while (T<1.f) { const float Next=FMath::Min(1.f,T+1.f/FPS); Total+=Integrate(C.Trajectory,R.Keys,T,Next); T=Next; }
            Check(FMath::Abs(Total.X-75.)<0.01,TEXT("Frame independent integration"));
        }
        for (const auto& K : R.Keys)
            Check(FMath::Abs(ScaleAt(R.Keys,K.Time-1.e-7f)-ScaleAt(R.Keys,K.Time+1.e-7f))<1.e-4f,TEXT("Continuous curve"));
        auto Original=Build(C,*P,100.f,EReactionWarpMode::MultiPhase,true);
        Check(Original.Result==EReactionWarpResult::Unmodified,TEXT("Unobstructed status"));
        for (const auto& K : Original.Keys) Check(K.Scale==1.f,TEXT("Exact original scale"));
        Check(Build(C,*P,1.f,EReactionWarpMode::MultiPhase,true).Reason==TEXT("InsufficientCompressionCapacity"),TEXT("Impossible budget"));
        Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true,0).Reason==TEXT("SolverIterationLimit"),TEXT("Iteration limit"));
        Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,false).Result==EReactionWarpResult::Fallback,TEXT("Review gate"));
        for (float Weight : {0.f,0.15f,0.5f,1.f})
        {
            const float A=0.13f,B=0.91f;
            FVector Input=Integrate(C.Trajectory,{},A,B)*Weight; Input.Z=7.;
            const FQuat Rotation=FRotator(0.,2.,0.).Quaternion();
            const FTransform Corrected=UReactionMultiPhaseWarpModifier::Correct(FTransform(Rotation,Input),C.Trajectory,R.Keys,A,B);
            Check(FMath::Abs(Corrected.GetTranslation().X-Integrate(C.Trajectory,R.Keys,A,B).X*Weight)<0.01,TEXT("Blended correction exactly once"));
            Check(Corrected.GetTranslation().Z==7. && Corrected.GetRotation().Equals(Rotation),TEXT("Preserve Z and rotation"));
        }
    }
    for (int32 Only=0; Only<3; ++Only)
    {
        Fixture(3);
        for (int32 I=0; I<3; ++I) P->Phases[I].bAllowWarp=I==Only;
        const auto R=Build(C,*P,95.f,EReactionWarpMode::MultiPhase,true);
        Check(R.Result==EReactionWarpResult::Planned,TEXT("Any phase can compress"));
        for (int32 I=0; I<3; ++I) if (I!=Only)
            for (int32 S=0; S<=100; ++S)
                Check(ScaleAt(R.Keys,FMath::Lerp(P->Phases[I].AssetStartSeconds,P->Phases[I].AssetEndSeconds,S/100.f))==1.f,TEXT("Locked full interval"));
    }
    Fixture(3);
    for (auto& Phase : P->Phases) Phase.bAllowWarp=false;
    Check(Build(C,*P,90.f,EReactionWarpMode::MultiPhase,true).Result==EReactionWarpResult::Fallback,TEXT("All locked"));
    Fixture(3); P->SourceHash=TEXT("wrong"); Check(!Validate(C,*P,true).IsEmpty(),TEXT("Hash mismatch"));
    Fixture(3); P->Phases[1].AssetStartSeconds=0.f; Check(!Validate(C,*P,true).IsEmpty(),TEXT("Overlap"));
    Fixture(3); P->Phases[1].MinScale=-1.f; Check(!Validate(C,*P,true).IsEmpty(),TEXT("Invalid bounds"));
    Fixture(3); P->Phases[1].PreservationWeight=std::numeric_limits<float>::quiet_NaN(); Check(!Validate(C,*P,true).IsEmpty(),TEXT("NaN"));
    Fixture(3);
    for (auto& Phase : P->Phases) { Phase.bSemanticReviewed=true; Phase.bWarpPolicyReviewed=true; }
    P->ApprovedPolicyHash=PolicyHash(*P);
    Check(Validate(C,*P,false).IsEmpty(),TEXT("Explicit approval"));
    P->Phases[0].PreservationWeight+=1.f;
    Check(Validate(C,*P,false)==TEXT("PolicyApprovalInvalidated"),TEXT("Policy edit invalidates approval"));
    Fixture(3); C.Trajectory[15].RootMotion.SetTranslation(FVector(25.,2.,0.)); P->AnalysisBinding=AnalysisBinding(C);
    auto Curved=Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true);
    Check(Curved.Result==EReactionWarpResult::Planned && Curved.MaxLateralCm==2.f,TEXT("Bounded lateral track"));
    const FVector CurvedDelta=Integrate(C.Trajectory,Curved.Keys,0.f,0.25f);
    Check(CurvedDelta.Y>0.,TEXT("Lateral delta retained"));
    P->LateralLimitCm=1.f; Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Reason==TEXT("LateralLimitExceeded"),TEXT("Lateral limit"));
    Fixture(3); C.Trajectory[15].RootMotion.SetTranslation(FVector(0.,0.,0.)); P->AnalysisBinding=AnalysisBinding(C);
    Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Reason==TEXT("ReverseRootTrack"),TEXT("Reverse rejection"));
    Fixture(3); C.Trajectory[15].RootMotion.SetTranslation(FVector(25.,0.,1.)); P->AnalysisBinding=AnalysisBinding(C);
    Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Reason==TEXT("UnsupportedVerticalOrRotation"),TEXT("Vertical rejection"));
    Fixture(3); C.Trajectory[15].RootMotion.SetRotation(FRotator(0.,5.,0.).Quaternion()); P->AnalysisBinding=AnalysisBinding(C);
    Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Reason==TEXT("UnsupportedVerticalOrRotation"),TEXT("Rotation rejection"));
    Fixture(3); P->Phases[0].TransitionSeconds=10.f; P->Phases[1].TransitionSeconds=10.f;
    Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Result==EReactionWarpResult::Planned,TEXT("Overlapping transitions clipped"));
    Fixture(2);
    C.Phases[0].AssetEndSeconds=C.Phases[1].AssetStartSeconds=0.0001f;
    P->Phases[0].AssetEndSeconds=P->Phases[1].AssetStartSeconds=0.0001f;
    P->AnalysisBinding=AnalysisBinding(C);
    Check(Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true).Result==EReactionWarpResult::Planned,TEXT("Very short phase"));
    Fixture(3);
    for (auto& S : C.Trajectory) S.RootMotion.SetTranslation(FVector(FMath::Max(0.,(double(S.TimeSeconds)-1./3.)*150.),0.,0.));
    P->AnalysisBinding=AnalysisBinding(C);
    const auto Zero=Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true);
    Check(Zero.Result==EReactionWarpResult::Planned && FMath::Abs(Zero.PhaseTravel[0].OriginalCm)<0.001,TEXT("Zero-travel phase"));
    Fixture(3);
    const auto Weighted=Build(C,*P,75.f,EReactionWarpMode::MultiPhase,true);
    Check(Weighted.PhaseTravel[0].PlannedCm>Weighted.PhaseTravel[1].PlannedCm,TEXT("Preservation cost distributes compression"));
    FVector Tail=FVector::ZeroVector;
    for (int32 I=35; I<60; ++I) Tail+=Integrate(C.Trajectory,Weighted.Keys,I/60.f,(I+1)/60.f);
    Check(Tail.Equals(Integrate(C.Trajectory,Weighted.Keys,35/60.f,1.f),0.001),TEXT("Late start excludes missing prefix"));
    const FVector FinalRaw=Integrate(C.Trajectory,{},59/60.f,1.f);
    const FTransform Final=UReactionMultiPhaseWarpModifier::Correct(FTransform(FinalRaw*.2),C.Trajectory,Weighted.Keys,59/60.f,1.f);
    Check(Final.GetTranslation().Equals(Integrate(C.Trajectory,Weighted.Keys,59/60.f,1.f)*.2,0.001),TEXT("Final blended interval"));
    const FVector ExactReference(1.7,.3,0.);
    const FTransform BlendedInput(ExactReference*.35);
    const FTransform SampleCorrected=UReactionMultiPhaseWarpModifier::Correct(BlendedInput,C.Trajectory,Weighted.Keys,.3f,.32f,&ExactReference);
    Check((SampleCorrected.GetTranslation()-BlendedInput.GetTranslation()).Equals(
        (Integrate(C.Trajectory,Weighted.Keys,.3f,.32f)-Integrate(C.Trajectory,{},.3f,.32f))*.35,0.001),TEXT("Exact engine blend reference"));
    auto Ones=Weighted.Keys; for (auto& K : Ones) K.Scale=1.f;
    Check(UReactionMultiPhaseWarpModifier::Correct(BlendedInput,C.Trajectory,Ones,.3f,.32f,&ExactReference).Equals(BlendedInput,1.e-6),TEXT("Locked interval preserves exact engine delta"));
    Fixture(3); C.Trajectory[3].AssetTimeSeconds=C.Trajectory[2].AssetTimeSeconds; P->AnalysisBinding=AnalysisBinding(C);
    Check(!Validate(C,*P,true).IsEmpty(),TEXT("Non-increasing source time"));
    Fixture(17); Check(!Validate(C,*P,true).IsEmpty(),TEXT("Phase cap rejects without truncation"));
    Report=FString::Printf(TEXT("%s checks=%d failures=%d\n"),Failures ? TEXT("FAIL") : TEXT("PASS"),Checks,Failures)+Report;
    return Failures==0;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReactionWarpNumerics,"GE.ReactionWarp.Numerics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FReactionWarpNumerics::RunTest(const FString&)
{
    FString Report; const bool Pass=ReactionWarp::SelfTest(Report); AddInfo(Report); return Pass;
}
#endif
