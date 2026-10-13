#include "SharpSplinePath.h"

#include "Components/SplineComponent.h"

ASharpSplinePath::ASharpSplinePath()
{
	PrimaryActorTick.bCanEverTick = false;
	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);
}

bool ASharpSplinePath::ValidatePath(FString& OutReason) const
{
	if (PathId.IsNone() || PathRevision < 1 || !Spline || Spline->GetNumberOfSplinePoints() < 2 || Spline->IsClosedLoop())
	{
		OutReason = TEXT("M1 requires a PathId, revision >= 1 and an open spline with at least two points");
		return false;
	}
	if (!FMath::IsFinite(MinimumSegmentLength) || MinimumSegmentLength <= 0.0f ||
		!FMath::IsFinite(PlanarHeightTolerance) || PlanarHeightTolerance < 0.0f ||
		!FMath::IsFinite(GetLength()) || GetLength() < MinimumSegmentLength)
	{
		OutReason = TEXT("Invalid path length or validation tolerances");
		return false;
	}
	const FVector Scale = Spline->GetComponentScale();
	if (Scale.ContainsNaN() || Scale.X <= 0 || !FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z))
	{
		OutReason = TEXT("Use a positive uniform scale for planar spline projection"); return false;
	}
	if (!FMath::IsFinite(StartHeightOffset) || StartHeightOffset < 0)
	{
		OutReason = TEXT("Start height offset must be finite and nonnegative"); return false;
	}
	const FVector First = GetLocation(0.0f);
	for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		const FVector Point = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		const FVector Tangent = Spline->GetTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);
		if (Point.ContainsNaN() || Tangent.ContainsNaN() || FMath::Abs(Point.Z - First.Z) > PlanarHeightTolerance ||
			FMath::Abs(Tangent.Z) > PlanarHeightTolerance)
		{
			OutReason = TEXT("M1 requires a finite horizontal spline, including its tangents");
			return false;
		}
		if (Index > 0 && FVector::Dist2D(Point, Spline->GetLocationAtSplinePoint(Index - 1, ESplineCoordinateSpace::World)) < MinimumSegmentLength)
		{
			OutReason = TEXT("Adjacent spline points are too close");
			return false;
		}
	}
	return ValidateProjectionGeometry(OutReason);
}

float ASharpSplinePath::GetLength() const
{
	return Spline->GetSplineLength();
}

FVector ASharpSplinePath::GetLocation(float Distance) const
{
	return Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

FVector ASharpSplinePath::GetDirection(float Distance) const
{
	return Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
}

float ASharpSplinePath::ProjectDistance(const FVector& WorldPosition) const
{
	FVector PlanarPosition = WorldPosition;
	PlanarPosition.Z = GetLocation(0.0f).Z;
	return Spline->GetDistanceAlongSplineAtLocation(PlanarPosition, ESplineCoordinateSpace::World);
}

FString ASharpSplinePath::GetGeometrySignature() const
{
	FString Result = FString::Printf(TEXT("Points=%d Reparam=%d Closed=%d"), Spline->GetNumberOfSplinePoints(), Spline->ReparamStepsPerSegment, Spline->IsClosedLoop());
	for (int32 I = 0; I < Spline->GetNumberOfSplinePoints(); ++I)
	{
		Result.Appendf(TEXT(" Key=%.9g Type=%d"), Spline->GetInputKeyValueAtSplinePoint(I), static_cast<int32>(Spline->GetSplinePointType(I)));
		const FVector Values[] = {Spline->GetLocationAtSplinePoint(I, ESplineCoordinateSpace::World),
			Spline->GetArriveTangentAtSplinePoint(I, ESplineCoordinateSpace::World), Spline->GetLeaveTangentAtSplinePoint(I, ESplineCoordinateSpace::World)};
		for (const FVector& V : Values) { Result.Appendf(TEXT(" (%.17g,%.17g,%.17g)"), V.X, V.Y, V.Z); }
	}
	return Result;
}
