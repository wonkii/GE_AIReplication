#pragma once

#include "CoreMinimal.h"
#include "SharpExperimentDebugTypes.generated.h"

/** Fixed set of development commands accepted by the server RPC. */
UENUM()
enum class ESharpExperimentDebugCommand : uint8
{
	Prepare,
	Start,
	Stop,
	Reset,
	Status
};
