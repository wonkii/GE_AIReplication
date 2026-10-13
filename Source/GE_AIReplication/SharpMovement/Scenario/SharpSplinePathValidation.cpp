#include "SharpSplinePath.h"
#include "Components/SplineComponent.h"

// Preparation-time guard against ambiguous nearest-point projection. Full
// geometry validation never runs in the follower's per-frame steering loop.
bool ASharpSplinePath::ValidateProjectionGeometry(FString& OutReason) const
{
	if (!FMath::IsFinite(MinimumProjectionSeparation) || MinimumProjectionSeparation <= 0)
	{
		OutReason = TEXT("Projection separation must be finite and positive"); return false;
	}
	TArray<FVector> Points;
	TArray<double> Distances;
	if (!Spline->ConvertSplineToPolyLineWithDistances(ESplineCoordinateSpace::World, 25.0f, Points, Distances) ||
		Points.Num() < 2 || Points.Num() > 2048 || Distances.Num() != Points.Num())
	{
		OutReason = TEXT("Spline cannot be validated within the 2048-point geometry budget; simplify it"); return false;
	}
	for (int32 I = 1; I + 1 < Points.Num(); ++I)
	{
		if (FVector::DotProduct((Points[I] - Points[I - 1]).GetSafeNormal2D(), (Points[I + 1] - Points[I]).GetSafeNormal2D()) < -0.99)
		{
			OutReason = TEXT("Spline doubles back on itself; use PingPong traversal instead"); return false;
		}
	}
	for (int32 I = 0; I + 1 < Points.Num(); ++I)
	{
		for (int32 J = I + 2; J + 1 < Points.Num(); ++J)
		{
			// Nearby segments along the same arc naturally share a neighbourhood.
			if (Distances[J] - Distances[I + 1] < 2.0 * MinimumProjectionSeparation) { continue; }
			FVector A, B;
			FMath::SegmentDistToSegmentSafe(Points[I], Points[I + 1], Points[J], Points[J + 1], A, B);
			if (FVector::Dist2D(A, B) < MinimumProjectionSeparation)
			{
				OutReason = TEXT("Nonlocal spline sections cross or approach too closely for unambiguous projection"); return false;
			}
		}
	}
	OutReason.Empty(); return true;
}
