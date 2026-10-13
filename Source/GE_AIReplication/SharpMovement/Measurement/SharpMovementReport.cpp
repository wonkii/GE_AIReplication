#include "SharpMovementReport.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

ASharpMovementReport::ASharpMovementReport()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ASharpMovementReport::GenerateReport()
{
#if WITH_EDITOR
	if (PythonExecutable.IsEmpty() || !FMath::IsFinite(BudgetTolerance) || BudgetTolerance < 0 ||
		!FMath::IsFinite(MinimumCoverage) || MinimumCoverage < 0 || MinimumCoverage > 1)
	{
		LastResult = TEXT("Set a Python executable and valid comparison parameters"); return;
	}
	const FString Script = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/Analysis/sharp_metrics.py")));
	if (!FPaths::FileExists(Script)) { LastResult = TEXT("Analysis script not found"); return; }
	if (RecordingDirectories.IsEmpty()) { RecordingDirectories.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SharpExperiments")))); }
	if (OutputDirectory.IsEmpty()) { OutputDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SharpExperiments/_analysis"))); }
	// Direct process arguments, never shell commands. Reject embedded quotes rather than misquote paths.
	FString Arguments = TEXT("\"") + Script + TEXT("\"");
	for (const FString& Directory : RecordingDirectories)
	{
		if (Directory.Contains(TEXT("\""))) { LastResult = TEXT("Recording path contains a quote"); return; }
		Arguments += TEXT(" \"") + FPaths::ConvertRelativePathToFull(Directory) + TEXT("\"");
	}
	if (OutputDirectory.Contains(TEXT("\""))) { LastResult = TEXT("Output path contains a quote"); return; }
	Arguments += FString::Printf(TEXT(" --native --clock %s --out \"%s\" --budget-tolerance %.6f --minimum-coverage %.6f"),
		bSameHostWallClock ? TEXT("wall") : TEXT("server"), *FPaths::ConvertRelativePathToFull(OutputDirectory), BudgetTolerance, MinimumCoverage);
	int32 ReturnCode = -1; FString StdOut, StdErr;
	const bool bLaunched = FPlatformProcess::ExecProcess(*PythonExecutable, *Arguments, &ReturnCode, &StdOut, &StdErr);
	LastResult = bLaunched && ReturnCode == 0 ? TEXT("Report generated: ") + OutputDirectory : TEXT("Report failed: ") + StdErr;
	UE_LOG(LogTemp, Display, TEXT("[Sharp][Report] %s\n%s"), *LastResult, *StdOut);
#else
	LastResult = TEXT("Generate reports in the Editor or run Scripts/Analysis/sharp_metrics.py offline");
#endif
}

void ASharpMovementReport::OpenReport()
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(OutputDirectory, TEXT("report.html")));
	if (FPaths::FileExists(Path)) { FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path); }
	else { LastResult = TEXT("Generate a report first"); }
}
