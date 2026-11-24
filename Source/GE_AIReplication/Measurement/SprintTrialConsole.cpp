#include "SprintTrialReport.h"

#if WITH_EDITOR && DO_ENABLE_NET_TEST

#include "RubberBandingCaptureSubsystem.h"
#include "RubberBandingSweep.h"
#include "SprintTrialCapture.h"
#include "SprintTrialCharacter.h"
#include "SprintTrialProfile.h"
#include "SprintTrialInput.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "SharpMovement/Measurement/SharpRecordingFiles.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonSerializer.h"
#include "Trace/Trace.h"

DEFINE_LOG_CATEGORY_STATIC(LogSprintTrialConsole, Log, All);

namespace
{
	constexpr const TCHAR* Usage =
		TEXT("SprintTrial.Run <SeparateRpc|MoveData> [Network] [Scenario] [Block] | CmcWalk Matrix [Block] | ProfileReport <batch-folder>");

	void Announce(const FString& Message)
	{
		UE_LOG(LogSprintTrialConsole, Display, TEXT("%s"), *Message);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.f, FColor::Cyan, Message); }
	}

	struct FOptions
	{
		FString Variant;
		FString Network = TEXT("Fixed50");
		FString Scenario = TEXT("Straight");
		int32 Block = 1;
		FIntVector Emulation = FIntVector(50, 50, 0);
		bool bProfile = false;

		bool ParseMatrix(TArray<FString> Args, const TCHAR* MatrixVariant)
		{
			Variant = MatrixVariant;
			const bool bWalk = Variant == TEXT("CmcWalk");
			Scenario = bWalk ? TEXT("Walk") : TEXT("Toggle");
			if (!bWalk && Args.Num() >= 2 && Args[Args.Num() - 2] == TEXT("Profile"))
			{
				const FString Pattern = Args.Pop();
				if (Pattern != TEXT("Toggle") && Pattern != TEXT("Hold") && Pattern != TEXT("Tap") && Pattern != TEXT("TapSweep")) { return false; }
				Scenario = Pattern;
			}
			bProfile = bWalk || (!Args.IsEmpty() && Args.Last() == TEXT("Profile"));
			if (bProfile && !bWalk) { Args.Pop(); }
			return Args.IsEmpty() || (Args.Num() == 1 && LexTryParseString(Block, *Args[0]) && Block >= 1 && Block <= 10000);
		}

		bool Parse(const TArray<FString>& Args)
		{
			if (Args.IsEmpty() || Args.Num() > 4)
			{
				return false;
			}
			if (Args[0] == TEXT("SeparateRpc")) { Variant = TEXT("SeparateRpc"); }
			else if (Args[0] == TEXT("MoveData")) { Variant = TEXT("MoveData"); }
			else { return false; }

			if (Args.IsValidIndex(1))
			{
				if (Args[1] == TEXT("Local")) { Network = TEXT("Local"); Emulation = FIntVector::ZeroValue; }
				else if (Args[1] == TEXT("Fixed50")) { Network = TEXT("Fixed50"); }
				else if (Args[1] == TEXT("Fixed100")) { Network = TEXT("Fixed100"); Emulation = FIntVector(100, 100, 0); }
				else if (Args[1] == TEXT("Jitter")) { Network = TEXT("Jitter"); Emulation = FIntVector(50, 100, 0); }
				else if (Args[1] == TEXT("Loss")) { Network = TEXT("Loss"); Emulation = FIntVector(50, 50, 2); }
				else { return false; }
			}
			if (Args.IsValidIndex(2))
			{
				if (Args[2] == TEXT("Straight")) { Scenario = TEXT("Straight"); }
				else if (Args[2] == TEXT("Turn90")) { Scenario = TEXT("Turn90"); }
				else if (Args[2] == TEXT("Toggle")) { Scenario = TEXT("Toggle"); }
				else { return false; }
			}
			return !Args.IsValidIndex(3) || (LexTryParseString(Block, *Args[3]) && Block >= 1 && Block <= 10000);
		}

	};


	FIntVector4 NetworkTotals(const APlayerController* Controller)
	{
		const UNetConnection* Connection = Controller ? Controller->GetNetConnection() : nullptr;
		return Connection ? FIntVector4(Connection->InTotalBytes, Connection->OutTotalBytes,
			Connection->InTotalPackets, Connection->OutTotalPackets) : FIntVector4(0);
	}

	struct FParticipant
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UNetDriver> Driver;
		TWeakObjectPtr<USprintTrialCapture> Capture;
		TWeakObjectPtr<ASprintTrialCharacter> Subject;
		TWeakObjectPtr<ACharacter> Original;
		TWeakObjectPtr<AActor> OriginalView;
		FPacketSimulationSettings NetworkBefore;
		bool bOpened = false;
		bool bFoundOriginal = false;
		bool bHiddenBefore = false;
		bool bCollisionBefore = false;
		bool bMoveTickBefore = false;
		bool bAlwaysRelevantBefore = false;

		bool Configure(UWorld& InWorld)
		{
			World = &InWorld;
			Driver = InWorld.GetNetDriver();
			Capture = USprintTrialCapture::Find(&InWorld);
			if (!Driver.IsValid() || !Capture.IsValid() || IsRubberBandingSweepActive(&InWorld)
				|| URubberBandingCaptureSubsystem::GetActive(&InWorld) || Capture->IsRecording())
			{
				return false;
			}
			NetworkBefore = Driver->PacketSimulationSettings;
			return true;
		}

		void SuspendOriginal(ACharacter* Character)
		{
			if (!Character || bFoundOriginal) { return; }
			Original = Character;
			bFoundOriginal = true;
			bHiddenBefore = Character->IsHidden();
			bCollisionBefore = Character->GetActorEnableCollision();
			bMoveTickBefore = Character->GetCharacterMovement()->IsComponentTickEnabled();
			bAlwaysRelevantBefore = Character->bAlwaysRelevant;
			if (Character->HasAuthority()) { Character->bAlwaysRelevant = true; }
			Character->SetActorHiddenInGame(true);
			Character->SetActorEnableCollision(false);
			Character->GetCharacterMovement()->SetComponentTickEnabled(false);
		}

		void RestoreOriginal() const
		{
			if (ACharacter* Character = Original.Get())
			{
				if (Character->HasAuthority()) { Character->bAlwaysRelevant = bAlwaysRelevantBefore; }
				Character->SetActorHiddenInGame(bHiddenBefore);
				Character->SetActorEnableCollision(bCollisionBefore);
				Character->GetCharacterMovement()->SetComponentTickEnabled(bMoveTickBefore);
			}
		}
	};

	constexpr int32 JitterSettings[] = {100, 200, 300};
	constexpr int32 LossSettings[] = {0, 10, 20};
	constexpr int32 BaseLagMs = 50;

	struct FMatrixCell
	{
		int32 Jitter;
		int32 Loss;
		int32 TapMs = INDEX_NONE;
		FString Outcome = TEXT("Pending");
		FString Detail;

		FString Name() const
		{
			const FString Network = FString::Printf(TEXT("Jitter%dLoss%d"), Jitter, Loss);
			return TapMs == INDEX_NONE ? Network : Network + FString::Printf(TEXT("-Tap%d"), TapMs);
		}
	};

	enum class ETrialStage : uint8 { Idle, Replicating, Connecting, Warmup, Running, Drain, NextCell, CellFailed };

	class FTrialLauncher
	{
	public:
		void Run(const TArray<FString>& Args, UWorld* World)
		{
			if (!Args.IsEmpty() && Args[0] == TEXT("CmcWalk"))
			{
				if (Args.Num() < 2 || Args[1] != TEXT("Matrix")) { Announce(Usage); return; }
				TArray<FString> MatrixArgs = Args;
				MatrixArgs.RemoveAt(0, 2);
				Start(MatrixArgs, World, TEXT("CmcWalk"));
				return;
			}
			if (!Args.IsEmpty() && Args[0] == TEXT("ProfileReport"))
			{
				if (Stage != ETrialStage::Idle || UE::Trace::IsTracing())
				{
					Announce(TEXT("Wait for recording and trace shutdown to complete before rebuilding a profile."));
					return;
				}
				if (Args.Num() != 2 || !Args[1].StartsWith(TEXT("20")) || !Args[1].Contains(TEXT("-SprintMatrix-"))
					|| FPaths::GetCleanFilename(Args[1]) != Args[1] || !FPaths::ValidatePath(Args[1]))
				{
					Announce(Usage);
					return;
				}
				FString Result;
				FSprintTrialProfile::AnalyzeBatch(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tasks"), Args[1]), Result, true);
				Announce(Result);
				return;
			}
			Start(Args, World, nullptr);
		}

		void Start(const TArray<FString>& Args, UWorld* World, const TCHAR* MatrixVariant)
		{
			if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Client)
			{
				Announce(TEXT("Enter the command in a running PIE client. Use Play As Client, 2 players, and Run Under One Process."));
				return;
			}
			if (Stage != ETrialStage::Idle)
			{
				Announce(TEXT("A sprint trial is already running. Enter SprintTrial.Close to cancel."));
				return;
			}
			Options = FOptions();
			if (MatrixVariant)
			{
				if (!Options.ParseMatrix(Args, MatrixVariant))
				{
					Announce(Usage);
					return;
				}
			}
			else if (!Options.Parse(Args))
			{
				Announce(Usage);
				return;
			}
			if (Options.bProfile)
			{
				const IConsoleVariable* PackedMoves = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUsePackedMovementRPCs"));
				if (!PackedMoves || PackedMoves->GetInt() == 0)
				{
					Announce(TEXT("SprintTrial profiling requires p.NetUsePackedMovementRPCs 1."));
					return;
				}
			}

			TArray<UWorld*> Servers, OtherClients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (!Candidate || Candidate == World || Candidate->WorldType != EWorldType::PIE) { continue; }
				if (Candidate->GetNetMode() == NM_DedicatedServer) { Servers.Add(Candidate); }
				if (Candidate->GetNetMode() == NM_Client) { OtherClients.Add(Candidate); }
			}
			if (Servers.Num() != 1 || OtherClients.Num() != 1)
			{
				Announce(TEXT("Use two PIE players with Play As Client and Run Under One Process."));
				return;
			}
			Owner = FParticipant();
			Observer = FParticipant();
			Server = FParticipant();
			if (!Owner.Configure(*World) || !Observer.Configure(*OtherClients[0]) || !Server.Configure(*Servers[0]))
			{
				Announce(TEXT("Wait for both PIE clients to connect and finish any active movement recording."));
				return;
			}

			APlayerController* Local = World->GetFirstPlayerController();
			APlayerController* Viewer = OtherClients[0]->GetFirstPlayerController();
			auto* LocalPawn = Local ? Cast<ACharacter>(Local->GetPawn()) : nullptr;
			const APlayerState* PlayerState = Local ? Local->PlayerState.Get() : nullptr;
			if (!LocalPawn || !Local->PlayerCameraManager || !PlayerState || !Viewer || !Viewer->GetPawn() || LocalPawn->GetLocalRole() != ROLE_AutonomousProxy
				|| !LocalPawn->GetCharacterMovement()->IsMovingOnGround() || !LocalPawn->GetVelocity().IsNearlyZero(1.f))
			{
				Announce(TEXT("Stand still on the floor in the PIE client before starting."));
				return;
			}
			APlayerController* Authority = nullptr;
			for (auto It = Servers[0]->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* Candidate = It->Get();
				if (Candidate && Candidate->PlayerState && Candidate->PlayerState->GetPlayerId() == PlayerState->GetPlayerId())
				{
					Authority = Candidate;
					break;
				}
			}
			auto* AuthorityPawn = Authority ? Cast<ACharacter>(Authority->GetPawn()) : nullptr;
			if (!AuthorityPawn || !AuthorityPawn->GetCharacterMovement()->IsMovingOnGround() || !AuthorityPawn->GetVelocity().IsNearlyZero(1.f))
			{
				Announce(TEXT("Wait for the server copy of the character to stop on the floor."));
				return;
			}

			Cells.Reset();
			CellIndex = 0;
			BatchId.Reset();
			bTookPossession = false;
			Command = Options.Variant == TEXT("CmcWalk") ? TEXT("SprintTrial.Run CmcWalk Matrix")
				: MatrixVariant ? FString(TEXT("SprintTrial.")) + MatrixVariant : TEXT("SprintTrial.Run");
			if (!Args.IsEmpty()) { Command += TEXT(" ") + FString::Join(Args, TEXT(" ")); }
			StartFoot = AuthorityPawn->GetActorLocation();
			StartFoot.Z -= AuthorityPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Camera = Local->PlayerCameraManager->GetCameraCacheView();
			Camera.Location -= LocalPawn->GetActorLocation();
			Heading = FRotator(0, MatrixVariant ? Camera.Rotation.Yaw : Local->GetControlRotation().Yaw, 0);
			if (MatrixVariant && !PrepareBatch())
			{
				Announce(TEXT("Could not create the sprint matrix output."));
				return;
			}
			FString ProfileError;
			if (Options.bProfile && !Profile.Start(BatchDirectory, ProfileError))
			{
				SaveBatch(ProfileError);
				Announce(ProfileError);
				return;
			}
			OwnerController = Local;
			ObserverController = Viewer;
			ServerController = Authority;
			OtherServerController.Reset();
			if (Viewer->PlayerState)
			{
				for (auto It = Servers[0]->GetPlayerControllerIterator(); It; ++It)
				{
					auto* Candidate = It->Get();
					if (Candidate && Candidate != Authority && Candidate->PlayerState
						&& Candidate->PlayerState->GetPlayerId() == Viewer->PlayerState->GetPlayerId())
					{
						OtherServerController = Candidate;
						break;
					}
				}
			}
			OriginalControlRotation = Local->GetControlRotation();
			OriginalServerRotation = Authority->GetControlRotation();
			Owner.OriginalView = Local->GetViewTarget();
			Observer.OriginalView = Viewer->GetViewTarget();
			Owner.SuspendOriginal(LocalPawn);
			for (TActorIterator<ACharacter> It(OtherClients[0]); It; ++It)
			{
				if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == PlayerState->GetPlayerId())
				{
					Observer.SuspendOriginal(*It);
					break;
				}
			}
			Server.SuspendOriginal(AuthorityPawn);
			SetNetwork(FPacketSimulationSettings());
			Stage = ETrialStage::NextCell;
			Deadline = FPlatformTime::Seconds();
			Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTrialLauncher::Tick));
			PreTick = FWorldDelegates::OnWorldPreActorTick.AddRaw(this, &FTrialLauncher::OnWorldTick);
			PIEEnded = FEditorDelegates::PrePIEEnded.AddRaw(this, &FTrialLauncher::OnPIEEnded);
		}

		void Cancel(UWorld* World)
		{
			if (Stage != ETrialStage::Idle && World && (World == Owner.World.Get() || World == Observer.World.Get()))
			{
				Stop(TEXT("Cancelled from a PIE client."));
			}
		}

	private:
		bool PrepareBatch()
		{
			BatchId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			BatchDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tasks"),
				FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-SprintMatrix-") + Options.Variant + TEXT("-") + BatchId.Left(8)));
			for (int32 Jitter : JitterSettings)
			{
				for (int32 Loss : LossSettings)
				{
					if (Options.Scenario == TEXT("TapSweep"))
					{
						for (int32 TapMs : {50, 100, 200, 0}) { Cells.Add({Jitter, Loss, TapMs}); }
					}
					else { Cells.Add({Jitter, Loss}); }
				}
			}
			BatchReadme = FString::Printf(TEXT("# Sprint state transition matrix\n\nCommand: %s\nCreated (UTC): %s\n\n")
				TEXT("Purpose: compare sprint transitions across nine network conditions. TapSweep runs four fresh attempts per network condition.\n")
				TEXT("batch.csv indexes planned and attempted conditions. Each condition folder contains a manifest, raw role CSVs, and a summary.\n")
				TEXT("Keep the manifests and raw CSVs with the comparison report.\n"),
				*Command, *FDateTime::UtcNow().ToIso8601());
			if (Options.bProfile)
			{
				BatchReadme += TEXT("profile.utrace opens in Unreal Insights. profile_status.csv indexes extraction status.\n")
					TEXT("Cell profile.csv contains phase and total costs, rpc_bits.csv lists RPC ranges, and frames.csv records owner frames.\n")
					TEXT("Role timing.csv files contain movement timing. owner/packed.csv and server/packed.csv contain move and response serialization.")
					TEXT(" The RPC CSV links matched records by direction and PackedSerial. Retain the trace and complete batch with reports.\n");
			}
			return IFileManager::Get().MakeDirectory(*BatchDirectory, true) && SaveBatch(TEXT("Preparing"));
		}

		bool SaveBatch(const FString& Status) const
		{
			FString Csv = TEXT("Cell,JitterMs,LossPerDirection,Outcome,Detail,TrialFolder\n");
			for (int32 Index = 0; Index < Cells.Num(); ++Index)
			{
				const FMatrixCell& Cell = Cells[Index];
				Csv += FString::Printf(TEXT("%d,%d,%d,%s,%s,%s\n"), Index + 1, Cell.Jitter, Cell.Loss,
					*Cell.Outcome, *FSharpRecordingFiles::Csv(Cell.Detail),
					*FSharpRecordingFiles::Csv(Cell.Outcome == TEXT("Pending") ? FString() : Cell.Name()));
			}
			return FFileHelper::SaveStringToFile(BatchReadme + TEXT("\nOutcome: ") + Status + TEXT("\n"),
				*FPaths::Combine(BatchDirectory, TEXT("README.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				&& FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(BatchDirectory, TEXT("batch.csv")),
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		bool StartCell()
		{
			bProfileSaved = false;
			NextEdge = 0;
			PlannedEdges.Reset();
			TrialId = FGuid::NewGuid();
			if (Options.bProfile) { Profile.ResetCell(TrialId); }
			Manifest.Reset();
			Owner.bOpened = Observer.bOpened = Server.bOpened = false;
			if (!Cells.IsEmpty())
			{
				FMatrixCell& Cell = Cells[CellIndex];
				Cell.Outcome = TEXT("Running");
				Options.Network = Cell.Name();
				Options.Emulation = FIntVector(BaseLagMs, BaseLagMs + Cell.Jitter, Cell.Loss);
				if (Cell.TapMs != INDEX_NONE) { PlannedEdges = SprintTrialInput::TapEdges(Cell.TapMs); }
				SaveBatch(TEXT("Running"));
				Announce(FString::Printf(TEXT("%s %d/%d | %s"), *Options.Variant, CellIndex + 1, Cells.Num(), *Options.Network));
			}
			Stage = ETrialStage::Replicating;
			Deadline = FPlatformTime::Seconds() + 15.;
			return PrepareOutput() && OpenRoles() && Spawn();
		}

		bool PrepareOutput()
		{
			const FString RunId = TrialId.ToString(EGuidFormats::Digits);
			const FString Map = UWorld::RemovePIEPrefix(Server.World->GetOutermost()->GetName());
			Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tasks"),
				FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-SprintPIE-") + Options.Variant + TEXT("-") + RunId.Left(8)));
			if (!Cells.IsEmpty()) { Directory = FPaths::Combine(BatchDirectory, Options.Network); }
			Readme = FString::Printf(TEXT("# Sprint PIE trial\n\nCommand: %s\nCreated (UTC): %s\n")
				TEXT("Purpose: compare same-move position errors in the open PIE level.\n\n")
				TEXT("- manifest.json describes the experiment, level, and starting pose.\n")
				TEXT("- server/, owner/ and observer/ contain movement events and connection counters.\n")
				TEXT("- summary.csv contains the measured result or failure.\n")
				TEXT("\nKeep the manifest and raw CSVs with the report.\n"),
				*Command, *FDateTime::UtcNow().ToIso8601());
			if (!IFileManager::Get().MakeDirectory(*Directory, true) || !SaveReadme(TEXT("Preparing"))) { return false; }
			Manifest = MakeShared<FJsonObject>();
			Manifest->SetStringField(TEXT("RunId"), RunId);
			Manifest->SetStringField(TEXT("Command"), Command);
			if (!Cells.IsEmpty())
			{
				Manifest->SetStringField(TEXT("MatrixId"), BatchId);
				Manifest->SetNumberField(TEXT("Cell"), CellIndex + 1);
				Manifest->SetNumberField(TEXT("PktJitterMs"), Cells[CellIndex].Jitter);
				Manifest->SetNumberField(TEXT("IncomingFixedLagMs"), BaseLagMs);
				Manifest->SetNumberField(TEXT("TapMs"), Cells[CellIndex].TapMs);
			}
			Manifest->SetStringField(TEXT("StartedUtc"), FDateTime::UtcNow().ToIso8601());
			const FString EngineVersion = FEngineVersion::Current().ToString();
			const FString TrialBuild = ANSI_TO_TCHAR(__DATE__ " " __TIME__);
			Manifest->SetStringField(TEXT("EngineVersion"), EngineVersion);
			Manifest->SetStringField(TEXT("TrialBuild"), TrialBuild);
			Manifest->SetStringField(TEXT("Map"), Map);
			Manifest->SetStringField(TEXT("StartFootLocation"), StartFoot.ToString());
			Manifest->SetNumberField(TEXT("HeadingDegrees"), Heading.Yaw);
			Manifest->SetStringField(TEXT("Variant"), Options.Variant);
			Manifest->SetStringField(TEXT("Network"), Options.Network);
			Manifest->SetStringField(TEXT("Scenario"), Options.Scenario);
			Manifest->SetStringField(TEXT("Instrumentation"), TEXT("ServerCheck"));
			Manifest->SetNumberField(TEXT("Block"), Options.Block);
			Manifest->SetNumberField(TEXT("OwnerLagMinMs"), Options.Emulation.X);
			Manifest->SetNumberField(TEXT("OwnerLagMaxMs"), Options.Emulation.Y);
			Manifest->SetNumberField(TEXT("OwnerLossPercentPerDirection"), Options.Emulation.Z);
			const FString Condition = FString::Printf(TEXT("%s|%s|%s|%g|%g|%s|%s|%d,%d,%d|%.3f,%.3f,%.3f|%.3f"), *Map,
				*Options.Scenario, *Options.Network, USprintTrialPolicy::WalkSpeed, USprintTrialPolicy::RunSpeed,
				*EngineVersion, *TrialBuild, Options.Emulation.X, Options.Emulation.Y, Options.Emulation.Z,
				StartFoot.X, StartFoot.Y, StartFoot.Z, Heading.Yaw);
			Manifest->SetStringField(TEXT("ConditionHash"), FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Condition)));
			if (Options.bProfile)
			{
				const FString WalkingCondition = Condition.Replace(*FString::Printf(TEXT("|%s|"), *Options.Scenario), TEXT("|InitialWalk|"));
				Manifest->SetStringField(TEXT("ProfileWalkCondition"), FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*WalkingCondition)));
				Profile.DescribeEnvironment(*Owner.World, *Manifest);
			}
			return SaveManifest(TEXT("Preparing"));
		}

		bool OpenRoles()
		{
			Server.bOpened = Server.Capture->Open(Directory, TEXT("server"), TrialId, Options.bProfile);
			Owner.bOpened = Owner.Capture->Open(Directory, TEXT("owner"), TrialId, Options.bProfile);
			Observer.bOpened = Observer.Capture->Open(Directory, TEXT("observer"), TrialId, Options.bProfile);
			return Server.bOpened && Owner.bOpened && Observer.bOpened;
		}

		bool Spawn()
		{
			UClass* Class = Options.Variant == TEXT("SeparateRpc") ? ASprintRpcCharacter::StaticClass() : ASprintMoveDataCharacter::StaticClass();
			if (Options.Variant == TEXT("CmcWalk")) { Class = ASprintCmcWalkCharacter::StaticClass(); }
			if (Options.Variant == TEXT("MoveDataBit")) { Class = ASprintMoveDataBitCharacter::StaticClass(); }
			FVector Location = StartFoot;
			Location.Z += Class->GetDefaultObject<ASprintTrialCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FActorSpawnParameters Parameters;
			Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
			auto* Subject = Server.World->SpawnActor<ASprintTrialCharacter>(Class, Location, Heading, Parameters);
			if (!Subject) { return false; }
			Server.Subject = Subject;
			Subject->TrialId = TrialId;
			if (!Subject->InitializePresentation(*Server.Original.Get())) { return false; }
			Forward = Heading.Vector();
			Right = FRotationMatrix(Heading).GetScaledAxis(EAxis::Y);
			return true;
		}

		void SetNetwork(const FPacketSimulationSettings& OwnerSettings)
		{
			Owner.Driver->SetPacketSimulationSettings(OwnerSettings);
			Server.Driver->SetPacketSimulationSettings(FPacketSimulationSettings());
			Observer.Driver->SetPacketSimulationSettings(FPacketSimulationSettings());
			bNetworkChanged = true;
		}

		ASprintTrialCharacter* FindReplica(const FParticipant& Role) const
		{
			for (TActorIterator<ASprintTrialCharacter> It(Role.World.Get()); It; ++It)
			{
				if (It->TrialId == TrialId) { return *It; }
			}
			return nullptr;
		}

		void ApplyTrialNetwork()
		{
			FPacketSimulationSettings Impaired;
			Impaired.PktLagMin = Impaired.PktIncomingLagMin = Options.Emulation.X;
			Impaired.PktLagMax = Impaired.PktIncomingLagMax = Options.Emulation.Y;
			Impaired.PktLoss = Impaired.PktIncomingLoss = Options.Emulation.Z;
			if (!Cells.IsEmpty())
			{
				Impaired = SprintTrialInput::OwnerEmulation(Cells[CellIndex].Jitter, Cells[CellIndex].Loss);
			}
			SetNetwork(Impaired);
		}

		void OnPIEEnded(bool bSimulating)
		{
			if (Stage != ETrialStage::Idle) { Stop(TEXT("PIE ended before the trial completed.")); }
		}

		bool Tick(float DeltaSeconds)
		{
			if (!Owner.World.IsValid() || !Observer.World.IsValid() || !Server.World.IsValid() || !OwnerController.IsValid()
				|| !ObserverController.IsValid() || !ServerController.IsValid() || !Server.Original.IsValid()
				|| !Owner.Capture.IsValid() || !Server.Capture.IsValid() || !Observer.Capture.IsValid()
				|| !Owner.Driver.IsValid() || !Observer.Driver.IsValid() || !Server.Driver.IsValid())
			{
				Stop(TEXT("A PIE participant disconnected."));
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			if (Stage == ETrialStage::NextCell)
			{
				if (Now < Deadline) { return true; }
				if (!StartCell()) { return EndTick(false, TEXT("Could not open recordings or spawn at the starting position.")); }
				return true;
			}
			if (Stage == ETrialStage::CellFailed) { return EndTick(false, PendingFailure); }
			if (!Server.Subject.IsValid()) { return EndTick(false, TEXT("The server trial character disappeared.")); }
			if (Stage == ETrialStage::Replicating)
			{
				Owner.Subject = FindReplica(Owner);
				Observer.Subject = FindReplica(Observer);
				if (Owner.Subject.IsValid() && Owner.Subject->HasTrialPresentation()
					&& Observer.Subject.IsValid() && Observer.Subject->HasTrialPresentation())
				{
					// Resolve the trial pawn on both clients before sending the possession RPC.
					if (!Cells.IsEmpty()) { Owner.Subject->TrialCamera = Camera; }
					ServerController->Possess(Server.Subject.Get());
					if (ServerController->GetPawn() != Server.Subject.Get()) { return EndTick(false, TEXT("Could not possess the trial character.")); }
					bTookPossession = true;
					Stage = ETrialStage::Connecting;
					Deadline = Now + 15.;
				}
				else if (Now >= Deadline) { return EndTick(false, TEXT("Trial pawn or its mesh and animation settings did not replicate to both clients.")); }
				return true;
			}
			if (Stage == ETrialStage::Connecting)
			{
				const auto* Local = Owner.Subject.Get();
				const auto* Remote = Observer.Subject.Get();
				const bool bLocalReady = Local && Local->GetLocalRole() == ROLE_AutonomousProxy
					&& OwnerController->AcknowledgedPawn == Local;
				const float LocalTime = bLocalReady
					? Local->GetCharacterMovement()->GetPredictionData_Client_Character()->CurrentTimeStamp : 0.f;
				const float ServerTime = Server.Subject->GetCharacterMovement()->GetPredictionData_Server_Character()->CurrentClientTimeStamp;
				if (bLocalReady && Remote && Remote->GetLocalRole() == ROLE_SimulatedProxy
					&& ServerController->AcknowledgedPawn == Server.Subject.Get()
					&& Local->GetCharacterMovement()->IsMovingOnGround() && Server.Subject->GetCharacterMovement()->IsMovingOnGround()
					&& LocalTime > 0.f && ServerTime > 0.f)
				{
					OwnerController->SetViewTarget(Owner.Subject.Get());
					if (Cells.IsEmpty()) { ObserverController->SetViewTarget(Observer.Subject.Get()); }
					ApplyTrialNetwork();
					Stage = ETrialStage::Warmup;
					Deadline = Now + 1.;
				}
				else if (Now >= Deadline)
				{
					return EndTick(false, FString::Printf(TEXT("Trial movement not ready (roles S/O/R %d/%d/%d, ack S/O %d/%d, modes S/O %d/%d, timestamps S/O %.3f/%.3f)."),
						static_cast<int32>(Server.Subject->GetLocalRole()), Local ? static_cast<int32>(Local->GetLocalRole()) : -1,
						Remote ? static_cast<int32>(Remote->GetLocalRole()) : -1,
						ServerController->AcknowledgedPawn == Server.Subject.Get(), Local && OwnerController->AcknowledgedPawn == Local,
						static_cast<int32>(Server.Subject->GetCharacterMovement()->MovementMode),
						Local ? static_cast<int32>(Local->GetCharacterMovement()->MovementMode) : -1, ServerTime, LocalTime));
				}
			}
			else if (Stage == ETrialStage::Warmup && Now >= Deadline)
			{
				for (const FParticipant* Role : {&Server, &Owner, &Observer})
				{
					if (!Role->Subject.IsValid() || !Role->Capture.IsValid())
					{
						return EndTick(false, TEXT("A PIE replica disappeared before capture."));
					}
					Role->Capture->Start();
				}
				OwnerNetStart = NetworkTotals(ServerController.Get());
				ObserverNetStart = NetworkTotals(OtherServerController.Get());
				NetworkStart = Now;
				Elapsed = 0.f;
				LastMoveTime = -1.f;
				Epoch = 0;
				PreviousPhase = INDEX_NONE;
				if (Options.bProfile) { Profile.BindEndpoints(*Owner.Driver, *Owner.Subject, *Server.Driver, *Server.Subject); }
				Stage = ETrialStage::Running;
			}
			else if (Stage == ETrialStage::Drain && Now >= Deadline)
			{
				if (!PlannedEdges.IsEmpty())
				{
					if (!bProfileSaved)
					{
						if (!Profile.SaveCell(Directory, Now, *Manifest)) { return EndTick(false, TEXT("Could not save profile boundaries.")); }
						bProfileSaved = true;
					if (!SaveNetworkSnapshot()) { return EndTick(false, TEXT("Could not save fixed-window connection counters.")); }
					}
					if (!Owner.Subject.IsValid()) { return EndTick(false, TEXT("Owner pawn disappeared during diagnostic drain.")); }
					const auto* Policy = Server.Subject->GetSprintPolicy();
					const bool bReleaseSeen = Policy->Variant != ESprintTrialVariant::SeparateRpc
						|| Policy->GetTransition() == Owner.Subject->GetSprintPolicy()->GetTransition();
					const bool bWalking = !CastChecked<USprintObservedMovement>(Server.Subject->GetCharacterMovement())->IsLastAppliedSprint();
					if ((!bReleaseSeen || !bWalking) && Now < LastReleaseAt + 10.) { return true; }
					Manifest->SetBoolField(TEXT("ReleaseObservedAfterDrain"), bReleaseSeen && bWalking);
				}
				return EndTick(true, FString());
			}
			return true;
		}

		void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaTime)
		{
			if (Stage != ETrialStage::Running && Stage != ETrialStage::Drain) { return; }
			if (World == Observer.World && Observer.Subject.IsValid() && Observer.Capture.IsValid())
			{
				Observer.Capture->Record(*Observer.Subject, TEXT("RemoteSample"));
			}
			if (World != Owner.World) { return; }
			const double RealNow = Options.bProfile ? FPlatformTime::Seconds() : 0.;
			if (Stage == ETrialStage::Drain)
			{
				if (Options.bProfile && !bProfileSaved) { Profile.RecordFrame(4, RealNow, DeltaTime); }
				return;
			}
			const int32 Phase = FMath::Min(4, FMath::FloorToInt(Elapsed / 2.f));
			if (Options.bProfile)
			{
				if (PreviousPhase != Phase) { Profile.MarkPhase(Phase, RealNow); }
				Profile.RecordFrame(Phase, RealNow, DeltaTime);
			}
			auto* Subject = Owner.Subject.Get();
			if (!Subject || !Owner.Capture.IsValid() || !OwnerController.IsValid() || OwnerController->GetPawn() != Subject)
			{
				FailCell(TEXT("Owner lost possession of the trial character."));
				return;
			}
			const float MoveTime = Subject->GetCharacterMovement()->GetPredictionData_Client_Character()->CurrentTimeStamp;
			if (MoveTime < LastMoveTime) { ++Epoch; }
			LastMoveTime = MoveTime;
			if (Elapsed >= 8.f)
			{
				Subject->TrialPhase = INDEX_NONE;
				if (auto* Policy = Subject->GetSprintPolicy()) { Policy->Request(false); }
				Stage = ETrialStage::Drain;
				Deadline = FPlatformTime::Seconds() + 1.;
				return;
			}
			if (DeltaTime > .125f)
			{
				FailCell(TEXT("Owner frame exceeded 125 ms."));
				return;
			}
			if (!Subject->GetCharacterMovement()->IsMovingOnGround())
			{
				FailCell(TEXT("The trial character left the floor."));
				return;
			}
			Subject->TrialPhase = Phase;
			auto* Policy = Subject->GetSprintPolicy();
			const float TransitionTime = Elapsed - 2.f;
			bool bRun = Policy && Phase == 1;
			if (bRun && Options.Scenario == TEXT("Toggle")) { bRun = FMath::FloorToInt(TransitionTime / .25f) % 2 == 0; }
			if (bRun && Options.Scenario == TEXT("Tap"))
			{
				if (PreviousPhase != Phase) { TapStart = Elapsed; }
				bRun = Elapsed - TapStart < .1f;
			}
			if (!PlannedEdges.IsEmpty())
			{
				while (PlannedEdges.IsValidIndex(NextEdge) && Elapsed >= PlannedEdges[NextEdge].Seconds)
				{
					const auto& Edge = PlannedEdges[NextEdge++];
					Owner.Capture->Record(*Subject, TEXT("PlannedInput"), MoveTime, Epoch, FVector::ZeroVector,
						NextEdge, Phase, Edge.Seconds, Edge.bPressed);
				}
				bRun = NextEdge && PlannedEdges[NextEdge - 1].bPressed;
			}
			const bool bTransition = Policy && bRun != Policy->IsRequested();
			if (bTransition && !bRun) { LastReleaseAt = RealNow; }
			if (Policy) { Policy->Request(bRun); }
			if (PreviousPhase != Phase || bTransition)
			{
				Owner.Capture->Record(*Subject, TEXT("Input"), MoveTime, Epoch, FVector::ZeroVector, bRun ? 1. : 0., Phase);
			}
			if (Phase != 3)
			{
				Subject->AddMovementInput(Options.Scenario == TEXT("Turn90") && Elapsed >= 3.f ? Right : Forward);
			}
			const TCHAR* Step = Phase == 3 ? TEXT("Stop") : bRun ? TEXT("Sprint") : TEXT("Walk");
			GEngine->AddOnScreenDebugMessage(0x53505254, .5f, FColor::Cyan,
				FString::Printf(TEXT("Sprint %s | %s | %s | %.1f / 8 s | owner %.0f cm/s"), *Options.Variant, *Options.Network, Step,
					Elapsed, Subject->GetVelocity().Size2D()));
			PreviousPhase = Phase;
			Elapsed += DeltaTime;
		}

		void FailCell(const FString& Message)
		{
			PendingFailure = Message;
			Stage = ETrialStage::CellFailed;
		}

		bool EndTick(bool bSuccess, const FString& Message)
		{
			FinishCell(bSuccess, Message);
			if (!Cells.IsEmpty() && CellIndex + 1 < Cells.Num())
			{
				SetNetwork(FPacketSimulationSettings());
				ReleaseSubject();
				++CellIndex;
				Stage = ETrialStage::NextCell;
				Deadline = FPlatformTime::Seconds() + .5;
				return true;
			}
			CloseBatch(Cells.IsEmpty() ? FString() : TEXT("All conditions attempted."));
			return false;
		}

		bool SaveNetworkSnapshot()
		{
			const FIntVector4 OwnerCost = NetworkTotals(ServerController.Get()) - OwnerNetStart;
			const FIntVector4 ObserverCost = NetworkTotals(OtherServerController.Get()) - ObserverNetStart;
			const double Seconds = FPlatformTime::Seconds() - NetworkStart;
			const FString Csv = FString::Printf(TEXT("Connection,Seconds,ReceivedBytes,SentBytes,ReceivedPackets,SentPackets\n")
				TEXT("owner,%.6f,%d,%d,%d,%d\nobserver,%.6f,%d,%d,%d,%d\n"),
				Seconds, OwnerCost.X, OwnerCost.Y, OwnerCost.Z, OwnerCost.W,
				Seconds, ObserverCost.X, ObserverCost.Y, ObserverCost.Z, ObserverCost.W);
			return Server.Capture->Signal(TEXT("network.csv"), Csv);
		}

		FString SaveRaw()
		{
			if (!bProfileSaved && !SaveNetworkSnapshot()) { return TEXT("Could not write connection counters."); }
			const auto* PlayerState = ServerController->PlayerState.Get();
			const float Ping = PlayerState ? PlayerState->GetPingInMilliseconds() : -1.f;
			Server.Capture->Record(*Server.Subject, TEXT("OwnerPingMs"), 0.f, 0, FVector::ZeroVector, Ping);
			bool bFilesComplete = true;
			for (const FParticipant* Role : {&Server, &Owner, &Observer})
			{
				bFilesComplete &= Role->Capture->Finish();
			}
			return bFilesComplete ? FString() : TEXT("Could not finish writing the role recordings.");
		}

		void FinishCell(bool bSuccess, FString Message)
		{
			if (Options.bProfile && Manifest && !bProfileSaved && !Profile.SaveCell(Directory, FPlatformTime::Seconds(), *Manifest) && bSuccess)
			{
				bSuccess = false;
				Message = TEXT("Could not save owner frames.");
			}
			if (bSuccess)
			{
				Message = SaveRaw();
				bSuccess = Message.IsEmpty() && SprintTrialReport::WriteSummary(Directory, *Manifest, Message);
			}
			if (!bSuccess)
			{
				for (const FParticipant* Role : {&Server, &Owner, &Observer})
				{
					if (Role->bOpened && Role->Capture.IsValid()) { Role->Capture->Fail(Message); }
				}
				if (Manifest) { SprintTrialReport::WriteFailure(Directory, *Manifest, Message); }
			}
			const FString Outcome = bSuccess ? TEXT("Completed") : TEXT("Failed");
			if (Manifest)
			{
				Manifest->SetStringField(TEXT("FinishedUtc"), FDateTime::UtcNow().ToIso8601());
				Manifest->SetStringField(TEXT("Result"), Message);
				if (Server.Subject.IsValid())
				{
					Manifest->SetStringField(TEXT("MovementClass"), Server.Subject->GetCharacterMovement()->GetClass()->GetName());
				}
				SaveManifest(Outcome);
				SaveReadme(Outcome + TEXT(" - ") + Message);
				Announce(FString::Printf(TEXT("%s | %s\nResults: %s"), *Outcome, *Message, *Directory));
			}
			if (!Cells.IsEmpty())
			{
				Cells[CellIndex].Outcome = Outcome;
				Cells[CellIndex].Detail = Message;
				SaveBatch(TEXT("Running"));
			}
		}

		void Stop(const FString& Reason)
		{
			if (Stage != ETrialStage::NextCell) { FinishCell(false, Reason); }
			CloseBatch(Reason);
		}

		void CloseBatch(const FString& Status)
		{
			Stage = ETrialStage::Idle;
			if (Ticker.IsValid()) { FTSTicker::RemoveTicker(Ticker); Ticker.Reset(); }
			FWorldDelegates::OnWorldPreActorTick.Remove(PreTick);
			PreTick.Reset();
			FEditorDelegates::PrePIEEnded.Remove(PIEEnded);
			PIEEnded.Reset();
			RestorePIE();
			if (Options.bProfile) { Profile.Stop(); }
			if (!Cells.IsEmpty())
			{
				int32 Completed = 0;
				for (const FMatrixCell& Cell : Cells) { Completed += Cell.Outcome == TEXT("Completed"); }
				const FString Summary = FString::Printf(TEXT("%s %d/%d recorded. Results: %s"), *Status, Completed, Cells.Num(), *BatchDirectory);
				if (!SaveBatch(Summary)) { Announce(TEXT("Could not finish writing the matrix index.")); }
				Announce(Summary);
				if (Options.bProfile)
				{
					const FString DirectoryToAnalyze = BatchDirectory;
					const double Timeout = FPlatformTime::Seconds() + 5.;
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([DirectoryToAnalyze, Timeout](float)
					{
						if (UE::Trace::IsTracing() && FPlatformTime::Seconds() < Timeout) { return true; }
						if (UE::Trace::IsTracing()) { Announce(TEXT("Trace shutdown still pending. Use SprintTrial.Run ProfileReport when it closes.")); return false; }
						FString Result;
						FSprintTrialProfile::AnalyzeBatch(DirectoryToAnalyze, Result);
						Announce(Result);
						return false;
					}));
				}
			}
		}

		void ReleaseSubject()
		{
			if (ASprintTrialCharacter* Subject = Server.Subject.Get())
			{
				if (ServerController.IsValid() && ServerController->GetPawn() == Subject)
				{
					ServerController->UnPossess();
				}
				Subject->Destroy();
			}
			Server.Subject.Reset();
			Owner.Subject.Reset();
			Observer.Subject.Reset();
		}

		void RestorePIE()
		{
			for (const FParticipant* Role : {&Server, &Owner, &Observer})
			{
				if (bNetworkChanged && Role->Driver.IsValid()) { Role->Driver->SetPacketSimulationSettings(Role->NetworkBefore); }
				Role->RestoreOriginal();
			}
			bNetworkChanged = false;
			if (ServerController.IsValid() && Server.Original.IsValid() && bTookPossession &&
				(!ServerController->GetPawn() || ServerController->GetPawn() == Server.Subject.Get()))
			{
				ServerController->Possess(Server.Original.Get());
				ServerController->SetControlRotation(OriginalServerRotation);
			}
			if (OwnerController.IsValid() && bTookPossession)
			{
				OwnerController->SetViewTarget(Owner.OriginalView.IsValid() ? Owner.OriginalView.Get() : Owner.Original.Get());
				OwnerController->SetControlRotation(OriginalControlRotation);
			}
			if (ObserverController.IsValid() && ObserverController->GetViewTarget() == Observer.Subject.Get() && Observer.OriginalView.IsValid())
			{
				ObserverController->SetViewTarget(Observer.OriginalView.Get());
			}
			ReleaseSubject();
		}

		bool SaveReadme(const FString& Outcome) const
		{
			return FFileHelper::SaveStringToFile(Readme + TEXT("\nOutcome: ") + Outcome + TEXT("\n"),
				*FPaths::Combine(Directory, TEXT("README.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		bool SaveManifest(const FString& Outcome) const
		{
			Manifest->SetStringField(TEXT("Outcome"), Outcome);
			FString Json;
			return FJsonSerializer::Serialize(Manifest.ToSharedRef(), TJsonWriterFactory<>::Create(&Json))
				&& FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Directory, TEXT("manifest.json")),
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FOptions Options;
		FSprintTrialProfile Profile;
		TArray<FMatrixCell> Cells;
		int32 CellIndex = 0;
		FString BatchId, BatchDirectory, BatchReadme, Command, PendingFailure;
		FMinimalViewInfo Camera;
		FVector StartFoot;
		FRotator Heading;
		bool bTookPossession = false;
		ETrialStage Stage = ETrialStage::Idle;
		FParticipant Server, Owner, Observer;
		TWeakObjectPtr<APlayerController> ServerController, OwnerController, ObserverController, OtherServerController;
		FRotator OriginalControlRotation, OriginalServerRotation;
		FVector Forward, Right;
		FIntVector4 OwnerNetStart, ObserverNetStart;
		TSharedPtr<FJsonObject> Manifest;
		FGuid TrialId;
		FString Directory, Readme;
		FTSTicker::FDelegateHandle Ticker;
		FDelegateHandle PreTick, PIEEnded;
		double Deadline = 0., NetworkStart = 0.;
		float Elapsed = 0.f, LastMoveTime = -1.f, TapStart = 0.f;
		int32 Epoch = 0, PreviousPhase = INDEX_NONE;
		bool bNetworkChanged = false;
		bool bProfileSaved = false;
		TArray<FSprintInputEdge> PlannedEdges;
		int32 NextEdge = 0;
		double LastReleaseAt = 0.;
	};

	FTrialLauncher Launcher;
	FAutoConsoleCommandWithWorldAndArgs RunCommand(TEXT("SprintTrial.Run"), Usage,
		FConsoleCommandWithWorldAndArgsDelegate::CreateRaw(&Launcher, &FTrialLauncher::Run));
	FAutoConsoleCommandWithWorldAndArgs RpcCommand(TEXT("SprintTrial.SeparateRpc"), TEXT("SprintTrial.SeparateRpc [Block] [Profile [Toggle|Hold|Tap|TapSweep]]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Launcher.Start(Args, World, TEXT("SeparateRpc"));
		}));
	FAutoConsoleCommandWithWorldAndArgs MoveDataCommand(TEXT("SprintTrial.MoveData"), TEXT("SprintTrial.MoveData [Block] [Profile [Toggle|Hold|Tap|TapSweep]]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Launcher.Start(Args, World, TEXT("MoveData"));
		}));
	FAutoConsoleCommandWithWorldAndArgs MoveDataBitCommand(TEXT("SprintTrial.MoveDataBit"), TEXT("SprintTrial.MoveDataBit [Block] [Profile [Toggle|Hold|Tap|TapSweep]]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TArray<FString> ProfileArgs = Args;
			if (ProfileArgs.IsEmpty() || (ProfileArgs.Num() == 1 && ProfileArgs[0] != TEXT("Profile")))
			{
				ProfileArgs.Add(TEXT("Profile"));
			}
			Launcher.Start(ProfileArgs, World, TEXT("MoveDataBit"));
		}));
	FAutoConsoleCommandWithWorld CloseCommand(TEXT("SprintTrial.Close"), TEXT("Cancel the active PIE sprint trial."),
		FConsoleCommandWithWorldDelegate::CreateRaw(&Launcher, &FTrialLauncher::Cancel));
}

#endif
