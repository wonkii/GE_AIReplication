// Fill out your copyright notice in the Description page of Project Settings.


#include "SharpMovement/Net/SharpUpdatePolicy.h"

#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharpUpdatePolicy, Log, All);

bool USharpUpdatePolicy::InitializePolicy(
    ESharpExperimentMode InMode,
    USharpExperimentSettings* InSettings)
{
    AActor* OwnerActor = GetOwner();

    if (!IsValid(OwnerActor))
    {
        UE_LOG(LogSharpUpdatePolicy, Error,
            TEXT("[M5][Policy] InitRejected Reason=MissingOwner"));
        return false;
    }

    // Policy는 서버에서만 적용한다.
    if (!OwnerActor->HasAuthority())
    {
        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Policy] InitRejected Reason=NotAuthority Actor=%s"),
            *GetNameSafe(OwnerActor));
        return false;
    }

    // 실제로 구현된 모드만 허용한다.
    if (InMode != ESharpExperimentMode::Default &&
        InMode != ESharpExperimentMode::Adaptive &&
        InMode != ESharpExperimentMode::AlwaysHigh &&
        InMode != ESharpExperimentMode::SingleShot &&
        InMode != ESharpExperimentMode::Burst &&
        InMode != ESharpExperimentMode::EventOnly &&
        InMode != ESharpExperimentMode::EventPredictor)
    {
        const FString RequestedMode =
            StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(
                static_cast<int64>(InMode));

        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Policy] InitRejected Reason=ModeNotImplemented "
                "Actor=%s Mode=%s"),
            *GetNameSafe(OwnerActor),
            *RequestedMode);
        return false;
    }

    // Default는 Settings가 없어도 엔진의 기존 상태를 유지한다.
    // Adaptive, AlwaysHigh, SingleShot, Burst, EventOnly, EventPredictor는 Settings가 필요하다.
    if (!InSettings && InMode != ESharpExperimentMode::Default)
    {
        const FString ModeName =
            StaticEnum<ESharpExperimentMode>()->GetNameStringByValue(
                static_cast<int64>(InMode));

        UE_LOG(LogSharpUpdatePolicy, Error,
            TEXT("[M5][Policy] InitRejected Reason=SettingsMissing "
                "Actor=%s Mode=%s"),
            *GetNameSafe(OwnerActor),
            *ModeName);
        return false;
    }

    // Adaptive는 Unreal 내장 기능을 사용한다.
    // CVar는 PIE 프로세스 전체에 영향을 주므로 여기서 변경하지 않는다.
    const bool bAdaptiveGlobalEnabled =
        UNetDriver::IsAdaptiveNetUpdateFrequencyEnabled();

    if (InMode == ESharpExperimentMode::Adaptive &&
        !bAdaptiveGlobalEnabled)
    {
        UE_LOG(LogSharpUpdatePolicy, Error,
            TEXT("[M5][Policy] InitRejected Reason=AdaptiveGlobalDisabled "
                "Actor=%s RequiredCVar=net.UseAdaptiveNetUpdateFrequency=1"),
            *GetNameSafe(OwnerActor));
        return false;
    }

    // Adaptive의 최소 빈도는 해당 Actor의 최대 빈도를 넘을 수 없다.
    // 최대 NetUpdateFrequency 자체는 기존 설정을 보존한다.
    if (InMode == ESharpExperimentMode::Adaptive &&
        InSettings->NormalNetUpdateFrequency > OwnerActor->GetNetUpdateFrequency())
    {
        UE_LOG(LogSharpUpdatePolicy, Error,
            TEXT("[M5][Policy] InitRejected Reason=AdaptiveMinExceedsMax "
                "Actor=%s RequestedMinHz=%.1f ActorMaxHz=%.1f"),
            *GetNameSafe(OwnerActor),
            InSettings->NormalNetUpdateFrequency,
            OwnerActor->GetNetUpdateFrequency());
        return false;
    }

    // Burst는 High 값이 실제 기준 빈도보다 높은 경우에만 유효하다.
    // 현재 BP 기본값 100Hz와 Settings High 60Hz 조합은 역효과이므로 거부한다.
    // Policy가 평상시 빈도를 몰래 10Hz로 변경하지 않도록 한다.
    if (InMode == ESharpExperimentMode::Burst)
    {
        const float BaselineHz = OwnerActor->GetNetUpdateFrequency();
        const float HighHz = InSettings->HighNetUpdateFrequency;
        if (HighHz <= BaselineHz)
        {
            UE_LOG(LogSharpUpdatePolicy, Error,
                TEXT("[M5][Policy] InitRejected Reason=BurstHighNotAboveBaseline "
                    "Actor=%s BaselineHz=%.1f ConfiguredHighHz=%.1f "
                    "Action=NoChange"),
                *GetNameSafe(OwnerActor),
                BaselineHz,
                HighHz);
            return false;
        }

        if (!FMath::IsFinite(BurstDurationSeconds) ||
            BurstDurationSeconds <= 0.0f)
        {
            UE_LOG(LogSharpUpdatePolicy, Error,
                TEXT("[M5][Policy] InitRejected Reason=InvalidBurstDuration "
                    "Actor=%s DurationSeconds=%.3f"),
                *GetNameSafe(OwnerActor),
                BurstDurationSeconds);
            return false;
        }
    }

    if (InSettings)
    {
        FString InvalidReason;
        if (!InSettings->IsValidConfiguration(InvalidReason))
        {
            UE_LOG(LogSharpUpdatePolicy, Error,
                TEXT("[M5][Policy] InitRejected Reason=InvalidSettings "
                    "Actor=%s Details=%s"),
                *GetNameSafe(OwnerActor),
                *InvalidReason);
            return false;
        }
    }
    else
    {
        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Policy] SettingsMissing Actor=%s "
                "Mode=Default (no replication settings will change)"),
            *GetNameSafe(OwnerActor));
    }

    // 재초기화라면 현재 모드가 바꾼 값을 먼저 원상복구한다.
    if (bPolicyInitialized)
    {
        RestorePolicyState();
    }

    ActiveMode = InMode;
    CachedSettings = InSettings;
    bPolicyInitialized = true;
    SavedNetUpdateFrequencyHz = 0.0f;
    bHasSavedNetUpdateFrequency = false;
    SavedMinNetUpdateFrequencyHz = 0.0f;
    bHasSavedMinNetUpdateFrequency = false;
    LastHandledSequenceId = 0;
    bBurstActive = false;
    BurstTimerHandle.Invalidate();

    const float PreviousFrequencyHz = OwnerActor->GetNetUpdateFrequency();

    // Default에서는 SetNetUpdateFrequency / SetMinNetUpdateFrequency / 
    // ForceNetUpdate를 모두 호출하지 않는다.
    if (InMode == ESharpExperimentMode::Default)
    {
        if (bAdaptiveGlobalEnabled)
        {
            UE_LOG(LogSharpUpdatePolicy, Warning,
                TEXT("[M5][Policy] GlobalAdaptiveContamination "
                    "Actor=%s Mode=Default "
                    "RequiredCVar=net.UseAdaptiveNetUpdateFrequency=0"),
                *GetNameSafe(OwnerActor));
        }

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][Policy] Initialized Actor=%s Mode=Default "
                "Settings=%s NetUpdateHz=%.1f Action=NoChange"),
            *GetNameSafe(OwnerActor),
            *GetNameSafe(InSettings),
            PreviousFrequencyHz);
        return true;
    }

    // SingleShot: 평상시 NetUpdateFrequency 및 CMC 복제는 건드리지 않는다.
    // 실제 Detector 사건을 전달받기 전까지는 ForceNetUpdate를 호출하지 않는다.
    if (InMode == ESharpExperimentMode::SingleShot)
    {
        if (bAdaptiveGlobalEnabled)
        {
            UE_LOG(LogSharpUpdatePolicy, Warning,
                TEXT("[M5][Policy] GlobalAdaptiveContamination "
                    "Actor=%s Mode=SingleShot "
                    "RequiredCVar=net.UseAdaptiveNetUpdateFrequency=0"),
                *GetNameSafe(OwnerActor));
        }

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][Policy] Initialized Actor=%s Mode=SingleShot "
                "Settings=%s NetUpdateHz=%.1f "
                "Action=AwaitDetectorEvent NoChange"),
            *GetNameSafe(OwnerActor),
            *GetNameSafe(InSettings),
            PreviousFrequencyHz);
        return true;
    }

    // Burst는 이벤트가 오기 전까지 기존 빈도에 손대지 않는다.
    // 감지 이벤트가 아직 없어 ForceNetUpdate나 타이머도 시작하지 않는다.
    if (InMode == ESharpExperimentMode::Burst)
    {
        if (bAdaptiveGlobalEnabled)
        {
            UE_LOG(LogSharpUpdatePolicy, Warning,
                TEXT("[M5][Policy] GlobalAdaptiveContamination "
                    "Actor=%s Mode=Burst "
                    "RequiredCVar=net.UseAdaptiveNetUpdateFrequency=0"),
                *GetNameSafe(OwnerActor));
        }

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][Policy] Initialized Actor=%s Mode=Burst "
                "Settings=%s BaselineNetUpdateHz=%.1f "
                "HighNetUpdateHz=%.1f BurstDurationSeconds=%.3f "
                "Action=AwaitDetectorEvent NoChange"),
            *GetNameSafe(OwnerActor),
            *GetNameSafe(InSettings),
            PreviousFrequencyHz,
            InSettings->HighNetUpdateFrequency,
            BurstDurationSeconds);
        return true;
    }

    // EventOnly / EventPredictor: 서버의 동일한 Snapshot 게시 경로를 사용한다.
    // 이 시점에는 NetUpdateFrequency나 CMC를 변경하지 않는다.
    // EventPredictor의 실제 클라이언트 Mesh 예측은 구현하지 않는다.
    if (InMode == ESharpExperimentMode::EventOnly ||
        InMode == ESharpExperimentMode::EventPredictor)
    {
        const FString ModeName = StaticEnum<ESharpExperimentMode>()
            ->GetNameStringByValue(static_cast<int64>(InMode));

        if (bAdaptiveGlobalEnabled)
        {
            UE_LOG(LogSharpUpdatePolicy, Warning,
                TEXT("[M5][Policy] GlobalAdaptiveContamination "
                    "Actor=%s Mode=%s "
                    "RequiredCVar=net.UseAdaptiveNetUpdateFrequency=0"),
                *GetNameSafe(OwnerActor), *ModeName);
        }

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][Policy] Initialized Actor=%s Mode=%s "
                "Settings=%s NetUpdateHz=%.1f "
                "Action=AwaitDetectorSnapshot NoChange"),
            *GetNameSafe(OwnerActor),
            *ModeName,
            *GetNameSafe(InSettings),
            PreviousFrequencyHz);
        return true;
    }

    // Adaptive: UE 내장 적응형 빈도 사용.
    // 현재 Actor의 최대 빈도는 유지하고 Settings의 Normal 값을 최소 빈도로 사용.
    if (InMode == ESharpExperimentMode::Adaptive)
    {
        SavedMinNetUpdateFrequencyHz = OwnerActor->GetMinNetUpdateFrequency();
        bHasSavedMinNetUpdateFrequency = true;

        OwnerActor->SetMinNetUpdateFrequency(
            InSettings->NormalNetUpdateFrequency);

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][Policy] Initialized Actor=%s Mode=Adaptive "
                "Settings=%s AdaptiveGlobalEnabled=%d "
                "MaxNetUpdateHz=%.1f PreviousMinNetUpdateHz=%.1f "
                "AppliedMinNetUpdateHz=%.1f "
                "Action=SetMinNetUpdateFrequency"),
            *GetNameSafe(OwnerActor),
            *GetNameSafe(InSettings),
            bAdaptiveGlobalEnabled ? 1 : 0,
            OwnerActor->GetNetUpdateFrequency(),
            SavedMinNetUpdateFrequencyHz,
            OwnerActor->GetMinNetUpdateFrequency());
        return true;
    }

    // AlwaysHigh도 글로벌 Adaptive가 켜져 있으면 혼입될 수 있으므로 경고한다.
    if (bAdaptiveGlobalEnabled)
    {
        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Policy] GlobalAdaptiveContamination "
                "Actor=%s Mode=AlwaysHigh "
                "RequiredCVar=net.UseAdaptiveNetUpdateFrequency=0"),
            *GetNameSafe(OwnerActor));
    }

    // AlwaysHigh: 실험 중 설정된 High 값을 계속 사용한다.
    // 이는 '설정값'이지 실제 초당 전송 패킷 수를 뜻하지 않는다.
    const float RequestedHighHz = InSettings->HighNetUpdateFrequency;

    // 추후 원상복구에 사용할 실제 진입 직전의 빈도를 보관한다.
    SavedNetUpdateFrequencyHz = PreviousFrequencyHz;
    bHasSavedNetUpdateFrequency = true;

    OwnerActor->SetNetUpdateFrequency(RequestedHighHz);

    const float AppliedFrequencyHz = OwnerActor->GetNetUpdateFrequency();
    const TCHAR* RelativeToPrevious =
        RequestedHighHz < PreviousFrequencyHz ? TEXT("Lower") :
        RequestedHighHz > PreviousFrequencyHz ? TEXT("Higher") :
        TEXT("Equal");

    UE_LOG(LogSharpUpdatePolicy, Display,
        TEXT("[M5][Policy] Initialized Actor=%s Mode=AlwaysHigh "
            "Settings=%s PreviousNetUpdateHz=%.1f "
            "RequestedHighHz=%.1f AppliedNetUpdateHz=%.1f "
            "RelativeToPrevious=%s Action=SetNetUpdateFrequency"),
        *GetNameSafe(OwnerActor),
        *GetNameSafe(InSettings),
        PreviousFrequencyHz,
        RequestedHighHz,
        AppliedFrequencyHz,
        RelativeToPrevious);

    return true;
}

bool USharpUpdatePolicy::HandleSharpMovementEvent(
    uint32 InSequenceId,
    double InServerEventTimeSeconds)
{
    AActor* OwnerActor = GetOwner();

    if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority() ||
        !bPolicyInitialized ||
        (ActiveMode != ESharpExperimentMode::SingleShot &&
            ActiveMode != ESharpExperimentMode::Burst))
    {
        // 이 오버로드는 SingleShot/Burst 전용이다.
        // EventOnly / EventPredictor는 사후 상태 전체를 담은 Snapshot이 필요하다.
        return false;
    }

    // 실제 Detector가 발급할 SequenceId는 Actor별 단조 증가(0은 미발급).
    // 미래의 wrap-around 처리 계약은 아직 없다.
    if (InSequenceId == 0 || InSequenceId <= LastHandledSequenceId ||
        !FMath::IsFinite(InServerEventTimeSeconds) ||
        InServerEventTimeSeconds < 0.0)
    {
        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Event] EventRejected Actor=%s "
                "SequenceId=%u LastHandledSequenceId=%u "
                "EventServerTime=%.3f Reason=InvalidOrDuplicate"),
            *GetNameSafe(OwnerActor),
            InSequenceId,
            LastHandledSequenceId,
            InServerEventTimeSeconds);
        return false;
    }

    UWorld* World = OwnerActor->GetWorld();
    if (!World)
    {
        return false;
    }

    const double RequestServerTime = World->GetTimeSeconds();

    if (ActiveMode == ESharpExperimentMode::SingleShot)
    {
        LastHandledSequenceId = InSequenceId;
        OwnerActor->ForceNetUpdate();

        UE_LOG(LogSharpUpdatePolicy, Display,
            TEXT("[M5][SingleShot] Requested Actor=%s "
                "SequenceId=%u EventServerTime=%.3f "
                "RequestServerTime=%.3f NetUpdateHz=%.1f "
                "Action=ForceNetUpdate"),
            *GetNameSafe(OwnerActor),
            InSequenceId,
            InServerEventTimeSeconds,
            RequestServerTime,
            OwnerActor->GetNetUpdateFrequency());
        return true;
    }

    // Burst: 사건 직전 정상 빈도는 최초 진입 시에만 저장한다.
    // 연속 사건은 High 상태를 유지하며 종료 타이머를 다시 시작한다.
    if (!IsValid(CachedSettings.Get()))
    {
        return false;
    }

    const float HighHz = CachedSettings->HighNetUpdateFrequency;
    const float BaselineHz = bBurstActive
        ? SavedNetUpdateFrequencyHz
        : OwnerActor->GetNetUpdateFrequency();

    // 기존 100Hz -> High 60Hz처럼 역으로 내려가는 정책을 금지한다.
    if (!FMath::IsFinite(HighHz) || HighHz <= BaselineHz)
    {
        UE_LOG(LogSharpUpdatePolicy, Error,
            TEXT("[M5][Burst] EventRejected Actor=%s "
                "SequenceId=%u Reason=HighNotAboveBaseline "
                "BaselineHz=%.1f HighHz=%.1f"),
            *GetNameSafe(OwnerActor),
            InSequenceId,
            BaselineHz,
            HighHz);
        return false;
    }

    const bool bExtendedExistingBurst = bBurstActive;
    if (!bBurstActive)
    {
        SavedNetUpdateFrequencyHz = BaselineHz;
        bHasSavedNetUpdateFrequency = true;
        OwnerActor->SetNetUpdateFrequency(HighHz);
        bBurstActive = true;
    }

    // 이벤트 중복 억제와 Update 요청은 실제 Detector 연동 후에 검증한다.
    LastHandledSequenceId = InSequenceId;
    OwnerActor->ForceNetUpdate();
    World->GetTimerManager().SetTimer(
        BurstTimerHandle,
        this,
        &USharpUpdatePolicy::FinishBurst,
        BurstDurationSeconds,
        false);

    UE_LOG(LogSharpUpdatePolicy, Display,
        TEXT("[M5][Burst] Requested Actor=%s SequenceId=%u "
            "EventServerTime=%.3f RequestServerTime=%.3f "
            "BaselineHz=%.1f AppliedHighHz=%.1f "
            "DurationSeconds=%.3f Extended=%d "
            "Action=ForceNetUpdateAndStartOrExtend"),
        *GetNameSafe(OwnerActor),
        InSequenceId,
        InServerEventTimeSeconds,
        RequestServerTime,
        BaselineHz,
        OwnerActor->GetNetUpdateFrequency(),
        BurstDurationSeconds,
        bExtendedExistingBurst ? 1 : 0);

    return true;
}

void USharpUpdatePolicy::FinishBurst()
{
    AActor* OwnerActor = GetOwner();
    if (!bBurstActive || !bHasSavedNetUpdateFrequency ||
        !IsValid(OwnerActor) || !OwnerActor->HasAuthority())
    {
        return;
    }

    const float BeforeRestoreHz = OwnerActor->GetNetUpdateFrequency();
    const float OriginalHz = SavedNetUpdateFrequencyHz;
    OwnerActor->SetNetUpdateFrequency(OriginalHz);

    bBurstActive = false;
    bHasSavedNetUpdateFrequency = false;
    SavedNetUpdateFrequencyHz = 0.0f;
    BurstTimerHandle.Invalidate();

    UE_LOG(LogSharpUpdatePolicy, Display,
        TEXT("[M5][Burst] Finished Actor=%s "
            "PreviousHighHz=%.1f RestoredNetUpdateHz=%.1f "
            "Action=RestoreOriginal"),
        *GetNameSafe(OwnerActor),
        BeforeRestoreHz,
        OwnerActor->GetNetUpdateFrequency());
}

void USharpUpdatePolicy::RestorePolicyState()
{
    AActor* OwnerActor = GetOwner();

    if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority())
    {
        UE_LOG(LogSharpUpdatePolicy, Warning,
            TEXT("[M5][Policy] RestoreRejected Reason=MissingOwnerOrAuthority"));
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BurstTimerHandle);
    }
    BurstTimerHandle.Invalidate();

    // 실제로 바꾼 Actor 빈도만 복원한다.
    // 프로세스 전역 Adaptive CVar는 이 컴포넌트가 변경하지 않는다.
    const bool bRestoredFrequency = bHasSavedNetUpdateFrequency;
    if (bRestoredFrequency)
    {
        OwnerActor->SetNetUpdateFrequency(SavedNetUpdateFrequencyHz);
    }

    const bool bRestoredMinFrequency = bHasSavedMinNetUpdateFrequency;
    if (bRestoredMinFrequency)
    {
        OwnerActor->SetMinNetUpdateFrequency(SavedMinNetUpdateFrequencyHz);
    }

    const float CurrentFrequencyHz = OwnerActor->GetNetUpdateFrequency();
    const float CurrentMinFrequencyHz = OwnerActor->GetMinNetUpdateFrequency();

    ActiveMode = ESharpExperimentMode::Default;
    CachedSettings = nullptr;
    bPolicyInitialized = false;
    SavedNetUpdateFrequencyHz = 0.0f;
    bHasSavedNetUpdateFrequency = false;
    SavedMinNetUpdateFrequencyHz = 0.0f;
    bHasSavedMinNetUpdateFrequency = false;
    LastHandledSequenceId = 0;
    bBurstActive = false;

    UE_LOG(LogSharpUpdatePolicy, Display,
        TEXT("[M5][Policy] Restored Actor=%s NetUpdateHz=%.1f "
            "MinNetUpdateHz=%.1f FrequencyRestored=%d "
            "MinFrequencyRestored=%d"),
        *GetNameSafe(OwnerActor),
        CurrentFrequencyHz,
        CurrentMinFrequencyHz,
        bRestoredFrequency ? 1 : 0,
        bRestoredMinFrequency ? 1 : 0);
}

// Actor / Component 수명 종료 시 활성 Burst 타이머를 제거하고
// AlwaysHigh, Adaptive, Burst가 변경한 Actor의 값을 원상복구한다.
// 전역 net.UseAdaptiveNetUpdateFrequency CVar는 변경하지 않는다.
void USharpUpdatePolicy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bPolicyInitialized)
    {
        AActor* OwnerActor = GetOwner();
        if (IsValid(OwnerActor) && OwnerActor->HasAuthority())
        {
            RestorePolicyState();
        }
        else
        {
            // 소유 Actor가 사라진 경우에도 타이머는 반드시 취소한다.
            if (UWorld* World = GetWorld())
            {
                World->GetTimerManager().ClearTimer(BurstTimerHandle);
            }
            BurstTimerHandle.Invalidate();
            bBurstActive = false;
            bPolicyInitialized = false;
        }
    }
    else if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BurstTimerHandle);
        BurstTimerHandle.Invalidate();
    }

    Super::EndPlay(EndPlayReason);
}
