#include "ReactionWarpArtifacts.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"

FString ReactionWarpArtifacts::NewTask(const FString& Command)
{
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Tasks")/
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-reaction-warp-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Readme=TEXT("# Reaction Warp experiment\n\nPurpose: compare server-authored multi-phase plans and runtime movement.\nCreated: ")+
        FDateTime::UtcNow().ToIso8601()+TEXT(" (UTC)\nCommand: ")+Command+
        TEXT("\n\nFiles: trials.csv includes pending/rejected/fallback/cancelled trials; plan-*.json are server payloads. ")+
        TEXT("Runtime capsule/feet/contact samples are in the HitWallProbe session directories logged by the probe. ")+
        TEXT("Use ReactionWarp.Preview <this directory> to generate/open report.html.\n\n")+
        TEXT("Outcome: running; visual quality and owner prediction are NOT approved. Retain raw plans and CSV with reports; share the whole task directory.\n");
    return FFileHelper::SaveStringToFile(Readme,*(Directory/TEXT("README.md"))) ? Directory : FString();
}
void ReactionWarpArtifacts::SavePlan(const FString& Directory, const FString& Name, const FReactionWarpPlan& Plan)
{
    FString Text; FJsonObjectConverter::UStructToJsonObjectString(Plan,Text);
    FFileHelper::SaveStringToFile(Text,*(Directory/(Name+TEXT(".json"))));
}
