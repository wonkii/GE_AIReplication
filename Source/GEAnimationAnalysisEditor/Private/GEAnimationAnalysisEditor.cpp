#include "HAL/IConsoleManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
FString PythonString(FString Text)
{
    Text.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Text.ReplaceInline(TEXT("'"), TEXT("\\'"));
    Text.ReplaceInline(TEXT("\r"), TEXT("\\r"));
    Text.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    return TEXT("'") + Text + TEXT("'");
}
}

class FGEAnimationAnalysisEditorModule : public IModuleInterface
{
    TArray<IConsoleObject*> Commands;
public:
    virtual void StartupModule() override
    {
        for (const FString Action : { TEXT("Help"), TEXT("Extract"), TEXT("Analyze"), TEXT("Build"),
            TEXT("Validate"), TEXT("Preview"), TEXT("SelfTest"), TEXT("Run") })
        {
            Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(
                *(TEXT("AnimAnalysis.") + Action), TEXT("Offline joint analysis. AnimAnalysis.Help lists syntax."),
                FConsoleCommandWithArgsDelegate::CreateLambda([Action](const TArray<FString>& Args)
                {
                    auto* Python = IPythonScriptPlugin::Get();
                    if (!Python || !Python->IsPythonAvailable())
                    {
                        UE_LOG(LogTemp, Error, TEXT("[AnimAnalysis] PythonScriptPlugin is unavailable."));
                        return;
                    }
                    FString Arguments = TEXT("[") + PythonString(Action.ToLower());
                    for (const FString& Arg : Args) Arguments += TEXT(",") + PythonString(Arg);
                    Arguments += TEXT("]");
                    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Scripts/AnimationAnalysis"));
                    const FString Code = TEXT("import sys; sys.path.insert(0, ") + PythonString(Directory)
                        + TEXT("); import anim_analysis; anim_analysis.dispatch(") + Arguments + TEXT(")");
                    if (!Python->ExecPythonCommand(*Code))
                        UE_LOG(LogTemp, Error, TEXT("[AnimAnalysis] FAIL %s. See Python error above."), *Action);
                }), ECVF_Default));
        }
        for (const FString Action : {TEXT("Help"), TEXT("Migrate"), TEXT("Validate"), TEXT("SelfTest"), TEXT("Plan"), TEXT("Preview")})
        {
            Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(
                *(TEXT("ReactionWarp.")+Action),TEXT("Multi-phase warp diagnostics. ReactionWarp.Help lists syntax."),
                FConsoleCommandWithArgsDelegate::CreateLambda([Action](const TArray<FString>& Args)
                {
                    auto* Python=IPythonScriptPlugin::Get();
                    if (!Python || !Python->IsPythonAvailable()) return;
                    FString Arguments=TEXT("[")+PythonString(Action.ToLower());
                    for (const auto& Arg : Args) Arguments+=TEXT(",")+PythonString(Arg);
                    Arguments+=TEXT("]");
                    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Scripts/ReactionWarp"));
                    const FString Code=TEXT("import sys; sys.path.insert(0,")+PythonString(Directory)+
                        TEXT("); import reaction_warp; reaction_warp.dispatch(")+Arguments+TEXT(")");
                    if (!Python->ExecPythonCommand(*Code)) UE_LOG(LogTemp,Error,TEXT("[ReactionWarp] FAIL %s"),*Action);
                }),ECVF_Default));
        }
    }
    virtual void ShutdownModule() override
    {
        for (auto* Command : Commands) IConsoleManager::Get().UnregisterConsoleObject(Command);
        Commands.Empty();
    }
};

IMPLEMENT_MODULE(FGEAnimationAnalysisEditorModule, GEAnimationAnalysisEditor)
