#include "ReactionWarpPlan.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

namespace
{
FString Digest(const FString& Text)
{
    FTCHARToUTF8 Bytes(*Text);
    FSHA1 SHA;
    SHA.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    SHA.Final();
    uint8 Hash[20]; SHA.GetHash(Hash);
    return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
}
template<typename T> FString StructJson(const T& Value)
{
    FString Text;
    FJsonObjectConverter::UStructToJsonObjectString(Value, Text, 0, 0, 0, nullptr, false);
    return Text;
}
bool Finite(float X) { return FMath::IsFinite(X); }
}

FString ReactionWarp::AnalysisBinding(const FHitWallReactionCandidate& C) { return Digest(StructJson(C)); }
FString ReactionWarp::PolicyHash(const UReactionWarpPolicy& P)
{
    FString Text = FString::Printf(TEXT("%d|%d|%s|%s|%s|%s|%.9g|%.9g"), P.SchemaVersion,
        P.PolicyVersion, *P.CandidateId.ToString(), *P.SourceHash, *P.AnalysisHash, *P.AnalysisBinding,
        P.LateralLimitCm, P.SmoothnessWeight);
    for (const auto& Phase : P.Phases) Text += TEXT("|") + StructJson(Phase);
    return Digest(Text);
}
FString ReactionWarp::PlanHash(const FReactionWarpPlan& P)
{
    FReactionWarpPlan Copy = P; Copy.PlanHash.Reset();
    return Digest(StructJson(Copy));
}

FString ReactionWarp::Validate(const FHitWallReactionCandidate& C, const UReactionWarpPolicy& P, bool bDraft)
{
    if (P.SchemaVersion != 2 || P.PolicyVersion < 1) return TEXT("UnsupportedPolicySchema");
    if (C.CandidateId.IsNone() || C.CandidateId != P.CandidateId || C.SourceHash.IsEmpty() || C.AnalysisHash.IsEmpty() ||
        C.SourceHash != P.SourceHash || C.AnalysisHash != P.AnalysisHash || AnalysisBinding(C) != P.AnalysisBinding)
        return TEXT("SourceAnalysisBindingMismatch");
    if (!Finite(C.PlayRate) || !FMath::IsNearlyEqual(C.PlayRate, 1.f) || !Finite(C.DurationSeconds) || C.DurationSeconds <= 0.f)
        return TEXT("UnsupportedPlayback");
    if (!Finite(P.LateralLimitCm) || P.LateralLimitCm < 0.f || !Finite(P.SmoothnessWeight) || P.SmoothnessWeight < 0.f)
        return TEXT("InvalidPolicySettings");
    if (P.Phases.IsEmpty() || P.Phases.Num() > MaxPhases || P.Phases.Num() != C.Phases.Num()) return TEXT("InvalidPhaseCount");
    if (C.Trajectory.Num() < 2 || C.Trajectory.Num() > 16384) return TEXT("InvalidTrajectoryCount");
    float Previous = -1.f;
    for (const auto& S : C.Trajectory)
    {
        if (!Finite(S.AssetTimeSeconds) || !Finite(S.TimeSeconds) || S.AssetTimeSeconds <= Previous ||
            !FMath::IsNearlyEqual(S.TimeSeconds, S.AssetTimeSeconds, 1.e-4f) || S.RootMotion.ContainsNaN() ||
            !S.RootMotion.GetScale3D().Equals(FVector::OneVector, 1.e-4)) return TEXT("InvalidTrajectory");
        Previous = S.AssetTimeSeconds;
    }
    if (FMath::Abs(C.Trajectory[0].AssetTimeSeconds) > 1.e-5f ||
        !C.Trajectory[0].RootMotion.GetTranslation().IsNearlyZero(1.e-4) ||
        !FMath::IsNearlyEqual(Previous, C.DurationSeconds, 1.e-4f)) return TEXT("UnsupportedTrackRange");
    Previous = 0.f;
    TSet<int32> Ids;
    for (int32 I = 0; I < P.Phases.Num(); ++I)
    {
        const auto& Phase = P.Phases[I]; const auto& Source = C.Phases[I];
        if (Ids.Contains(Phase.PhaseId) || !Finite(Phase.AssetStartSeconds) || !Finite(Phase.AssetEndSeconds) ||
            FMath::Abs(Phase.AssetStartSeconds - Previous) > 1.e-5f || Phase.AssetEndSeconds <= Phase.AssetStartSeconds ||
            !FMath::IsNearlyEqual(Phase.AssetStartSeconds, Source.AssetStartSeconds, 1.e-5f) ||
            !FMath::IsNearlyEqual(Phase.AssetEndSeconds, Source.AssetEndSeconds, 1.e-5f)) return TEXT("InvalidPhaseBoundaries");
        Ids.Add(Phase.PhaseId); Previous = Phase.AssetEndSeconds;
        if (!Finite(Phase.MinScale) || !Finite(Phase.MaxScale) || Phase.MinScale < 0.f || Phase.MaxScale > 1.f ||
            Phase.MinScale > Phase.MaxScale || !Finite(Phase.PreservationWeight) || Phase.PreservationWeight <= 0.f ||
            !Finite(Phase.TransitionSeconds) || Phase.TransitionSeconds < 0.f) return TEXT("InvalidPhasePolicy");
        if (!bDraft && (!Phase.bWarpPolicyReviewed || !Phase.bSemanticReviewed)) return TEXT("UnreviewedPhasePolicy");
    }
    if (!FMath::IsNearlyEqual(Previous, C.DurationSeconds, 1.e-4f)) return TEXT("IncompletePhaseCoverage");
    if (!bDraft && P.ApprovedPolicyHash != PolicyHash(P)) return TEXT("PolicyApprovalInvalidated");
    return {};
}

float ReactionWarp::ScaleAt(const TArray<FReactionWarpScaleKey>& Keys, float Time)
{
    if (Keys.IsEmpty()) return 1.f;
    for (int32 I = 1; I < Keys.Num(); ++I)
        if (Time <= Keys[I].Time)
            return FMath::Lerp(Keys[I-1].Scale, Keys[I].Scale,
                FMath::Clamp((Time - Keys[I-1].Time) / (Keys[I].Time - Keys[I-1].Time), 0.f, 1.f));
    return Keys.Last().Scale;
}

FVector ReactionWarp::Integrate(const TArray<FHitWallTrajectorySample>& Track,
    const TArray<FReactionWarpScaleKey>& Keys, float Start, float End)
{
    FVector Result = FVector::ZeroVector;
    for (int32 I = 1; I < Track.Num(); ++I)
    {
        const float A = FMath::Max(Start, Track[I-1].AssetTimeSeconds);
        const float B = FMath::Min(End, Track[I].AssetTimeSeconds);
        if (B <= A) continue;
        const FVector Velocity = (Track[I].RootMotion.GetTranslation() - Track[I-1].RootMotion.GetTranslation()) /
            (Track[I].AssetTimeSeconds - Track[I-1].AssetTimeSeconds);
        float Cursor = A;
        while (Cursor < B)
        {
            float Next = B;
            for (const auto& K : Keys) if (K.Time > Cursor && K.Time < Next) Next = K.Time;
            const double Area = (Next - Cursor) * (ScaleAt(Keys, Cursor) + ScaleAt(Keys, Next)) * 0.5;
            Result += FVector(Velocity.X * Area, Velocity.Y * Area, Velocity.Z * (Next - Cursor));
            Cursor = Next;
        }
    }
    return Result;
}

FReactionWarpPlan ReactionWarp::Build(const FHitWallReactionCandidate& C, const UReactionWarpPolicy& P,
    float Budget, EReactionWarpMode Mode, bool bDraft, int32 MaxIterations)
{
    FReactionWarpPlan R;
    R.CandidateId = C.CandidateId; R.SourceHash = C.SourceHash; R.AnalysisHash = C.AnalysisHash;
    R.PolicyHash = PolicyHash(P); R.Mode = Mode; R.BudgetCm = Budget; R.bDraft = bDraft;
    const auto Fail = [&](const TCHAR* Reason)
    { R.Result = EReactionWarpResult::Fallback; R.Reason = Reason; R.Keys.Reset(); R.PlanHash = PlanHash(R); return R; };
    const FString Error = Validate(C, P, bDraft);
    if (!Error.IsEmpty()) return Fail(*Error);
    if (!Finite(Budget) || Budget < 0.f) return Fail(TEXT("InvalidBudget"));
    const FVector Direction = C.Trajectory.Last().RootMotion.GetTranslation().GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return Fail(TEXT("NoPlanarTravel"));
    double PreviousTravel = 0.;
    for (const auto& S : C.Trajectory)
    {
        const FVector V = S.RootMotion.GetTranslation();
        const double Travel = FVector::DotProduct(V, Direction);
        const float Lateral = (V - Direction * Travel).Size2D();
        if (Lateral > R.MaxLateralCm) { R.MaxLateralCm = Lateral; R.MaxLateralTime = S.AssetTimeSeconds; }
        if (FMath::Abs(V.Z) > 0.1 || !S.RootMotion.GetRotation().Equals(FQuat::Identity, 1.e-4)) return Fail(TEXT("UnsupportedVerticalOrRotation"));
        if (Travel < PreviousTravel - 1.e-4) return Fail(TEXT("ReverseRootTrack"));
        PreviousTravel = Travel;
    }
    if (R.MaxLateralCm > P.LateralLimitCm) return Fail(TEXT("LateralLimitExceeded"));
    R.OriginalCm = PreviousTravel;
    TArray<double> Lower, Upper, Weights, Coeff;
    for (const auto& Phase : P.Phases)
    {
        const float Width = Phase.AssetEndSeconds - Phase.AssetStartSeconds;
        // Shared boundary keys enforce continuity. Locked phases constrain both boundary keys to one.
        const float Pad = FMath::Min(Width * 0.25f, FMath::Max(Phase.TransitionSeconds * 0.5f, Width * 1.e-3f));
        for (float T : {Phase.AssetStartSeconds, Phase.AssetStartSeconds + Pad, Phase.AssetEndSeconds - Pad, Phase.AssetEndSeconds})
            if (R.Keys.IsEmpty() || T > R.Keys.Last().Time) { FReactionWarpScaleKey K; K.Time = T; R.Keys.Add(K); }
    }
    if (R.Keys.Num() > MaxKeys) return Fail(TEXT("KeyLimitExceeded"));
    const int32 N = R.Keys.Num();
    for (int32 K = 0; K < N; ++K)
    {
        double L = 0., U = 1., W = 0.;
        for (int32 J = 0; J < P.Phases.Num(); ++J)
        {
            const auto& Phase = P.Phases[J];
            if (R.Keys[K].Time < Phase.AssetStartSeconds || R.Keys[K].Time > Phase.AssetEndSeconds) continue;
            const bool Locked = !Phase.bAllowWarp || (Mode == EReactionWarpMode::MiddleOnly && J != P.Phases.Num()/2);
            L = FMath::Max(L, Locked ? 1. : double(Phase.MinScale));
            U = FMath::Min(U, Locked ? 1. : double(Phase.MaxScale));
            W += Phase.PreservationWeight;
        }
        if (L > U) return Fail(TEXT("IncompatibleBoundaryConstraints"));
        Lower.Add(L); Upper.Add(U); Weights.Add(W);
        auto Basis = R.Keys;
        for (int32 J = 0; J < N; ++J) Basis[J].Scale = J == K ? 1.f : 0.f;
        Coeff.Add(FVector::DotProduct(Integrate(C.Trajectory, Basis, 0.f, C.DurationSeconds), Direction));
        R.MinimumCm += Coeff[K] * L; R.MaximumCm += Coeff[K] * U;
    }
    const double Target = FMath::Min(double(Budget), double(R.OriginalCm));
    if (Mode == EReactionWarpMode::Off) { R.Reason = TEXT("Disabled"); R.PlanHash = PlanHash(R); return R; }
    if (Mode == EReactionWarpMode::Legacy) return Fail(TEXT("LegacyUsesNotifyPlanner"));
    if (Target < R.MinimumCm - 1.e-4 || Target > R.MaximumCm + 1.e-4) return Fail(TEXT("InsufficientCompressionCapacity"));
    TArray<double> X; X.Init(1., N);
    if (Mode == EReactionWarpMode::Uniform)
    {
        const double Scale = Target / R.OriginalCm;
        for (int32 I = 0; I < N; ++I)
        {
            if (Scale < Lower[I] - 1.e-6 || Scale > Upper[I] + 1.e-6) return Fail(TEXT("InsufficientCompressionCapacity"));
            X[I] = Scale;
        }
    }
    else if (Target < R.OriginalCm - 1.e-6)
    {
        // Projected gradient on a strictly convex QP. Projection onto the box/equality
        // intersection uses deterministic dual bisection (all travel coefficients >= 0).
        const auto Project = [&](const TArray<double>& Y, TArray<double>& Out)
        {
            double Low = -1., High = 1.;
            const auto Sum = [&](double Mu)
            { double S = 0.; for (int32 I = 0; I < N; ++I) S += Coeff[I] * FMath::Clamp(Y[I] - Mu*Coeff[I], Lower[I], Upper[I]); return S; };
            for (int32 I=0; I<80 && Sum(Low)<Target; ++I) Low *= 2.;
            for (int32 I=0; I<80 && Sum(High)>Target; ++I) High *= 2.;
            for (int32 I=0; I<80; ++I) { const double Mid=(Low+High)*0.5; if (Sum(Mid)>Target) Low=Mid; else High=Mid; }
            Out.SetNum(N);
            for (int32 I=0; I<N; ++I) Out[I]=FMath::Clamp(Y[I]-(Low+High)*0.5*Coeff[I], Lower[I], Upper[I]);
        };
        Project(X, X);
        const double Step = 1. / (2. * (FMath::Max(Weights) + 4. * P.SmoothnessWeight));
        bool Converged = false;
        for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
        {
            auto Y = X;
            for (int32 I=0; I<N; ++I)
            {
                double G=Weights[I]*(X[I]-1.);
                if (I>0) G+=P.SmoothnessWeight*(X[I]-X[I-1]);
                if (I+1<N) G+=P.SmoothnessWeight*(X[I]-X[I+1]);
                Y[I]-=2.*Step*G;
            }
            TArray<double> Next; Project(Y, Next);
            double Delta=0.; for (int32 I=0; I<N; ++I) Delta=FMath::Max(Delta,FMath::Abs(Next[I]-X[I]));
            X=MoveTemp(Next);
            if (Delta<1.e-8) { Converged=true; break; }
        }
        if (!Converged) return Fail(TEXT("SolverIterationLimit"));
    }
    for (int32 I=0; I<N; ++I) { if (!FMath::IsFinite(X[I])) return Fail(TEXT("SolverNumericalFailure")); R.Keys[I].Scale=X[I]; }
    const double Reached = FVector::DotProduct(Integrate(C.Trajectory,R.Keys,0.f,C.DurationSeconds),Direction);
    if (FMath::Abs(Reached-Target)>0.01) return Fail(TEXT("SolverEqualityResidual"));
    for (const auto& Phase : P.Phases)
    {
        FReactionWarpPhaseTravel Travel; Travel.PhaseId=Phase.PhaseId;
        Travel.OriginalCm=FVector::DotProduct(Integrate(C.Trajectory,{},Phase.AssetStartSeconds,Phase.AssetEndSeconds),Direction);
        Travel.PlannedCm=FVector::DotProduct(Integrate(C.Trajectory,R.Keys,Phase.AssetStartSeconds,Phase.AssetEndSeconds),Direction);
        R.PhaseTravel.Add(Travel);
    }
    R.Result = Target >= R.OriginalCm - 1.e-6 ? EReactionWarpResult::Unmodified : EReactionWarpResult::Planned;
    R.Reason = R.Result == EReactionWarpResult::Planned ? TEXT("BudgetSolved") : TEXT("OriginalPreserved");
    R.PlanHash=PlanHash(R); return R;
}
