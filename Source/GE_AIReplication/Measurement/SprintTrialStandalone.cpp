#include "CoreMinimal.h"

#if WITH_EDITOR && DO_ENABLE_NET_TEST

#include "SprintTrialCapture.h"
#include "SprintTrialCharacter.h"
#include "SprintTrialInput.h"
#include "SprintTrialProfile.h"
#include "SprintTrialReport.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "GameFramework/GameNetworkManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Trace/Trace.h"

namespace
{
	constexpr double MeasureSeconds = 60.;
	constexpr double TailSeconds = 10.;
	constexpr double WarmupSeconds = 3.;

	bool ReadJson(const FString& Path, TSharedPtr<FJsonObject>& Json)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
	}

	bool WriteAtomic(const FString& Path, const FString& Text)
	{
		const FString Temp = Path + TEXT(".tmp");
		return FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			&& IFileManager::Get().Move(*Path, *Temp);
	}

	bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		return FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) && WriteAtomic(Path, Text);
	}

	class FSprintStandalone
	{
	public:
		void Initialize()
		{
			if (!FParse::Value(FCommandLine::Get(), TEXT("SprintCostRole="), Role)) { return; }
			if (!FParse::Value(FCommandLine::Get(), TEXT("SprintCostDirectory="), Root)
				|| !ReadJson(Path(TEXT("plan.json")), Plan))
			{
				Fail(TEXT("Could not read the sprint experiment plan."));
				return;
			}
			bServer = Role == TEXT("server");
			bOwner = Role == TEXT("owner");
			if ((!bOwner && !bServer && Role != TEXT("observer")) ||
				!FGuid::ParseExact(Plan->GetStringField(TEXT("RunId")), EGuidFormats::Digits, TrialId))
			{
				Fail(TEXT("Invalid sprint experiment role or run id."));
				return;
			}
			const IConsoleVariable* Packed = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUsePackedMovementRPCs"));
			if (!Packed || Packed->GetInt() == 0) { Fail(TEXT("Packed movement RPCs are required.")); return; }
			if (auto* Idle = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground"))) { Idle->Set(0); }
			Variant = Plan->GetStringField(TEXT("Variant"));
			Pattern = Plan->GetStringField(TEXT("Scenario"));
			FString Error;
			if ((bOwner || bServer) && !Profile.Start(Path(Role), Error)) { Fail(Error); return; }
			Profile.ResetCell(TrialId);
			for (const auto& Value : Plan->GetArrayField(TEXT("InputEdges")))
			{
				const auto& Edge = Value->AsObject();
				Edges.Add({Edge->GetNumberField(TEXT("Seconds")), Edge->GetBoolField(TEXT("Pressed"))});
			}
			Heading = Plan->GetNumberField(TEXT("HeadingDegrees"));
			Foot = FVector(Plan->GetNumberField(TEXT("X")), Plan->GetNumberField(TEXT("Y")), Plan->GetNumberField(TEXT("Z")));
			Deadline = FPlatformTime::Seconds() + 150.;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FSprintStandalone::Tick));
			FWorldDelegates::OnWorldPreActorTick.AddRaw(this, &FSprintStandalone::TickWorld);
		}

	private:
		FString Path(const FString& Name) const { return FPaths::Combine(Root, Name); }
		FString RolePath(const FString& Name) const { return FPaths::Combine(Root, Role, Name); }
		bool Exists(const FString& Name) const { return IFileManager::Get().FileExists(*Path(Name)); }

		bool Fail(const FString& Reason)
		{
			UE_LOG(LogTemp, Error, TEXT("SprintCost %s: %s"), *Role, *Reason);
			bFinished = true;
			if (Capture.IsValid()) { Capture->Fail(Reason); }
			Profile.Stop();
			if (!Root.IsEmpty() && !Role.IsEmpty()) { WriteAtomic(RolePath(TEXT("role_failed.txt")), Reason); }
			FPlatformMisc::RequestExitWithStatus(false, 1);
			return false;
		}

		UWorld* FindWorld() const
		{
			if (!GEngine) { return nullptr; }
			for (const auto& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate->WorldType == EWorldType::Game &&
					Candidate->GetNetMode() == (bServer ? NM_DedicatedServer : NM_Client)) { return Candidate; }
			}
			return nullptr;
		}

		bool Spawn(UWorld& InWorld, APlayerController& Controller)
		{
			auto* Original = Cast<ACharacter>(Controller.GetPawn());
			if (!Original) { return false; }
			UClass* Class = Variant == TEXT("SeparateRpc") ? ASprintRpcCharacter::StaticClass() :
				Variant == TEXT("MoveData") ? ASprintMoveDataCharacter::StaticClass() : ASprintMoveDataBitCharacter::StaticClass();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
			const FVector Position = Foot + FVector(0, 0, Class->GetDefaultObject<ASprintTrialCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			Original->SetActorHiddenInGame(true);
			Original->SetActorEnableCollision(false);
			Original->GetCharacterMovement()->SetComponentTickEnabled(false);
			auto* Pawn = InWorld.SpawnActor<ASprintTrialCharacter>(Class, Position, FRotator(0., Heading, 0.), Params);
			if (!Pawn || !Pawn->InitializePresentation(*Original)) { return false; }
			Pawn->TrialId = TrialId;
			Pawn->bAlwaysRelevant = true;
			Controller.Possess(Pawn);
			Controller.SetControlRotation(FRotator(0., Heading, 0.));
			Subject = Pawn;
			Driver = InWorld.GetNetDriver();
			return Controller.GetPawn() == Pawn;
		}

		bool Prepare()
		{
			UWorld* Candidate = FindWorld();
			if (!Candidate || !Candidate->GetNetDriver()) { return true; }
			if (bServer)
			{
				auto It = Candidate->GetPlayerControllerIterator();
				APlayerController* Controller = It ? It->Get() : nullptr;
				if (!Controller || !Controller->GetPawn() || !Controller->GetNetConnection()) { return true; }
				if (!Subject.IsValid() && !Spawn(*Candidate, *Controller)) { return Fail(TEXT("Could not spawn the trial character at the specified foot location.")); }
				if (Controller->AcknowledgedPawn != Subject.Get() || !Subject->GetCharacterMovement()->IsMovingOnGround()
					|| Subject->GetCharacterMovement()->GetPredictionData_Server_Character()->CurrentClientTimeStamp <= 0.f)
				{
					return true;
				}
			}
			else
			{
				APlayerController* Controller = Candidate->GetFirstPlayerController();
				if (!Controller || !Controller->GetPawn()) { return true; }
				if (bOwner)
				{
					auto* Local = Cast<ASprintTrialCharacter>(Controller->GetPawn());
					if (!Local || Local->TrialId != TrialId || Controller->AcknowledgedPawn != Local
						|| !Local->GetCharacterMovement()->IsMovingOnGround()) { return true; }
					Subject = Local;
				}
				else if (!Subject.IsValid())
				{
					for (TActorIterator<ASprintTrialCharacter> It(Candidate); It; ++It)
					{
						if (It->TrialId == TrialId) { Subject = *It; break; }
					}
				}
				if (!Subject.IsValid() || !Subject->HasTrialPresentation()) { return true; }
				Controller->SetViewTarget(Subject.Get());
			}
			World = Candidate;
			Driver = Candidate->GetNetDriver();
			Capture = USprintTrialCapture::Find(Candidate);
			if (!Capture.IsValid() || !Capture->Open(Root, Role, TrialId, true)) { return Fail(TEXT("Could not open role capture.")); }
			if (bOwner)
			{
				Driver->SetPacketSimulationSettings(SprintTrialInput::OwnerEmulation(Plan->GetIntegerField(TEXT("JitterMs")),
					Plan->GetIntegerField(TEXT("LossPct"))));
			}
			else { Driver->SetPacketSimulationSettings(FPacketSimulationSettings()); }
			if (bOwner || bServer) { Profile.BindEndpoint(*Driver, *Subject, bServer); }
			if (!WriteAtomic(RolePath(TEXT("ready.txt")), FString::Printf(TEXT("%.9f"), FPlatformTime::Seconds())))
			{
				return Fail(TEXT("Could not mark role ready."));
			}
			bReady = true;
			return true;
		}

		bool Tick(float)
		{
			const double Now = FPlatformTime::Seconds();
			if (Now >= Deadline) { return Fail(TEXT("Role coordination timed out.")); }
			if (!bReady) { return Prepare(); }
			if (!bCaptured && (!Subject.IsValid() || !World.IsValid() || !Driver.IsValid())) { return Fail(TEXT("Trial participant disconnected.")); }
			if (StartSeconds < 0.)
			{
				FString Start;
				if (bOwner && Exists(TEXT("server/ready.txt")) && Exists(TEXT("observer/ready.txt")) &&
					!Exists(TEXT("start.txt")) && !WriteAtomic(Path(TEXT("start.txt")), FString::Printf(TEXT("%.9f"), Now + WarmupSeconds + 1.)))
				{
					return Fail(TEXT("Could not schedule shared start."));
				}
				if (FFileHelper::LoadFileToString(Start, *Path(TEXT("start.txt"))))
				{
					StartSeconds = FCString::Atod(*Start);
					if (StartSeconds - Now < WarmupSeconds) { return Fail(TEXT("Shared start was received too late.")); }
					Capture->Start();
					if (bOwner && Pattern == TEXT("Hold")) { Subject->GetSprintPolicy()->Request(true); }
				}
				return true;
			}
			if (!bCaptured && Now >= StartSeconds + MeasureSeconds + TailSeconds)
			{
				if (!bCostSaved || !Capture->Finish()) { return Fail(TEXT("Could not finalize role data.")); }
				Profile.Stop();
				bCaptured = true;
				Deadline = Now + 120.;
			}
			if (!bCaptured || ((bOwner || bServer) && UE::Trace::IsTracing())) { return true; }
			if (!bRoleDone)
			{
				if (!WriteAtomic(RolePath(TEXT("role_done.txt")), TEXT("Completed"))) { return Fail(TEXT("Could not mark role complete.")); }
				bRoleDone = true;
			}
			// Keep both connections alive until the authority has all three final files.
			if (bServer && Exists(TEXT("owner/role_done.txt")) && Exists(TEXT("observer/role_done.txt")))
			{
				if (!Finalize()) { return Fail(TEXT("Could not produce split-process reports.")); }
			}
			if (!Exists(TEXT("complete.txt"))) { return true; }
			bFinished = true;
			FPlatformMisc::RequestExit(false);
			return false;
		}

		const UNetConnection* Connection() const
		{
			return bServer ? Subject->GetNetConnection() : Driver->ServerConnection.Get();
		}

		void TickWorld(UWorld* InWorld, ELevelTick, float Delta)
		{
			if (bFinished || bCaptured || StartSeconds < 0. || InWorld != World.Get() || !Subject.IsValid()) { return; }
			const double Now = FPlatformTime::Seconds();
			const double Elapsed = Now - StartSeconds;
			const bool bMeasuring = Elapsed >= 0. && Elapsed < MeasureSeconds;
			const int32 Phase = bMeasuring ? FMath::Min(4, int32(Elapsed / (MeasureSeconds / 5.))) : INDEX_NONE;
			if (bMeasuring && Delta > .125f && bOwner) { Fail(TEXT("Owner frame exceeded 125 ms.")); return; }
			if (bOwner && !Subject->GetCharacterMovement()->IsMovingOnGround())
			{
				Fail(TEXT("The trial character left the floor."));
				return;
			}
			if (bMeasuring && FVector::DistSquared2D(Subject->GetActorLocation(), Foot) > FMath::Square(2000.))
			{
				Fail(TEXT("The trial character left the configured course."));
				return;
			}
			if (!bOwner && !bServer)
			{
				Capture->Record(*Subject, TEXT("RemoteSample"));
				if (Elapsed >= MeasureSeconds) { bCostSaved = true; }
				return;
			}
			const auto* Net = Connection();
			if (!Net) { Fail(TEXT("The owner connection was lost.")); return; }
			if (bMeasuring && Phase != CurrentPhase)
			{
				if (Phase != CurrentPhase + 1) { Fail(TEXT("A measurement phase was skipped.")); return; }
				if (Phase == 0)
				{
					Profile.BindEndpoint(*Driver, *Subject, bServer);
					StartBytes = Net->OutTotalBytes;
					StartPackets = Net->OutTotalPackets;
					CounterStart = Now;
				}
				Profile.MarkPhase(Phase, Now);
				CurrentPhase = Phase;
			}
			if (bMeasuring && bOwner) { Profile.RecordFrame(Phase, Now, Delta); }
			if (Elapsed >= MeasureSeconds && !bCostSaved)
			{
				if (CurrentPhase != 4 || Net->OutTotalBytes < StartBytes || Net->OutTotalPackets < StartPackets)
				{
					Fail(TEXT("Incomplete measurement window or connection counter reset."));
					return;
				}
				const auto Endpoint = MakeShared<FJsonObject>();
				Endpoint->SetNumberField(TEXT("WindowStart"), CounterStart);
				Endpoint->SetNumberField(TEXT("WindowEnd"), Now);
				Endpoint->SetNumberField(TEXT("SentBytes"), Net->OutTotalBytes - StartBytes);
				Endpoint->SetNumberField(TEXT("SentPackets"), Net->OutTotalPackets - StartPackets);
				Endpoint->SetNumberField(TEXT("ServerMaxTickRate"), Driver->GetNetServerMaxTickRate());
				Endpoint->SetStringField(TEXT("StartFoot"), Foot.ToString());
				Endpoint->SetStringField(TEXT("ConditionHash"), Plan->GetStringField(TEXT("ConditionHash")));
				Endpoint->SetStringField(TEXT("ProfileWalkCondition"), Plan->GetStringField(TEXT("ConditionHash")));
				Profile.DescribeEnvironment(*InWorld, *Endpoint);
				if (!Profile.SaveCell(Path(Role), Now, *Endpoint) || !WriteJson(RolePath(TEXT("endpoint.json")), Endpoint))
				{
					Fail(TEXT("Could not save endpoint data."));
					return;
				}
				bCostSaved = true;
			}
			if (!bOwner) { return; }
			auto* Policy = Subject->GetSprintPolicy();
			Subject->TrialPhase = bMeasuring ? 1 : INDEX_NONE;
			bool bRun = bMeasuring && Pattern == TEXT("Hold");
			while (bMeasuring && Edges.IsValidIndex(NextEdge) && Elapsed >= Edges[NextEdge].Seconds)
			{
				const auto& Edge = Edges[NextEdge++];
				Capture->Record(*Subject, TEXT("PlannedInput"), 0.f, 0, FVector::ZeroVector, NextEdge, Phase, Edge.Seconds, Edge.bPressed);
			}
			if (bMeasuring && NextEdge) { bRun = Edges[NextEdge - 1].bPressed; }
			if (Policy->IsRequested() != bRun && Elapsed >= 0.) { Policy->Request(bRun); }
			if (bMeasuring || (Elapsed < 0. && Pattern == TEXT("Hold")))
			{
				// A repeating input direction keeps the long run near the verified starting area.
				const double Angle = FMath::DegreesToRadians(Heading) + Elapsed * UE_DOUBLE_PI;
				Subject->AddMovementInput(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.));
			}
		}

		bool Finalize()
		{
			TSharedPtr<FJsonObject> Owner, Server;
			if (!ReadJson(Path(TEXT("owner/endpoint.json")), Owner) || !ReadJson(Path(TEXT("server/endpoint.json")), Server))
			{
				return false;
			}
			const double StartSkew = FMath::Abs(Owner->GetNumberField(TEXT("WindowStart")) - Server->GetNumberField(TEXT("WindowStart")));
			const double EndSkew = FMath::Abs(Owner->GetNumberField(TEXT("WindowEnd")) - Server->GetNumberField(TEXT("WindowEnd")));
			if (StartSkew > .125 || EndSkew > .125) { return false; }
			for (const auto& Field : Owner->Values) { Plan->SetField(Field.Key, Field.Value); }
			for (const TCHAR* Key : {TEXT("ProfileServerInstance"), TEXT("ProfileServerConnection"), TEXT("ProfileServerNetId")})
			{
				Plan->SetField(Key, Server->TryGetField(Key));
			}
			Plan->SetObjectField(TEXT("OwnerEndpoint"), Owner);
			Plan->SetObjectField(TEXT("ServerEndpoint"), Server);
			Plan->SetNumberField(TEXT("StartSkewMs"), StartSkew * 1000.);
			Plan->SetNumberField(TEXT("EndSkewMs"), EndSkew * 1000.);
			Plan->SetStringField(TEXT("Clock"), TEXT("Same-host Windows FPlatformTime/QPC"));
			Plan->SetStringField(TEXT("EngineVersion"), FEngineVersion::Current().ToString());
			Plan->SetStringField(TEXT("TrialBuild"), ANSI_TO_TCHAR(__DATE__ " " __TIME__));
			Plan->SetStringField(TEXT("ConnectionCounter"), TEXT("UNetConnection::OutTotalBytes on each sender: flush packet bytes plus engine PacketOverhead, including retransmissions and simulated outgoing drops"));
			Plan->SetStringField(TEXT("Outcome"), TEXT("Completed"));
			const auto* NetSettings = GetDefault<AGameNetworkManager>();
			Plan->SetNumberField(TEXT("ClientErrorUpdateRateLimit"), NetSettings->ClientErrorUpdateRateLimit);
			Plan->SetNumberField(TEXT("MaxPositionErrorSquared"), NetSettings->MAXPOSITIONERRORSQUARED);
			if (IFileManager::Get().Copy(*Path(TEXT("frames.csv")), *Path(TEXT("owner/frames.csv"))) != COPY_OK) { return false; }
			if (!WriteJson(Path(TEXT("manifest.json")), Plan.ToSharedRef())) { return false; }
			if (!FSprintTrialProfile::AnalyzeSplit(Root) || !SprintTrialReport::WriteDiagnostics(Root, Root)) { return false; }
			return WriteAtomic(Path(TEXT("complete.txt")), TEXT("Completed"));
		}

		FString Role, Root, Variant, Pattern;
		FGuid TrialId;
		TSharedPtr<FJsonObject> Plan;
		TArray<FSprintInputEdge> Edges;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UNetDriver> Driver;
		TWeakObjectPtr<USprintTrialCapture> Capture;
		TWeakObjectPtr<ASprintTrialCharacter> Subject;
		FSprintTrialProfile Profile;
		FVector Foot = FVector::ZeroVector;
		double Heading = 0., Deadline = 0., StartSeconds = -1., CounterStart = 0.;
		int64 StartBytes = 0, StartPackets = 0;
		int32 CurrentPhase = INDEX_NONE, NextEdge = 0;
		bool bOwner = false, bServer = false, bReady = false, bCostSaved = false;
		bool bCaptured = false, bRoleDone = false, bFinished = false;
	};

	FSprintStandalone Standalone;
	const FDelegateHandle SprintStandaloneStartup = FCoreDelegates::GetOnPostEngineInit().AddRaw(&Standalone, &FSprintStandalone::Initialize);
}

#endif
