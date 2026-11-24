#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace SprintTrialReport
{
	bool WriteSummary(const FString& Directory, const FJsonObject& Manifest, FString& Message);
	bool WriteFailure(const FString& Directory, const FJsonObject& Manifest, const FString& Reason);
	bool WriteDiagnostics(const FString& Source, const FString& Output);
}
