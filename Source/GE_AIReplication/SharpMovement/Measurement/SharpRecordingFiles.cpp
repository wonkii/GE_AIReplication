#include "SharpRecordingFiles.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpRecordingFiles, Log, All);

FString FSharpRecordingFiles::Csv(const FString& Value)
{
	return TEXT("\"") + Value.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
}

FString FSharpRecordingFiles::RootDirectory()
{
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("SharpOutDir="), Override) && !Override.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(Override, TEXT("Native")));
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SharpExperiments/Native")));
}

FString FSharpRecordingFiles::RunDirectory(const FString& RunId, int32 TrialId)
{
	// Keep the descriptive prefix short: Windows directory creation can fail near
	// MAX_PATH even before filename creation switches to extended paths. The full
	// identity remains in every CSV and manifest; the hash distinguishes prefixes.
	const FString SafeRun = FPaths::MakeValidFileName(RunId).Left(16) + FString::Printf(TEXT("_%08x"), GetTypeHash(RunId));
	return FPaths::Combine(RootDirectory(), SafeRun, FString::Printf(TEXT("Trial_%d"), TrialId));
}

FString FSharpRecordingFiles::ProcessRole(const UWorld& World)
{
	FString ClientId = TEXT("0");
	FParse::Value(FCommandLine::Get(), TEXT("SharpClientId="), ClientId);
	const FString Role = World.GetNetMode() == NM_Client ? TEXT("Client_") + FPaths::MakeValidFileName(ClientId) : TEXT("Server");
	return FString::Printf(TEXT("%s_%u_%08x"), *Role, FPlatformProcess::GetCurrentProcessId(), GetTypeHash(World.GetPathName()));
}

bool FSharpRecordingFiles::Open(const FString& Directory, int32 MaximumRows)
{
	Close(); Root = Directory; Limit = MaximumRows;
	bHealthy = IFileManager::Get().MakeDirectory(*Root, true);
	if (!bHealthy)
	{
		UE_LOG(LogSharpRecordingFiles, Error, TEXT("Cannot create recording directory %s (length=%d). Use a shorter SharpOutDir/BatchId if the path is near the Windows directory limit."), *Root, Root.Len());
	}
	return bHealthy;
}

void FSharpRecordingFiles::Add(const FString& File, const FString& Header, const FString& Row)
{
	if (!bHealthy) { return; }
	FString& Buffer = Buffers.FindOrAdd(File);
	if (!CreatedFiles.Contains(File))
	{
		const FString Path = FPaths::Combine(Root, File);
		// A session directory is unique; refuse to blend another execution into it.
		if (IFileManager::Get().FileExists(*Path))
		{
			bHealthy = false; UE_LOG(LogSharpRecordingFiles, Error, TEXT("Recording already exists: %s"), *Path); return;
		}
		Buffer += Header + TEXT("\n"); CreatedFiles.Add(File);
	}
	if (!Row.IsEmpty()) { Buffer += Row + TEXT("\n"); ++BufferedRows; }
	if (BufferedRows >= Limit) { Flush(); }
}

bool FSharpRecordingFiles::Flush()
{
	if (!bHealthy) { return false; }
	for (TPair<FString, FString>& Entry : Buffers)
	{
		if (Entry.Value.IsEmpty()) { continue; }
		if (!FFileHelper::SaveStringToFile(Entry.Value, *FPaths::Combine(Root, Entry.Key),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
		{
			bHealthy = false; UE_LOG(LogSharpRecordingFiles, Error, TEXT("Recording write failed: %s/%s"), *Root, *Entry.Key); return false;
		}
		Entry.Value.Empty();
	}
	BufferedRows = 0; return true;
}

void FSharpRecordingFiles::Close()
{
	Flush(); Buffers.Reset(); CreatedFiles.Reset(); BufferedRows = 0; Root.Empty(); bHealthy = false;
}
