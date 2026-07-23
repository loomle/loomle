// Copyright 2026 Loomle contributors.

#include "LoomleBridgeModule.h"

#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorContext/EditorContextService.h"
#include "Generated/LoomleProtocolVersion.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "LoomlePipeServer.h"
#include "LoomleRequestCancellation.h"
#include "LoomleSetup.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Sal/Reference/SalReferenceInterface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogLoomleBridge, Log, All);

FLoomleBridgeModule::FLoomleBridgeModule() = default;
FLoomleBridgeModule::~FLoomleBridgeModule() = default;

namespace
{
constexpr const TCHAR* PipeNamePrefix = TEXT("loomle");
const FName SlateStyleSetName(TEXT("LoomleBridgeStyle"));
const FName StatusBarIconBrushName(TEXT("LoomleBridge.StatusBarIcon"));

FString GetLoomleHomeDirectory()
{
#if PLATFORM_WINDOWS
    FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    if (Home.IsEmpty())
    {
        Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEDRIVE"))
            + FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEPATH"));
    }
#else
    FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
    if (Home.IsEmpty())
    {
        Home = FPlatformProcess::UserDir();
    }
    FPaths::NormalizeFilename(Home);
    while (Home.EndsWith(TEXT("/")))
    {
        Home.LeftChopInline(1, EAllowShrinking::No);
    }
    return Home;
}

FString GetPluginVersion()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    if (!Plugin.IsValid() || Plugin->GetDescriptor().VersionName.IsEmpty())
    {
        return TEXT("unknown");
    }
    return Plugin->GetDescriptor().VersionName;
}

FString GetPluginBaseDir()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    return Plugin.IsValid()
        ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir())
        : FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("LoomleBridge"));
}

bool IsPathUnderDirectory(FString Path, FString Directory)
{
    FPaths::NormalizeFilename(Path);
    FPaths::NormalizeFilename(Directory);
    while (Directory.EndsWith(TEXT("/")))
    {
        Directory.LeftChopInline(1, EAllowShrinking::No);
    }
    return Path.Equals(Directory) || Path.StartsWith(Directory + TEXT("/"));
}

FString GetPluginInstallScope(const FString& ProjectRoot, const FString& PluginBaseDir)
{
    const FString ProjectPluginDir = FPaths::Combine(ProjectRoot, TEXT("Plugins"), TEXT("LoomleBridge"));
    return IsPathUnderDirectory(PluginBaseDir, ProjectPluginDir) ? TEXT("project") : TEXT("engine");
}

FString GetPluginManagedBy(const FString& ProjectRoot, const FString& PluginBaseDir)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    return GetPluginInstallScope(ProjectRoot, PluginBaseDir).Equals(TEXT("engine"))
        && Plugin.IsValid()
        && Plugin->GetDescriptor().bInstalled
        ? TEXT("fab")
        : TEXT("external");
}

FString GetBundledClientPath()
{
    return LoomleSetup::GetBundledClientPath(GetPluginBaseDir());
}

bool HasBundledClient()
{
    return LoomleSetup::HasBundledClient(GetPluginBaseDir());
}

FString GetCodexConfigPath()
{
    return LoomleSetup::ResolveCodexConfigPath(
        GetLoomleHomeDirectory(),
        FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME")));
}

FString GetClaudeDesktopConfigPath()
{
#if PLATFORM_MAC
    return FPaths::Combine(
        GetLoomleHomeDirectory(),
        TEXT("Library"),
        TEXT("Application Support"),
        TEXT("Claude"),
        TEXT("claude_desktop_config.json"));
#elif PLATFORM_WINDOWS
    FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
    if (AppData.IsEmpty())
    {
        AppData = FPaths::Combine(GetLoomleHomeDirectory(), TEXT("AppData"), TEXT("Roaming"));
    }
    return FPaths::Combine(AppData, TEXT("Claude"), TEXT("claude_desktop_config.json"));
#else
    return FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".config"), TEXT("Claude"), TEXT("claude_desktop_config.json"));
#endif
}

bool ClientFileExists(const FString& Path)
{
    return IFileManager::Get().FileSize(*Path) > 0;
}

LoomleSetup::FConfigAssessment AssessCodexSetup(const FString& RawConfig)
{
    return LoomleSetup::AssessCodexConfig(
        RawConfig,
        GetBundledClientPath(),
        HasBundledClient(),
        GetLoomleHomeDirectory(),
        ClientFileExists);
}

LoomleSetup::FConfigAssessment AssessClaudeSetup(const FString& RawConfig)
{
    return LoomleSetup::AssessClaudeConfig(
        RawConfig,
        GetBundledClientPath(),
        HasBundledClient(),
        GetLoomleHomeDirectory(),
        ClientFileExists);
}

bool IsBundledClientConfigured()
{
    FString RawCodex;
    const FString CodexPath = GetCodexConfigPath();
    if (!CodexPath.IsEmpty()
        && FFileHelper::LoadFileToString(RawCodex, *CodexPath)
        && AssessCodexSetup(RawCodex).ExistingKind == LoomleSetup::EClientEntryKind::Bundled)
    {
        return true;
    }

    FString RawClaude;
    return FFileHelper::LoadFileToString(RawClaude, *GetClaudeDesktopConfigPath())
        && AssessClaudeSetup(RawClaude).ExistingKind == LoomleSetup::EClientEntryKind::Bundled;
}

struct FHostSetupResult
{
    FString Host;
    FString ConfigPath;
    FString Message;
    FString SuggestedText;
    LoomleSetup::EClientEntryKind ExistingKind = LoomleSetup::EClientEntryKind::None;
    bool bDetected = false;
    bool bNeedsConfiguration = false;
    bool bNeedsMigration = false;
    bool bError = false;
};

FHostSetupResult AssessCodexHost()
{
    FHostSetupResult Result;
    Result.Host = TEXT("Codex");
    Result.ConfigPath = GetCodexConfigPath();
    if (Result.ConfigPath.IsEmpty())
    {
        const FString CodexHome = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_HOME")).TrimStartAndEnd();
        Result.bDetected = !CodexHome.IsEmpty();
        Result.bError = Result.bDetected;
        Result.Message = Result.bDetected
            ? TEXT("CODEX_HOME is not an absolute normalized directory; configuration was left unchanged.")
            : TEXT("Codex was not detected.");
        return Result;
    }

    Result.bDetected = FPaths::DirectoryExists(FPaths::GetPath(Result.ConfigPath))
        || FPaths::FileExists(Result.ConfigPath);
    FString RawConfig;
    if (FPaths::FileExists(Result.ConfigPath)
        && !FFileHelper::LoadFileToString(RawConfig, *Result.ConfigPath))
    {
        Result.bError = true;
        Result.Message = FString::Printf(TEXT("Codex config could not be read: %s"), *Result.ConfigPath);
        return Result;
    }

    const LoomleSetup::FConfigAssessment Assessment = AssessCodexSetup(RawConfig);
    Result.ExistingKind = Assessment.ExistingKind;
    Result.Message = Assessment.Message;
    Result.SuggestedText = Assessment.SuggestedText;
    Result.bNeedsConfiguration = Assessment.bNeedsConfiguration;
    Result.bNeedsMigration = Assessment.bNeedsMigration;
    Result.bError = Assessment.bBlocked;
    return Result;
}

FHostSetupResult AssessClaudeHost()
{
    FHostSetupResult Result;
    Result.Host = TEXT("Claude Desktop");
    Result.ConfigPath = GetClaudeDesktopConfigPath();
    Result.bDetected = FPaths::DirectoryExists(FPaths::GetPath(Result.ConfigPath))
        || FPaths::FileExists(Result.ConfigPath);
    FString RawConfig;
    if (FPaths::FileExists(Result.ConfigPath)
        && !FFileHelper::LoadFileToString(RawConfig, *Result.ConfigPath))
    {
        Result.bError = true;
        Result.Message = FString::Printf(TEXT("Claude Desktop config could not be read: %s"), *Result.ConfigPath);
        return Result;
    }

    const LoomleSetup::FConfigAssessment Assessment = AssessClaudeSetup(RawConfig);
    Result.ExistingKind = Assessment.ExistingKind;
    Result.Message = Assessment.Message;
    Result.SuggestedText = Assessment.SuggestedText;
    Result.bNeedsConfiguration = Assessment.bNeedsConfiguration;
    Result.bNeedsMigration = Assessment.bNeedsMigration;
    Result.bError = Assessment.bBlocked;
    return Result;
}

struct FSetupSummary
{
    TArray<FHostSetupResult> Hosts;
    bool bAnyDetected = false;
    bool bAnyCurrent = false;
    bool bAnyNeedsConfiguration = false;
    bool bAnyNeedsMigration = false;
    bool bAnyManual = false;
    bool bAnyBlocked = false;
};

FSetupSummary AssessHosts()
{
    FSetupSummary Summary;
    Summary.Hosts = {AssessCodexHost(), AssessClaudeHost()};
    for (const FHostSetupResult& Host : Summary.Hosts)
    {
        Summary.bAnyDetected |= Host.bDetected;
        Summary.bAnyCurrent |= Host.ExistingKind == LoomleSetup::EClientEntryKind::Bundled;
        Summary.bAnyNeedsConfiguration |= Host.bNeedsConfiguration;
        Summary.bAnyNeedsMigration |= Host.bNeedsMigration;
        Summary.bAnyManual |= !Host.bError && Host.ExistingKind == LoomleSetup::EClientEntryKind::Manual;
        Summary.bAnyBlocked |= Host.bError;
    }
    return Summary;
}

FString GetPlatformName()
{
#if PLATFORM_WINDOWS
    return TEXT("windows");
#elif PLATFORM_MAC
    return TEXT("darwin");
#elif PLATFORM_LINUX
    return TEXT("linux");
#else
    return TEXT("unknown");
#endif
}

uint64 StableFnv1a64(const FString& Input)
{
    constexpr uint64 OffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64 Prime = 0x100000001b3ull;
    const FTCHARToUTF8 Utf8(*Input);
    const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
    uint64 Hash = OffsetBasis;
    for (int32 Index = 0; Index < Utf8.Length(); ++Index)
    {
        Hash ^= static_cast<uint64>(Bytes[Index]);
        Hash *= Prime;
    }
    return Hash;
}

FString GetRpcPipeName(const FString& RuntimeId)
{
    return FString::Printf(TEXT("%s-%s"), PipeNamePrefix, *RuntimeId);
}
}

bool FLoomleBridgeModule::TickHealthSnapshot(float DeltaTime)
{
    (void)DeltaTime;
    UpdateHealthSnapshot();
    return true;
}

void FLoomleBridgeModule::RecordGameThreadProgress()
{
    check(IsInGameThread());
    GameThreadProgressSequence.fetch_add(1);
    LastGameThreadProgressCycles.store(FPlatformTime::Cycles64());
}

void FLoomleBridgeModule::UpdateHealthSnapshot()
{
    RecordGameThreadProgress();

    ELoomleBridgeLifecycle Lifecycle = static_cast<ELoomleBridgeLifecycle>(BridgeLifecycleState.load());
    const ELoomlePipeListenerState ListenerState = PipeServer.IsValid()
        ? PipeServer->GetListenerState()
        : ELoomlePipeListenerState::Stopped;
    const ELoomleBridgeLifecycle ResolvedLifecycle = ResolveBridgeLifecycle(
        Lifecycle,
        ListenerState,
        bEditorInitialized);
    if (ResolvedLifecycle != Lifecycle)
    {
        const ELoomleBridgeLifecycle PreviousLifecycle = Lifecycle;
        Lifecycle = ResolvedLifecycle;
        BridgeLifecycleState.store(static_cast<uint8>(Lifecycle));
        if (Lifecycle == ELoomleBridgeLifecycle::Ready)
        {
            UE_LOG(LogLoomleBridge, Display, TEXT("Loomle runtime %s is ready on %s"), *RuntimeId, *RuntimeEndpoint);
        }
        else if (PreviousLifecycle == ELoomleBridgeLifecycle::Ready
            && Lifecycle == ELoomleBridgeLifecycle::Failed)
        {
            RemoveRuntimeRegistration();
            UE_LOG(LogLoomleBridge, Error, TEXT("Loomle runtime %s stopped listening and was unpublished"), *RuntimeId);
        }
    }

    if (Lifecycle == ELoomleBridgeLifecycle::Ready && RuntimeRegistrationPath.IsEmpty())
    {
        WriteRuntimeRegistration();
    }
    bBridgeRunningSnapshot.Store(
        Lifecycle == ELoomleBridgeLifecycle::Ready
        && ListenerState == ELoomlePipeListenerState::Listening);
    bIsPIESnapshot.Store(GEditor != nullptr && GEditor->IsPlayingSessionInEditor());
}

FString FLoomleBridgeModule::GetBridgeLifecycleName() const
{
    switch (static_cast<ELoomleBridgeLifecycle>(BridgeLifecycleState.load()))
    {
    case ELoomleBridgeLifecycle::Starting:
        return TEXT("starting");
    case ELoomleBridgeLifecycle::Ready:
        return TEXT("ready");
    case ELoomleBridgeLifecycle::Draining:
        return TEXT("draining");
    case ELoomleBridgeLifecycle::Failed:
        return TEXT("failed");
    case ELoomleBridgeLifecycle::Offline:
    default:
        return TEXT("offline");
    }
}

ELoomleBridgeLifecycle FLoomleBridgeModule::ResolveBridgeLifecycle(
    const ELoomleBridgeLifecycle CurrentLifecycle,
    const ELoomlePipeListenerState ListenerState,
    const bool bEditorInitialized)
{
    if (CurrentLifecycle == ELoomleBridgeLifecycle::Starting)
    {
        if (ListenerState == ELoomlePipeListenerState::Listening
            && bEditorInitialized)
        {
            return ELoomleBridgeLifecycle::Ready;
        }
        if (ListenerState == ELoomlePipeListenerState::Failed
            || ListenerState == ELoomlePipeListenerState::Stopped)
        {
            return ELoomleBridgeLifecycle::Failed;
        }
    }
    else if (CurrentLifecycle == ELoomleBridgeLifecycle::Ready
        && ListenerState != ELoomlePipeListenerState::Listening)
    {
        return ELoomleBridgeLifecycle::Failed;
    }
    return CurrentLifecycle;
}

FString FLoomleBridgeModule::MakeProjectIdForNormalizedRoot(
    FString NormalizedProjectRoot,
    const bool bFoldCase)
{
    if (bFoldCase)
    {
        NormalizedProjectRoot.ToLowerInline();
    }
    return FString::Printf(
        TEXT("%016llx"),
        static_cast<unsigned long long>(StableFnv1a64(NormalizedProjectRoot)));
}

FString FLoomleBridgeModule::NormalizeProjectRoot(FString RawProjectRoot)
{
    RawProjectRoot = FPaths::ConvertRelativePathToFull(RawProjectRoot);
    FPaths::NormalizeFilename(RawProjectRoot);
    while (RawProjectRoot.EndsWith(TEXT("/")))
    {
        RawProjectRoot.LeftChopInline(1, EAllowShrinking::No);
    }
    return RawProjectRoot.IsEmpty() ? TEXT("/") : RawProjectRoot;
}

bool FLoomleBridgeModule::RemoveLegacyProjectRegistration(
    const FString& ProjectsDirectory,
    const FString& CanonicalProjectRoot,
    const FString& CanonicalProjectId,
    const bool bFoldCase)
{
    // Before 0.7, every platform folded path case before hashing. On POSIX,
    // remove only that known legacy record and only when its embedded root is
    // exactly this project. A record for a genuinely distinct /game project
    // must survive while /Game is being registered.
    const FString LegacyProjectId = MakeProjectIdForNormalizedRoot(CanonicalProjectRoot, true);
    if (LegacyProjectId == CanonicalProjectId)
    {
        return false;
    }

    const FString LegacyPath = FPaths::Combine(ProjectsDirectory, LegacyProjectId + TEXT(".json"));
    FString RawRecord;
    if (!FFileHelper::LoadFileToString(RawRecord, *LegacyPath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Record;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawRecord);
    FString StoredProjectId;
    FString StoredProjectRoot;
    if (!FJsonSerializer::Deserialize(Reader, Record)
        || !Record.IsValid()
        || !Record->TryGetStringField(TEXT("projectId"), StoredProjectId)
        || !Record->TryGetStringField(TEXT("projectRoot"), StoredProjectRoot)
        || StoredProjectId != LegacyProjectId
        || FPaths::IsRelative(StoredProjectRoot))
    {
        return false;
    }

    const ESearchCase::Type SearchCase = bFoldCase
        ? ESearchCase::IgnoreCase
        : ESearchCase::CaseSensitive;
    if (!NormalizeProjectRoot(StoredProjectRoot).Equals(CanonicalProjectRoot, SearchCase))
    {
        return false;
    }
    return IFileManager::Get().Delete(*LegacyPath, false, true);
}

void FLoomleBridgeModule::InitializeRuntimeIdentity()
{
    ProjectRoot = NormalizeProjectRoot(FPaths::ProjectDir());
    ProjectId = MakeProjectIdForNormalizedRoot(
        ProjectRoot,
#if PLATFORM_WINDOWS
        true
#else
        false
#endif
    );
    RuntimeId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    RuntimeStartedAt = FDateTime::UtcNow().ToIso8601();
#if PLATFORM_WINDOWS
    RuntimeEndpoint = FString::Printf(TEXT("\\\\.\\pipe\\%s"), *GetRpcPipeName(RuntimeId));
#else
    const FString EndpointDirectory = FPaths::Combine(
        GetLoomleHomeDirectory(),
        TEXT(".loomle"),
        TEXT("state"),
        TEXT("endpoints"));
    IFileManager::Get().MakeDirectory(*EndpointDirectory, true);
    RuntimeEndpoint = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(EndpointDirectory, RuntimeId + TEXT(".sock")));
#endif
}

void FLoomleBridgeModule::RegisterStatusBarWidget()
{
    if (UToolMenus::IsToolMenuUIEnabled())
    {
        RegisterStatusBarMenus();
    }
}

void FLoomleBridgeModule::UnregisterStatusBarWidget()
{
    if (UToolMenus::IsToolMenuUIEnabled())
    {
        UToolMenus::UnregisterOwner(TEXT("LoomleBridge"));
    }
}

void FLoomleBridgeModule::RegisterLoomleSlateStyle()
{
    if (LoomleSlateStyle.IsValid())
    {
        return;
    }
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Unable to register Loomle status icon: plugin not found."));
        return;
    }
    LoomleSlateStyle = MakeShared<FSlateStyleSet>(SlateStyleSetName);
    LoomleSlateStyle->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
    LoomleSlateStyle->Set(
        StatusBarIconBrushName,
        new FSlateImageBrush(
            LoomleSlateStyle->RootToContentDir(TEXT("LoomleToolbarIcon"), TEXT(".png")),
            FVector2D(16.0f, 16.0f)));
    FSlateStyleRegistry::RegisterSlateStyle(*LoomleSlateStyle);
}

void FLoomleBridgeModule::UnregisterLoomleSlateStyle()
{
    if (LoomleSlateStyle.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*LoomleSlateStyle);
        LoomleSlateStyle.Reset();
    }
}

void FLoomleBridgeModule::RegisterStatusBarMenus()
{
    if (!UToolMenus::IsToolMenuUIEnabled())
    {
        return;
    }
    FToolMenuOwnerScoped OwnerScoped(TEXT("LoomleBridge"));
    UToolMenu* StatusBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.StatusBar.ToolBar"));
    if (StatusBar == nullptr)
    {
        return;
    }
    FToolMenuSection& Section = StatusBar->FindOrAddSection(TEXT("LoomleBridgeStatus"));
    Section.AddDynamicEntry(TEXT("LoomleBridgeStatusEntry"), FNewToolMenuSectionDelegate::CreateLambda(
        [this](FToolMenuSection& InSection)
        {
            InSection.AddEntry(FToolMenuEntry::InitWidget(
                TEXT("LoomleBridgeStatusWidget"),
                SNew(SComboButton)
                .HasDownArrow(false)
                .ContentPadding(FMargin(6.0f, 0.0f))
                .ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(TEXT("SimpleComboButton")))
                .MenuPlacement(MenuPlacement_AboveAnchor)
                .OnGetMenuContent_Raw(this, &FLoomleBridgeModule::CreateSetupStatusPanel)
                .ButtonContent()[CreateStatusBarButtonContent()],
                FText::GetEmpty(),
                true,
                false));
        }));
}

TSharedRef<SWidget> FLoomleBridgeModule::CreateStatusBarButtonContent()
{
    return SNew(SHorizontalBox)
        .ToolTipText_Lambda([this]() { return GetToolbarStatusTooltip(); })
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SBox).WidthOverride(16.0f).HeightOverride(16.0f)
            [
                SNew(SImage)
                .Image_Lambda([this]() -> const FSlateBrush*
                {
                    return LoomleSlateStyle.IsValid()
                        ? LoomleSlateStyle->GetBrush(StatusBarIconBrushName)
                        : FAppStyle::GetBrush(TEXT("Icons.Warning"));
                })
                .ColorAndOpacity_Lambda([this]() { return GetToolbarStatusColor(); })
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Loomle")))
            .ToolTipText_Lambda([this]() { return GetToolbarStatusTooltip(); })
        ];
}

TSharedRef<SWidget> FLoomleBridgeModule::CreateSetupStatusPanel()
{
    const bool bReady = bBridgeRunningSnapshot.Load();
    const bool bBundledClientAvailable = HasBundledClient();
    const FSetupSummary Setup = bReady ? AssessHosts() : FSetupSummary();
    const int32 ConnectionCount = PipeServer.IsValid() ? PipeServer->GetActiveConnectionCount() : 0;

    FString MainText = GetSetupPanelNextActionText().ToString();
    if (bReady && ConnectionCount > 0)
    {
        MainText = FString::Printf(TEXT("Bridge is ready. %d Loomle Client connection(s) are active."), ConnectionCount);
    }
    else if (bReady && Setup.bAnyBlocked)
    {
        MainText = TEXT("Bridge is ready, but MCP configuration could not be assessed safely. Loomle changed nothing.");
    }
    else if (bReady && Setup.bAnyNeedsMigration)
    {
        MainText = TEXT("Bridge is ready. A legacy or stale Loomle MCP entry needs migration.");
    }
    else if (bReady && Setup.bAnyNeedsConfiguration)
    {
        MainText = TEXT("Bridge is ready. A detected MCP host still needs a Loomle entry.");
    }

    FString Activity = TEXT("none since editor start");
    GetClientActivitySummary(Activity);
    const FString PluginBaseDir = GetPluginBaseDir();
    const FString CurrentProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString AdvancedText = FString::Printf(
        TEXT("Bridge: %s\nProject: %s\nEndpoint: %s\nConnections: %d\nLast activity: %s\n\nPlugin: %s\nVersion: %s\nScope: %s\nManaged by: %s\n\nClient payload: %s\nClient target: %s\nClient path: %s"),
        *GetToolbarStatusKey().ToLower(),
        FApp::GetProjectName(),
        *GetRuntimeEndpointDisplayString(),
        ConnectionCount,
        *Activity,
        *PluginBaseDir,
        *GetPluginVersion(),
        *GetPluginInstallScope(CurrentProjectRoot, PluginBaseDir),
        *GetPluginManagedBy(CurrentProjectRoot, PluginBaseDir),
        bBundledClientAvailable ? TEXT("available") : TEXT("missing"),
        *LoomleSetup::GetCurrentClientTarget(),
        *GetBundledClientPath());
    const bool bShowSetupPrompt = bReady
        && bBundledClientAvailable
        && (!Setup.bAnyCurrent || Setup.bAnyNeedsConfiguration || Setup.bAnyNeedsMigration);

    return SNew(SBox).WidthOverride(460.0f)
        [
            SNew(SBorder).Padding(12.0f).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Loomle ") + GetToolbarStatusKey()))
                    .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
                [SNew(STextBlock).Text(FText::FromString(MainText)).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Visibility(bShowSetupPrompt ? EVisibility::Visible : EVisibility::Collapsed)
                    .Text(FText::FromString(TEXT("Copy Setup Prompt")))
                    .OnClicked_Raw(this, &FLoomleBridgeModule::CopySetupPrompt)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea).InitiallyCollapsed(true)
                    .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Advanced details")))]
                    .BodyContent()[SNew(STextBlock).Text(FText::FromString(AdvancedText)).AutoWrapText(true)]
                ]
            ]
        ];
}

void FLoomleBridgeModule::RecordClientActivity(const FString& Method, const FString& ToolName)
{
    FScopeLock Lock(&ClientActivityMutex);
    bHasClientActivity = true;
    LastClientActivityAt = FDateTime::UtcNow();
    LastClientMethod = Method;
    LastClientTool = ToolName;
    ++ClientActivityCount;
}

bool FLoomleBridgeModule::GetClientActivitySummary(FString& OutDetail) const
{
    FScopeLock Lock(&ClientActivityMutex);
    if (!bHasClientActivity)
    {
        OutDetail = TEXT("none since editor start");
        return false;
    }
    const int32 AgeSeconds = FMath::Max(
        0,
        FMath::FloorToInt((FDateTime::UtcNow() - LastClientActivityAt).GetTotalSeconds()));
    FString LastCall = LastClientMethod;
    if (!LastClientTool.IsEmpty())
    {
        LastCall += TEXT(" / ") + LastClientTool;
    }
    OutDetail = FString::Printf(
        TEXT("%s, %d seconds ago; %llu call(s) since editor start"),
        *LastCall,
        AgeSeconds,
        static_cast<unsigned long long>(ClientActivityCount));
    return true;
}

FText FLoomleBridgeModule::GetToolbarStatusTooltip() const
{
    return FText::FromString(FString::Printf(TEXT("Loomle %s - Click for MCP status."), *GetToolbarStatusKey()));
}

FSlateColor FLoomleBridgeModule::GetToolbarStatusColor() const
{
    if (GetToolbarStatusKey().Equals(TEXT("Ready")))
    {
        return FSlateColor(FLinearColor(0.15f, 0.55f, 0.22f, 1.0f));
    }
    if (GetToolbarStatusKey().Equals(TEXT("PIE")))
    {
        return FSlateColor(FLinearColor(0.16f, 0.36f, 0.78f, 1.0f));
    }
    return FSlateColor(FLinearColor(0.45f, 0.18f, 0.18f, 1.0f));
}

FString FLoomleBridgeModule::GetToolbarStatusKey() const
{
    if (!bBridgeRunningSnapshot.Load())
    {
        return TEXT("Offline");
    }
    return bIsPIESnapshot.Load() ? TEXT("PIE") : TEXT("Ready");
}

FString FLoomleBridgeModule::GetRuntimeEndpointDisplayString() const
{
    return RuntimeEndpoint;
}

FText FLoomleBridgeModule::GetSetupPanelNextActionText() const
{
    if (!bBridgeRunningSnapshot.Load())
    {
        return FText::FromString(TEXT("Bridge is offline. Enable LoomleBridge and restart the editor."));
    }
    if (IsBundledClientConfigured())
    {
        return FText::FromString(TEXT("Loomle is ready. The configured MCP host can connect to this project."));
    }
    if (HasBundledClient())
    {
        return FText::FromString(TEXT("Loomle is ready. Copy the setup prompt to configure an MCP host."));
    }
    return FText::FromString(TEXT("Bridge is ready, but the matching bundled Loomle Client is missing."));
}

FString FLoomleBridgeModule::GetSetupPanelSetupPrompt() const
{
    const FString ClientPath = GetBundledClientPath();
    if (ClientPath.IsEmpty() || !HasBundledClient())
    {
        return TEXT("The LoomleBridge plugin has no compatible bundled Client. Reinstall or update the plugin before configuring MCP.");
    }

    TArray<FString> HostInstructions;
    for (const FHostSetupResult& Host : AssessHosts().Hosts)
    {
        if (Host.bError)
        {
            HostInstructions.Add(FString::Printf(
                TEXT("%s — BLOCKED. Do not edit %s. %s"),
                *Host.Host,
                Host.ConfigPath.IsEmpty() ? TEXT("its config") : *Host.ConfigPath,
                *Host.Message));
        }
        else if (Host.ExistingKind == LoomleSetup::EClientEntryKind::Bundled)
        {
            HostInstructions.Add(FString::Printf(
                TEXT("%s — CURRENT. Keep %s unchanged."),
                *Host.Host,
                *Host.ConfigPath));
        }
        else if (Host.ExistingKind == LoomleSetup::EClientEntryKind::Manual)
        {
            HostInstructions.Add(FString::Printf(
                TEXT("%s — MANUAL. Preserve the custom entry in %s."),
                *Host.Host,
                *Host.ConfigPath));
        }
        else if (Host.bNeedsMigration)
        {
            HostInstructions.Add(FString::Printf(
                TEXT("%s — NEEDS MIGRATION. Preserve unrelated settings in %s and replace only the stale Loomle entry with:\n%s"),
                *Host.Host,
                *Host.ConfigPath,
                *Host.SuggestedText));
        }
        else
        {
            HostInstructions.Add(FString::Printf(
                TEXT("%s — MISSING. Preserve unrelated settings in %s and merge:\n%s"),
                *Host.Host,
                Host.ConfigPath.IsEmpty() ? TEXT("its config") : *Host.ConfigPath,
                *Host.SuggestedText));
        }
    }

    return FString::Printf(
        TEXT("Set up only the requesting MCP host and preserve unrelated configuration. "
             "Use command \"%s\" with the single argument \"mcp\".\n\n%s\n\n"
             "Then connect to the open Unreal project and call editor_context before making changes."),
        *ClientPath,
        *FString::Join(HostInstructions, TEXT("\n\n")));
}

FReply FLoomleBridgeModule::CopySetupPrompt()
{
    FPlatformApplicationMisc::ClipboardCopy(*GetSetupPanelSetupPrompt());
    return FReply::Handled();
}

void FLoomleBridgeModule::WriteProjectRegistration(const FString& InProjectRoot, const FString& InProjectId)
{
    const FString Directory = FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".loomle"), TEXT("state"), TEXT("projects"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::Combine(Directory, InProjectId + TEXT(".json"));
    const FString TempPath = Path + TEXT(".tmp.") + RuntimeId;

    FString RegisteredAt = FDateTime::UtcNow().ToIso8601();
    FString Existing;
    if (FFileHelper::LoadFileToString(Existing, *Path))
    {
        TSharedPtr<FJsonObject> ExistingRecord;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Existing);
        if (FJsonSerializer::Deserialize(Reader, ExistingRecord) && ExistingRecord.IsValid())
        {
            ExistingRecord->TryGetStringField(TEXT("registeredAt"), RegisteredAt);
        }
    }

    const FString PluginBaseDir = GetPluginBaseDir();
    TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetNumberField(TEXT("schemaVersion"), 1);
    Record->SetStringField(TEXT("projectId"), InProjectId);
    Record->SetStringField(TEXT("name"), FApp::GetProjectName());
    Record->SetStringField(TEXT("projectRoot"), InProjectRoot);
    Record->SetStringField(TEXT("uproject"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Record->SetStringField(TEXT("pluginPath"), PluginBaseDir);
    Record->SetStringField(TEXT("pluginInstallScope"), GetPluginInstallScope(InProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginManagedBy"), GetPluginManagedBy(InProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginVersion"), GetPluginVersion());
    Record->SetStringField(TEXT("platform"), GetPlatformName());
    Record->SetStringField(TEXT("registeredAt"), RegisteredAt);
    Record->SetStringField(TEXT("lastSeenAt"), FDateTime::UtcNow().ToIso8601());
    Record->SetStringField(TEXT("source"), TEXT("runtime"));

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Record.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Output + TEXT("\n"), *TempPath)
        || !IFileManager::Get().Move(*Path, *TempPath, true, true))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to publish Loomle project registration %s"), *Path);
        IFileManager::Get().Delete(*TempPath, false, true);
        return;
    }

    if (RemoveLegacyProjectRegistration(
        Directory,
        InProjectRoot,
        InProjectId,
#if PLATFORM_WINDOWS
        true
#else
        false
#endif
    ))
    {
        UE_LOG(LogLoomleBridge, Display, TEXT("Removed the legacy Loomle project registration for %s"), *InProjectRoot);
    }
}

void FLoomleBridgeModule::WriteRuntimeRegistration()
{
    if (RuntimeId.IsEmpty()
        || ProjectId.IsEmpty()
        || RuntimeEndpoint.IsEmpty()
        || static_cast<ELoomleBridgeLifecycle>(BridgeLifecycleState.load()) != ELoomleBridgeLifecycle::Ready
        || !PipeServer.IsValid()
        || PipeServer->GetListenerState() != ELoomlePipeListenerState::Listening)
    {
        return;
    }

    const FString Directory = FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".loomle"), TEXT("state"), TEXT("runtimes"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString RegistrationPath = FPaths::Combine(Directory, RuntimeId + TEXT(".json"));
    const FString TempPath = RegistrationPath + TEXT(".tmp");
    const FString PluginBaseDir = GetPluginBaseDir();
    const FString Now = FDateTime::UtcNow().ToIso8601();
    TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetNumberField(TEXT("schemaVersion"), 2);
    Record->SetStringField(TEXT("runtimeId"), RuntimeId);
    Record->SetStringField(TEXT("projectId"), ProjectId);
    Record->SetStringField(TEXT("name"), FApp::GetProjectName());
    Record->SetStringField(TEXT("projectRoot"), ProjectRoot);
    Record->SetStringField(TEXT("uproject"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Record->SetStringField(TEXT("endpoint"), RuntimeEndpoint);
    Record->SetStringField(TEXT("platform"), GetPlatformName());
    Record->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
    Record->SetStringField(TEXT("pluginPath"), PluginBaseDir);
    Record->SetStringField(TEXT("pluginInstallScope"), GetPluginInstallScope(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginManagedBy"), GetPluginManagedBy(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginVersion"), GetPluginVersion());
    Record->SetNumberField(TEXT("protocolVersion"), Loomle::Protocol::Version);
    Record->SetStringField(TEXT("startedAt"), RuntimeStartedAt);
    Record->SetStringField(TEXT("lastSeenAt"), Now);

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Record.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Output + TEXT("\n"), *TempPath)
        || !IFileManager::Get().Move(*RegistrationPath, *TempPath, true, true))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to publish Loomle runtime registration %s"), *RegistrationPath);
        IFileManager::Get().Delete(*TempPath, false, true);
        return;
    }
    RuntimeRegistrationPath = RegistrationPath;
    WriteProjectRegistration(ProjectRoot, ProjectId);
}

void FLoomleBridgeModule::RemoveRuntimeRegistration()
{
    if (!RuntimeRegistrationPath.IsEmpty())
    {
        IFileManager::Get().Delete(*RuntimeRegistrationPath, false, true);
        RuntimeRegistrationPath.Reset();
    }
}

void FLoomleBridgeModule::StopBridgeRuntime(bool bWaitForWorkers)
{
    RemoveRuntimeRegistration();
    if (PipeServer.IsValid())
    {
        if (bWaitForWorkers)
        {
            PipeServer->StopServer();
            PipeServer.Reset();
        }
        else
        {
            PipeServer->Stop();
        }
    }

    if (bWaitForWorkers && ActiveGameThreadDispatchCount.GetValue() > 0)
    {
        check(IsInGameThread());
        while (ActiveGameThreadDispatchCount.GetValue() > 0)
        {
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        }
    }
    bBridgeRunningSnapshot.Store(false);
    bIsPIESnapshot.Store(false);
    if (bWaitForWorkers)
    {
        BridgeLifecycleState.store(static_cast<uint8>(ELoomleBridgeLifecycle::Offline));
    }
}

void FLoomleBridgeModule::BeginBridgeShutdown()
{
    if (bIsShuttingDown.Exchange(true))
    {
        return;
    }

    BridgeLifecycleState.store(static_cast<uint8>(ELoomleBridgeLifecycle::Draining));
    bBridgeRunningSnapshot.Store(false);
    Loomle::Sal::FSalReferenceInterface::Shutdown();
    Loomle::EditorContext::FEditorContextService::Get().Shutdown();
    StopBridgeRuntime(false);
}

void FLoomleBridgeModule::HandleShutdownPostPackagesSaved()
{
    BeginBridgeShutdown();
}

void FLoomleBridgeModule::HandleEditorPreExit()
{
    BeginBridgeShutdown();
}

void FLoomleBridgeModule::HandleEnginePreExit()
{
    BeginBridgeShutdown();
}

void FLoomleBridgeModule::HandlePreExit()
{
    BeginBridgeShutdown();
}

void FLoomleBridgeModule::HandleEditorInitialized(const double Duration)
{
    (void)Duration;
    check(IsInGameThread());
    bEditorInitialized = true;
    UpdateHealthSnapshot();
}

void FLoomleBridgeModule::StartupModule()
{
    bIsShuttingDown.Store(false);
    // GIsRunning is false during the configured PostEngineInit loading phase
    // and true when this module is loaded dynamically after Editor startup.
    bEditorInitialized = GIsRunning;
    BridgeLifecycleState.store(static_cast<uint8>(ELoomleBridgeLifecycle::Starting));
    GameThreadProgressSequence.store(0);
    LastGameThreadProgressCycles.store(0);
    RuntimeRegistrationPath.Reset();
    InitializeRuntimeIdentity();
    RegisterLoomleSlateStyle();
    RequestCancellationRegistry = MakeUnique<Loomle::Runtime::FRequestCancellationRegistry>();
    if (!bEditorInitialized)
    {
        EditorInitializedHandle = FEditorDelegates::OnEditorInitialized.AddRaw(
            this,
            &FLoomleBridgeModule::HandleEditorInitialized);
    }
    ShutdownPostPackagesSavedHandle = FEditorDelegates::OnShutdownPostPackagesSaved.AddRaw(
        this,
        &FLoomleBridgeModule::HandleShutdownPostPackagesSaved);
    EditorPreExitHandle = FEditorDelegates::OnEditorPreExit.AddRaw(
        this,
        &FLoomleBridgeModule::HandleEditorPreExit);
    EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(
        this,
        &FLoomleBridgeModule::HandleEnginePreExit);
    PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FLoomleBridgeModule::HandlePreExit);

#if PLATFORM_WINDOWS
    const FString PipeName = GetRpcPipeName(RuntimeId);
#endif
    PipeServer = MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
#if PLATFORM_WINDOWS
        PipeName,
#else
        RuntimeEndpoint,
#endif
        [this](int32 ConnectionSerial, const FString& RequestLine)
        {
            return HandleRequest(ConnectionSerial, RequestLine);
        },
        [this](int32 ConnectionSerial)
        {
            CancelRequestsForConnection(ConnectionSerial);
        });

    if (!PipeServer->Start())
    {
        UE_LOG(LogLoomleBridge, Error, TEXT("Failed to start Loomle pipe server."));
        BridgeLifecycleState.store(static_cast<uint8>(ELoomleBridgeLifecycle::Failed));
        PipeServer.Reset();
        if (EditorInitializedHandle.IsValid())
        {
            FEditorDelegates::OnEditorInitialized.Remove(EditorInitializedHandle);
            EditorInitializedHandle.Reset();
        }
        FEditorDelegates::OnShutdownPostPackagesSaved.Remove(ShutdownPostPackagesSavedHandle);
        ShutdownPostPackagesSavedHandle.Reset();
        FEditorDelegates::OnEditorPreExit.Remove(EditorPreExitHandle);
        EditorPreExitHandle.Reset();
        FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
        EnginePreExitHandle.Reset();
        FCoreDelegates::OnPreExit.Remove(PreExitHandle);
        PreExitHandle.Reset();
        RequestCancellationRegistry.Reset();
        UnregisterLoomleSlateStyle();
        return;
    }

    Loomle::Sal::FSalReferenceInterface::Startup();
    Loomle::EditorContext::FEditorContextService::Get().Startup();
    UpdateHealthSnapshot();
    HealthSnapshotTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FLoomleBridgeModule::TickHealthSnapshot),
        0.1f);
    StatusBarStartupHandle = UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLoomleBridgeModule::RegisterStatusBarMenus));
    RegisterStatusBarWidget();

#if PLATFORM_WINDOWS
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge starting on named pipe \\\\.\\pipe\\%s"), *PipeName);
#else
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge starting on unix socket %s"), *GetRuntimeEndpointDisplayString());
#endif
}

void FLoomleBridgeModule::ShutdownModule()
{
    BeginBridgeShutdown();
    if (EditorInitializedHandle.IsValid())
    {
        FEditorDelegates::OnEditorInitialized.Remove(EditorInitializedHandle);
        EditorInitializedHandle.Reset();
    }
    if (ShutdownPostPackagesSavedHandle.IsValid())
    {
        FEditorDelegates::OnShutdownPostPackagesSaved.Remove(ShutdownPostPackagesSavedHandle);
        ShutdownPostPackagesSavedHandle.Reset();
    }
    if (EditorPreExitHandle.IsValid())
    {
        FEditorDelegates::OnEditorPreExit.Remove(EditorPreExitHandle);
        EditorPreExitHandle.Reset();
    }
    if (EnginePreExitHandle.IsValid())
    {
        FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
        EnginePreExitHandle.Reset();
    }
    if (PreExitHandle.IsValid())
    {
        FCoreDelegates::OnPreExit.Remove(PreExitHandle);
        PreExitHandle.Reset();
    }
    StopBridgeRuntime(true);
    if (StatusBarStartupHandle.IsValid())
    {
        UToolMenus::UnRegisterStartupCallback(StatusBarStartupHandle);
        StatusBarStartupHandle.Reset();
    }
    UnregisterStatusBarWidget();
    UnregisterLoomleSlateStyle();
    if (HealthSnapshotTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(HealthSnapshotTickerHandle);
        HealthSnapshotTickerHandle.Reset();
    }
    RequestCancellationRegistry.Reset();
}

IMPLEMENT_MODULE(FLoomleBridgeModule, LoomleBridge)
