#pragma once
#include "CoreMinimal.h"

/** Small append-only writer shared by actor and connection recorders. No UObject state. */
class FSharpRecordingFiles
{
public:
	bool Open(const FString& Directory, int32 MaximumRows);
	void Add(const FString& File, const FString& Header, const FString& Row);
	bool Flush();
	void Close();
	bool IsHealthy() const { return bHealthy; }
	void MarkFailed() { bHealthy = false; }
	const FString& GetDirectory() const { return Root; }
	static FString Csv(const FString& Value);
	static FString RootDirectory();
	static FString RunDirectory(const FString& RunId, int32 TrialId);
	static FString ProcessRole(const UWorld& World);
private:
	FString Root;
	TMap<FString, FString> Buffers;
	TSet<FString> CreatedFiles;
	int32 BufferedRows = 0, Limit = 4096;
	bool bHealthy = false;
};
