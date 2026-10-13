#include "SharpMovementRecorderComponent.h"
#include "../Enemy/SharpEnemyCharacter.h"
#include "../Experiment/SharpEnemyExperimentComponent.h"
#include "../Experiment/SharpExperimentSettings.h"
#include "../Movement/SharpSplineFollowerComponent.h"
#include "../Scenario/SharpSplinePath.h"
#include "../Scenario/SharpImpulseVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

void USharpMovementRecorderComponent::WriteManifest(const FString& Outcome)
{
	if (!Enemy || Files.GetDirectory().IsEmpty()) { return; }
	TSharedRef<FJsonObject> Json = Manifest.IsValid() ? Manifest.ToSharedRef() : MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("SchemaVersion"), 2);
	Json->SetStringField(TEXT("RunId"), Context.Identity.RunId); Json->SetNumberField(TEXT("TrialId"), Context.Identity.TrialId);
	Json->SetNumberField(TEXT("ActorId"), RecordedActorId); Json->SetNumberField(TEXT("Generation"), Context.Generation);
	Json->SetNumberField(TEXT("Segment"), Context.Segment); Json->SetStringField(TEXT("PathId"), Context.PathId.ToString());
	Json->SetNumberField(TEXT("PathRevision"), Context.PathRevision);
	Json->SetStringField(TEXT("Mode"), StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(static_cast<int64>(Context.Identity.Mode)));
	Json->SetStringField(TEXT("Outcome"), Outcome); Json->SetStringField(TEXT("State"), UEnum::GetValueAsString(Context.State));
	Json->SetBoolField(TEXT("Healthy"), Files.IsHealthy());
	Json->SetNumberField(TEXT("ServerStartTime"), Context.SegmentStartTime); Json->SetNumberField(TEXT("ServerEndTime"), Context.ServerEndTime);
	Json->SetNumberField(TEXT("RecordingFinishWorldTime"), GetWorld()->GetTimeSeconds());
	if (!Manifest.IsValid())
	{
	Json->SetNumberField(TEXT("ProcessId"), FPlatformProcess::GetCurrentProcessId());
	Json->SetStringField(TEXT("Role"), FSharpRecordingFiles::ProcessRole(*GetWorld()));
	Json->SetStringField(TEXT("Map"), GetWorld()->GetMapName()); Json->SetStringField(TEXT("Engine"), FEngineVersion::Current().ToString());
	Json->SetStringField(TEXT("Clock"), TEXT("ServerTime=GameState estimate on client; WallTime=UTC ticks; cross-host wall alignment requires synchronized clocks"));
	Json->SetObjectField(TEXT("Recording"), FJsonObjectConverter::UStructToJsonObject(Context.Recording));
	Json->SetObjectField(TEXT("Prediction"), FJsonObjectConverter::UStructToJsonObject(Context.Prediction));
	if (const USharpExperimentSettings* Settings = Enemy->Experiment->GetPreparedSettings())
	{
		TSharedRef<FJsonObject> SettingsJson = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(Settings->GetClass(), Settings, SettingsJson);
		Json->SetObjectField(TEXT("Settings"), SettingsJson);
	}
	if (const ASharpSplinePath* Path = Enemy->Experiment->GetPath())
	{
		Json->SetStringField(TEXT("PathGeometry"), Path->GetGeometrySignature());
		Json->SetStringField(TEXT("PathTransform"), Path->GetActorTransform().ToString());
		Json->SetNumberField(TEXT("PathLength"), Path->GetLength());
		Json->SetObjectField(TEXT("Follow"), FJsonObjectConverter::UStructToJsonObject(Enemy->SplineFollower->GetPreparedSettings()));
	}
	const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	TSharedRef<FJsonObject> CMC = MakeShared<FJsonObject>();
	CMC->SetNumberField(TEXT("MaxAcceleration"), Movement->MaxAcceleration); CMC->SetNumberField(TEXT("GroundFriction"), Movement->GroundFriction);
	CMC->SetNumberField(TEXT("BrakingDecelerationWalking"), Movement->BrakingDecelerationWalking);
	CMC->SetNumberField(TEXT("GravityScale"), Movement->GravityScale); CMC->SetNumberField(TEXT("NetworkSmoothingMode"), static_cast<int32>(Movement->NetworkSmoothingMode));
	CMC->SetNumberField(TEXT("Mass"), Movement->Mass); CMC->SetNumberField(TEXT("AirControl"), Movement->AirControl);
	CMC->SetNumberField(TEXT("BrakingFrictionFactor"), Movement->BrakingFrictionFactor);
	CMC->SetNumberField(TEXT("FallingLateralFriction"), Movement->FallingLateralFriction);
	CMC->SetNumberField(TEXT("NetworkSimulatedSmoothLocationTime"), Movement->NetworkSimulatedSmoothLocationTime);
	CMC->SetNumberField(TEXT("NetworkMaxSmoothUpdateDistance"), Movement->NetworkMaxSmoothUpdateDistance);
	CMC->SetNumberField(TEXT("CapsuleRadius"), Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius());
	CMC->SetNumberField(TEXT("CapsuleHalfHeight"), Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	CMC->SetNumberField(TEXT("NetUpdateFrequency"), Enemy->GetNetUpdateFrequency()); CMC->SetNumberField(TEXT("MinNetUpdateFrequency"), Enemy->GetMinNetUpdateFrequency());
	Json->SetObjectField(TEXT("CharacterMovement"), CMC);
	Json->SetNumberField(TEXT("PreparedNetUpdateFrequency"), Enemy->Experiment->GetPreparedNetFrequency());
	Json->SetNumberField(TEXT("PreparedMinNetUpdateFrequency"), Enemy->Experiment->GetPreparedMinNetFrequency());
	TArray<TSharedPtr<FJsonValue>> Volumes;
	for (TActorIterator<ASharpImpulseVolume> It(GetWorld()); It; ++It)
	{
		if (!It->AppliesTo(Enemy->Experiment->GetManager())) { continue; }
		TSharedRef<FJsonObject> Volume = MakeShared<FJsonObject>();
		Volume->SetStringField(TEXT("Id"), It->VolumeId.ToString()); Volume->SetStringField(TEXT("Transform"), It->TriggerBox->GetComponentTransform().ToString());
		Volume->SetStringField(TEXT("Extent"), It->TriggerBox->GetUnscaledBoxExtent().ToString());
		Volume->SetObjectField(TEXT("Settings"), FJsonObjectConverter::UStructToJsonObject(It->Settings)); Volumes.Add(MakeShared<FJsonValueObject>(Volume));
	}
	Json->SetArrayField(TEXT("ImpulseVolumes"), Volumes);
	TSharedRef<FJsonObject> Network = MakeShared<FJsonObject>();
	const TCHAR* Names[] = {TEXT("net.UseAdaptiveNetUpdateFrequency"), TEXT("net.PktLag"), TEXT("net.PktLagMin"), TEXT("net.PktLagMax"),
		TEXT("net.PktLoss"), TEXT("net.PktIncomingLagMin"), TEXT("net.PktIncomingLagMax"), TEXT("net.PktIncomingLoss"), TEXT("t.MaxFPS")};
	for (const TCHAR* Name : Names)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name)) { Network->SetStringField(Name, Variable->GetString()); }
	}
	Json->SetObjectField(TEXT("NetworkCVars"), Network);
	#if DO_ENABLE_NET_TEST
	if (const UNetDriver* Driver = GetWorld()->GetNetDriver())
	{
		Json->SetObjectField(TEXT("NetworkEmulation"), FJsonObjectConverter::UStructToJsonObject(Driver->PacketSimulationSettings));
	}
	#endif
	}
	Manifest = Json;
	FString Text; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Json, Writer);
	if (!FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Files.GetDirectory(), TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("Sharp manifest write failed: %s"), *Files.GetDirectory());
		Files.MarkFailed();
	}
}
