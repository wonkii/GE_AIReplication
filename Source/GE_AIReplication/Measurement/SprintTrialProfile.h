#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR && DO_ENABLE_NET_TEST

class FJsonObject;
class UNetDriver;
class ASprintTrialCharacter;

class FSprintTrialProfile
{
public:
	bool Start(const FString& Batch, FString& Error);
	void Stop();
	void ResetCell(const FGuid& InTrialId);
	void BindEndpoints(const UNetDriver& Driver, const ASprintTrialCharacter& Subject,
		const UNetDriver& ServerDriver, const ASprintTrialCharacter& ServerSubject);
	void BindEndpoint(const UNetDriver& Driver, const ASprintTrialCharacter& Subject, bool bServer);
	void MarkPhase(int32 Phase, double Now);
	void RecordFrame(int32 Phase, double Now, float WorldDeltaTime);
	bool SaveCell(const FString& Directory, double Now, FJsonObject& Manifest);
	void DescribeEnvironment(const UWorld& World, FJsonObject& Manifest) const;
	static bool AnalyzeBatch(const FString& Directory, FString& Message, bool bReanalysis = false);
	static bool AnalyzeSplit(const FString& Directory);

private:
	struct FFrameSample
	{
		uint64 Frame;
		double Time, IntervalMs;
		float WorldDeltaMs;
		int32 Phase;
		bool bForeground;
	};

	FGuid TrialId;
	FString TracePath;
	TArray<FFrameSample> Frames;
	TArray<TPair<int32, double>> Phases;
	double LastFrame = 0.;
	uint64 NetId = 0;
	uint32 InstanceId = 0, ConnectionId = 0, PreviousVerbosity = 0;
	uint64 ServerNetId = 0;
	uint32 ServerInstanceId = 0, ServerConnectionId = 0;
	float PreviousMaxFps = 0.f;
	bool bOwnsTrace = false;
	bool bOwnsCap = false;
	bool ChannelWasEnabled[4]{};
};

#endif
