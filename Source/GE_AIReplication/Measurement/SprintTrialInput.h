#pragma once

#include "CoreMinimal.h"
#include "Engine/NetDriver.h"

struct FSprintInputEdge
{
	double Seconds;
	bool bPressed;
};

namespace SprintTrialInput
{
	inline TArray<FSprintInputEdge> TapEdges(int32 TapMs)
	{
		const double Duration = TapMs / 1000.;
		if (TapMs == 0) { return {{2., true}, {2.1, false}, {2.2, true}, {2.3, false}}; }
		return {{2., true}, {2. + Duration, false}};
	}

	inline FPacketSimulationSettings OwnerEmulation(int32 Jitter, int32 Loss)
	{
		FPacketSimulationSettings Settings;
		if (Jitter > 0 || Loss > 0)
		{
			Settings.PktLagMin = Settings.PktIncomingLagMin = Settings.PktIncomingLagMax = 50;
			Settings.PktLagMax = 50 + Jitter;
			Settings.PktJitter = Jitter;
			Settings.PktLoss = Settings.PktIncomingLoss = Loss;
		}
		return Settings;
	}
}
