// Copyright 2026 Loomle contributors.

#include "LoomleBridgeModule.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Algo/Sort.h"
#include "Blueprint/UserWidget.h"
#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PanelSlot.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/MemoryBase.h"
#include "HAL/MemoryMisc.h"
#include "IPythonScriptPlugin.h"
#include "Input/HittestGrid.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FormatText.h"
#include "K2Node_Select.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "Engine/Blueprint.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "MaterialEditingLibrary.h"
#include "IMaterialEditor.h"
#include "MaterialEditorUtilities.h"
#include "Interfaces/IMainFrameModule.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "PCGComponent.h"
#include "PCGDataAsset.h"
#include "PCGGraph.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGEdge.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "PCGManagedResource.h"
#include "Metadata/PCGDefaultValueInterface.h"
#include "Elements/PCGActorSelector.h"
#include "Elements/PCGApplyOnActor.h"
#include "Elements/Blueprint/PCGBlueprintBaseElement.h"
#include "Elements/PCGExecuteBlueprint.h"
#include "Helpers/PCGDataLayerHelpers.h"
#include "Helpers/PCGHLODHelpers.h"
#include "Elements/PCGFilterByAttribute.h"
#include "Elements/IO/PCGLoadAssetElement.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGGetActorProperty.h"
#include "Elements/PCGLoopElement.h"
#include "Elements/PCGReroute.h"
#include "Elements/PCGSpawnActor.h"
#include "Elements/PCGSpawnSpline.h"
#include "Elements/PCGSpawnSplineMesh.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "Elements/PCGSkinnedMeshSpawner.h"
#include "Elements/PCGTypedGetter.h"
#include "Elements/PCGUserParameterGet.h"
#include "PCGSubgraph.h"
#include "Editor/IPCGEditorModule.h"
#include "PCGEditor.h"
#include "InstanceDataPackers/PCGSkinnedMeshInstanceDataPackerBase.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "MeshSelectors/PCGMeshSelectorWeightedByCategory.h"
#include "MeshSelectors/PCGSkinnedMeshSelector.h"
#include "LoomleBlueprintAdapter.h"
#include "LoomleMutationResult.h"
#include "LoomlePipeServer.h"
#include "ScopedTransaction.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Misc/OutputDevice.h"
#include "Stats/StatsData.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "TickTaskManagerInterface.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"
#include "Misc/TransactionObjectEvent.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/WidgetRenderer.h"
#include "DynamicRHI.h"
#include "GPUProfiler.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "RHIStats.h"
#include "ToolMenus.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorTabs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetReference.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogLoomleBridge, Log, All);

using FCondensedJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

namespace
{
TSharedPtr<FJsonObject> BuildBlueprintNodeEditCapabilities(const TSharedPtr<FJsonObject>& Node);
}

namespace LoomleBridgeConstants
{
    static const TCHAR* PipeNamePrefix = TEXT("loomle");
    static const TCHAR* RpcVersion = TEXT("1.0");
    static const TCHAR* ExecuteToolName = TEXT("execute");
    static const TCHAR* JobsToolName = TEXT("jobs");
    static const TCHAR* ProfilingToolName = TEXT("profiling");
    static const TCHAR* PlayToolName = TEXT("play");
    static const TCHAR* EditorOpenToolName = TEXT("editor.open");
    static const TCHAR* EditorFocusToolName = TEXT("editor.focus");
    static const TCHAR* EditorScreenshotToolName = TEXT("editor.screenshot");
    static const TCHAR* DiagnosticTailToolName = TEXT("diagnostic.tail");
    static const TCHAR* LogTailToolName = TEXT("log.tail");
    static const FName SlateStyleSetName(TEXT("LoomleBridgeStyle"));
    static const FName StatusBarIconBrushName(TEXT("LoomleBridge.StatusBarIcon"));
    constexpr int32 ProtocolVersion = 1;
    constexpr double MutateIdempotencyTtlSeconds = 1800.0;
    constexpr int32 MaxMutateIdempotencyEntries = 2048;
}

namespace
{
FString GetLoomleHomeDirectory();

FString GetLoomleBridgePluginVersion()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    if (!Plugin.IsValid())
    {
        return TEXT("unknown");
    }

    const FString& VersionName = Plugin->GetDescriptor().VersionName;
    return VersionName.IsEmpty() ? TEXT("unknown") : VersionName;
}

FString GetLoomleBridgePluginBaseDir()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoomleBridge"));
    if (!Plugin.IsValid())
    {
        return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("LoomleBridge"));
    }
    return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
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

FString GetLoomleBridgePluginInstallScope(const FString& ProjectRoot, const FString& PluginBaseDir)
{
    const FString ProjectPluginDir = FPaths::Combine(ProjectRoot, TEXT("Plugins"), TEXT("LoomleBridge"));
    if (IsPathUnderDirectory(PluginBaseDir, ProjectPluginDir))
    {
        return TEXT("project");
    }
    return TEXT("engine");
}

FString GetLoomleBridgePluginManagedBy(const FString& ProjectRoot, const FString& PluginBaseDir)
{
    const FString FabMarkerPath = FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("Loomle"), TEXT("package.json"));
    const FString PythonMcpPath = FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("MCP"), TEXT("loomle_mcp_server.py"));
    if (FPaths::FileExists(FabMarkerPath) || FPaths::FileExists(PythonMcpPath))
    {
        return TEXT("fab");
    }

    const FString Scope = GetLoomleBridgePluginInstallScope(ProjectRoot, PluginBaseDir);
    if (Scope.Equals(TEXT("project")))
    {
        return TEXT("native");
    }
    return TEXT("external");
}

FString GetFabPythonMcpServerPath()
{
    return FPaths::Combine(
        GetLoomleBridgePluginBaseDir(),
        TEXT("Resources"),
        TEXT("MCP"),
        TEXT("loomle_mcp_server.py"));
}

bool HasFabPythonMcpServer()
{
    return FPaths::FileExists(GetFabPythonMcpServerPath());
}

FString GetCodexConfigPath()
{
    return FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".codex"), TEXT("config.toml"));
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

bool ConfigLooksLikeLoomleAny(const FString& RawConfig)
{
    FString Lower = RawConfig.ToLower();
    return Lower.Contains(TEXT("[mcp_servers.loomle]"))
        || Lower.Contains(TEXT("[mcp.servers.loomle]"))
        || (Lower.Contains(TEXT("mcpservers")) && Lower.Contains(TEXT("\"loomle\"")));
}

bool ConfigLooksLikeLoomleNative(const FString& RawConfig)
{
    FString Lower = RawConfig.ToLower();
    return ConfigLooksLikeLoomleAny(RawConfig)
        && (Lower.Contains(TEXT(".loomle/bin/loomle"))
            || Lower.Contains(TEXT("\\.loomle\\bin\\loomle"))
            || Lower.Contains(TEXT("loomle mcp")));
}

bool ConfigLooksLikeLoomleFab(const FString& RawConfig)
{
    FString Lower = RawConfig.ToLower();
    return ConfigLooksLikeLoomleAny(RawConfig)
        && (Lower.Contains(TEXT("loomle_mcp_server.py")) || Lower.Contains(TEXT("resources/mcp")));
}

bool IsNativeLoomleConfigured()
{
    FString RawCodex;
    if (FFileHelper::LoadFileToString(RawCodex, *GetCodexConfigPath()) && ConfigLooksLikeLoomleNative(RawCodex))
    {
        return true;
    }

    FString RawClaude;
    if (FFileHelper::LoadFileToString(RawClaude, *GetClaudeDesktopConfigPath()) && ConfigLooksLikeLoomleNative(RawClaude))
    {
        return true;
    }

    return false;
}

enum class ELoomleMcpEntryOwner
{
    None,
    Native,
    Fab,
    Manual
};

ELoomleMcpEntryOwner ClassifyLoomleMcpEntryOwner(const FString& RawConfig)
{
    if (!ConfigLooksLikeLoomleAny(RawConfig))
    {
        return ELoomleMcpEntryOwner::None;
    }
    if (ConfigLooksLikeLoomleNative(RawConfig))
    {
        return ELoomleMcpEntryOwner::Native;
    }
    if (ConfigLooksLikeLoomleFab(RawConfig))
    {
        return ELoomleMcpEntryOwner::Fab;
    }
    return ELoomleMcpEntryOwner::Manual;
}

FString LoomleMcpEntryOwnerToString(const ELoomleMcpEntryOwner Owner)
{
    switch (Owner)
    {
    case ELoomleMcpEntryOwner::Native:
        return TEXT("native");
    case ELoomleMcpEntryOwner::Fab:
        return TEXT("fab");
    case ELoomleMcpEntryOwner::Manual:
        return TEXT("manual");
    case ELoomleMcpEntryOwner::None:
    default:
        return TEXT("none");
    }
}

struct FLoomleHostSetupResult
{
    FString Host;
    FString ConfigPath;
    FString Status;
    FString Message;
    FString BackupPath;
    ELoomleMcpEntryOwner ExistingOwner = ELoomleMcpEntryOwner::None;
    bool bDetected = false;
    bool bChanged = false;
    bool bError = false;
};

FString GetNativeLoomleCliPath()
{
    return FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".loomle"), TEXT("bin"), TEXT("loomle"));
}

FString GetSetupPanelMcpServerOwner()
{
    return HasFabPythonMcpServer() ? TEXT("Fab Python MCP") : TEXT("native loomle MCP");
}

bool HasSetupPanelMcpServerPayload()
{
    return HasFabPythonMcpServer() || FPaths::FileExists(GetNativeLoomleCliPath());
}

bool MakeBackupIfNeeded(const FString& ConfigPath, FString& OutBackupPath, FString& OutError)
{
    OutBackupPath.Reset();
    if (!FPaths::FileExists(ConfigPath))
    {
        return true;
    }
    OutBackupPath = FString::Printf(TEXT("%s.loomle-backup-%lld"), *ConfigPath, static_cast<long long>(FDateTime::UtcNow().ToUnixTimestamp()));
    if (IFileManager::Get().Copy(*OutBackupPath, *ConfigPath, true, true) != COPY_OK)
    {
        OutError = FString::Printf(TEXT("Failed to back up %s"), *ConfigPath);
        return false;
    }
    return true;
}

FString TomlQuotedString(const FString& Value)
{
    FString Escaped = Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
    return FString::Printf(TEXT("\"%s\""), *Escaped);
}

bool BuildRecommendedMcpConfig(TSharedPtr<FJsonObject>& OutJsonConfig, FString& OutCodexToml, FString& OutError)
{
    FString Command;
    TArray<FString> Args;
    if (HasFabPythonMcpServer())
    {
        const FString McpDir = FPaths::GetPath(GetFabPythonMcpServerPath());
        Command = TEXT("uv");
        Args = { TEXT("--directory"), McpDir, TEXT("run"), TEXT("loomle_mcp_server.py") };
    }
    else
    {
        Command = GetNativeLoomleCliPath();
        if (!FPaths::FileExists(Command))
        {
            OutError = TEXT("Native loomle CLI was not found and Fab Python MCP is unavailable.");
            return false;
        }
        Args = { TEXT("mcp") };
    }

    OutJsonConfig = MakeShared<FJsonObject>();
    OutJsonConfig->SetStringField(TEXT("command"), Command);
    TArray<TSharedPtr<FJsonValue>> JsonArgs;
    TArray<FString> TomlArgs;
    for (const FString& Arg : Args)
    {
        JsonArgs.Add(MakeShared<FJsonValueString>(Arg));
        TomlArgs.Add(TomlQuotedString(Arg));
    }
    OutJsonConfig->SetArrayField(TEXT("args"), JsonArgs);

    OutCodexToml = TEXT("\n[mcp_servers.loomle]\n");
    OutCodexToml += FString::Printf(TEXT("command = %s\n"), *TomlQuotedString(Command));
    OutCodexToml += FString::Printf(TEXT("args = [%s]\n"), *FString::Join(TomlArgs, TEXT(", ")));
    return true;
}

FLoomleHostSetupResult AutoConfigureCodexIfSafe()
{
    FLoomleHostSetupResult Result;
    Result.Host = TEXT("Codex");
    Result.ConfigPath = GetCodexConfigPath();

    const FString ConfigDir = FPaths::GetPath(Result.ConfigPath);
    Result.bDetected = FPaths::DirectoryExists(ConfigDir) || FPaths::FileExists(Result.ConfigPath);
    if (!Result.bDetected)
    {
        Result.Status = TEXT("notDetected");
        Result.Message = TEXT("Codex was not detected.");
        return Result;
    }

    FString RawConfig;
    FFileHelper::LoadFileToString(RawConfig, *Result.ConfigPath);
    Result.ExistingOwner = ClassifyLoomleMcpEntryOwner(RawConfig);
    if (Result.ExistingOwner != ELoomleMcpEntryOwner::None)
    {
        Result.Status = TEXT("keptExisting");
        Result.Message = FString::Printf(TEXT("Codex already has a Loomle MCP entry (%s). Keeping it unchanged."), *LoomleMcpEntryOwnerToString(Result.ExistingOwner));
        return Result;
    }

    TSharedPtr<FJsonObject> JsonConfig;
    FString CodexToml;
    FString Error;
    if (!BuildRecommendedMcpConfig(JsonConfig, CodexToml, Error))
    {
        Result.Status = TEXT("blocked");
        Result.Message = Error;
        Result.bError = true;
        return Result;
    }

    if (!MakeBackupIfNeeded(Result.ConfigPath, Result.BackupPath, Error))
    {
        Result.Status = TEXT("blocked");
        Result.Message = Error;
        Result.bError = true;
        return Result;
    }

    FString Output = RawConfig;
    if (!Output.IsEmpty() && !Output.EndsWith(TEXT("\n")))
    {
        Output += TEXT("\n");
    }
    Output += CodexToml;
    if (!FFileHelper::SaveStringToFile(Output, *Result.ConfigPath))
    {
        Result.Status = TEXT("blocked");
        Result.Message = FString::Printf(TEXT("Failed to write Codex config: %s"), *Result.ConfigPath);
        Result.bError = true;
        return Result;
    }

    Result.Status = TEXT("configured");
    Result.Message = FString::Printf(TEXT("Codex was configured automatically using %s."), *GetSetupPanelMcpServerOwner());
    Result.bChanged = true;
    return Result;
}

FLoomleHostSetupResult AutoConfigureClaudeIfSafe()
{
    FLoomleHostSetupResult Result;
    Result.Host = TEXT("Claude");
    Result.ConfigPath = GetClaudeDesktopConfigPath();

    const FString ConfigDir = FPaths::GetPath(Result.ConfigPath);
    Result.bDetected = FPaths::DirectoryExists(ConfigDir) || FPaths::FileExists(Result.ConfigPath);
    if (!Result.bDetected)
    {
        Result.Status = TEXT("notDetected");
        Result.Message = TEXT("Claude was not detected.");
        return Result;
    }

    FString RawConfig;
    FFileHelper::LoadFileToString(RawConfig, *Result.ConfigPath);
    Result.ExistingOwner = ClassifyLoomleMcpEntryOwner(RawConfig);
    if (Result.ExistingOwner != ELoomleMcpEntryOwner::None)
    {
        Result.Status = TEXT("keptExisting");
        Result.Message = FString::Printf(TEXT("Claude already has a Loomle MCP entry (%s). Keeping it unchanged."), *LoomleMcpEntryOwnerToString(Result.ExistingOwner));
        return Result;
    }

    TSharedPtr<FJsonObject> LoomleConfig;
    FString CodexToml;
    FString Error;
    if (!BuildRecommendedMcpConfig(LoomleConfig, CodexToml, Error))
    {
        Result.Status = TEXT("blocked");
        Result.Message = Error;
        Result.bError = true;
        return Result;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    if (!RawConfig.TrimStartAndEnd().IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawConfig);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            Result.Status = TEXT("blocked");
            Result.Message = FString::Printf(TEXT("Claude config exists but is not valid JSON: %s"), *Result.ConfigPath);
            Result.bError = true;
            return Result;
        }
    }

    const TSharedPtr<FJsonObject>* ExistingMcpServers = nullptr;
    TSharedPtr<FJsonObject> McpServers;
    if (Root->TryGetObjectField(TEXT("mcpServers"), ExistingMcpServers) && ExistingMcpServers != nullptr && (*ExistingMcpServers).IsValid())
    {
        McpServers = *ExistingMcpServers;
    }
    else if (Root->HasField(TEXT("mcpServers")))
    {
        Result.Status = TEXT("blocked");
        Result.Message = TEXT("Claude config mcpServers exists but is not an object.");
        Result.bError = true;
        return Result;
    }
    else
    {
        McpServers = MakeShared<FJsonObject>();
        Root->SetObjectField(TEXT("mcpServers"), McpServers);
    }
    McpServers->SetObjectField(TEXT("loomle"), LoomleConfig);

    if (!MakeBackupIfNeeded(Result.ConfigPath, Result.BackupPath, Error))
    {
        Result.Status = TEXT("blocked");
        Result.Message = Error;
        Result.bError = true;
        return Result;
    }

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Output + TEXT("\n"), *Result.ConfigPath))
    {
        Result.Status = TEXT("blocked");
        Result.Message = FString::Printf(TEXT("Failed to write Claude config: %s"), *Result.ConfigPath);
        Result.bError = true;
        return Result;
    }

    Result.Status = TEXT("configured");
    Result.Message = FString::Printf(TEXT("Claude was configured automatically using %s."), *GetSetupPanelMcpServerOwner());
    Result.bChanged = true;
    return Result;
}

struct FSetupAutoConfigureSummary
{
    TArray<FLoomleHostSetupResult> Hosts;
    bool bAnyDetected = false;
    bool bAnyConfigured = false;
    bool bAnyExisting = false;
    bool bAnyBlocked = false;
};

FSetupAutoConfigureSummary AutoConfigureDetectedHostsIfSafe()
{
    FSetupAutoConfigureSummary Summary;
    Summary.Hosts.Add(AutoConfigureCodexIfSafe());
    Summary.Hosts.Add(AutoConfigureClaudeIfSafe());
    for (const FLoomleHostSetupResult& Host : Summary.Hosts)
    {
        Summary.bAnyDetected = Summary.bAnyDetected || Host.bDetected;
        Summary.bAnyConfigured = Summary.bAnyConfigured || Host.bChanged;
        Summary.bAnyExisting = Summary.bAnyExisting || Host.ExistingOwner != ELoomleMcpEntryOwner::None;
        Summary.bAnyBlocked = Summary.bAnyBlocked || Host.bError;
    }
    return Summary;
}

FString GetLoomlePlatformName()
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

#if PLATFORM_WINDOWS
bool CaptureNativeWindowToColorData(HWND WindowHandle, TArray<FColor>& OutColorData, FIntVector& OutImageSize, FString& OutError)
{
    if (WindowHandle == nullptr)
    {
        OutError = TEXT("The active editor window does not expose a native OS handle.");
        return false;
    }

    RECT WindowRect{};
    if (!::GetWindowRect(WindowHandle, &WindowRect))
    {
        OutError = TEXT("Failed to query the active editor window bounds.");
        return false;
    }

    const int32 CaptureWidth = FMath::Max(1L, WindowRect.right - WindowRect.left);
    const int32 CaptureHeight = FMath::Max(1L, WindowRect.bottom - WindowRect.top);

    HDC WindowDC = ::GetWindowDC(WindowHandle);
    if (WindowDC == nullptr)
    {
        OutError = TEXT("Failed to acquire the active editor window device context.");
        return false;
    }

    HDC MemoryDC = ::CreateCompatibleDC(WindowDC);
    if (MemoryDC == nullptr)
    {
        ::ReleaseDC(WindowHandle, WindowDC);
        OutError = TEXT("Failed to create a compatible device context for screenshot capture.");
        return false;
    }

    BITMAPINFO BitmapInfo{};
    BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    BitmapInfo.bmiHeader.biWidth = CaptureWidth;
    BitmapInfo.bmiHeader.biHeight = -CaptureHeight;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* BitmapPixels = nullptr;
    HBITMAP BitmapHandle = ::CreateDIBSection(WindowDC, &BitmapInfo, DIB_RGB_COLORS, &BitmapPixels, nullptr, 0);
    if (BitmapHandle == nullptr || BitmapPixels == nullptr)
    {
        if (BitmapHandle != nullptr)
        {
            ::DeleteObject(BitmapHandle);
        }
        ::DeleteDC(MemoryDC);
        ::ReleaseDC(WindowHandle, WindowDC);
        OutError = TEXT("Failed to allocate an offscreen bitmap for screenshot capture.");
        return false;
    }

    HGDIOBJ PreviousBitmap = ::SelectObject(MemoryDC, BitmapHandle);

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

    BOOL bCaptured = ::PrintWindow(WindowHandle, MemoryDC, PW_RENDERFULLCONTENT);
    if (!bCaptured)
    {
        bCaptured = ::BitBlt(MemoryDC, 0, 0, CaptureWidth, CaptureHeight, WindowDC, 0, 0, SRCCOPY | CAPTUREBLT);
    }

    if (!bCaptured)
    {
        if (PreviousBitmap != nullptr)
        {
            ::SelectObject(MemoryDC, PreviousBitmap);
        }
        ::DeleteObject(BitmapHandle);
        ::DeleteDC(MemoryDC);
        ::ReleaseDC(WindowHandle, WindowDC);
        OutError = TEXT("Windows failed to capture the active editor window.");
        return false;
    }

    const int32 PixelCount = CaptureWidth * CaptureHeight;
    OutColorData.SetNumUninitialized(PixelCount);
    const uint8* SourceBytes = static_cast<const uint8*>(BitmapPixels);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const uint8* SourcePixel = SourceBytes + (PixelIndex * 4);
        FColor& Dest = OutColorData[PixelIndex];
        Dest.B = SourcePixel[0];
        Dest.G = SourcePixel[1];
        Dest.R = SourcePixel[2];
        Dest.A = SourcePixel[3] == 0 ? 255 : SourcePixel[3];
    }

    OutImageSize = FIntVector(CaptureWidth, CaptureHeight, 0);

    if (PreviousBitmap != nullptr)
    {
        ::SelectObject(MemoryDC, PreviousBitmap);
    }
    ::DeleteObject(BitmapHandle);
    ::DeleteDC(MemoryDC);
    ::ReleaseDC(WindowHandle, WindowDC);
    return true;
}
#endif

TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Source)
{
    if (!Source.IsValid())
    {
        return nullptr;
    }

    FString Serialized;
    {
        const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
        FJsonSerializer::Serialize(Source.ToSharedRef(), Writer);
    }

    TSharedPtr<FJsonObject> Clone = MakeShared<FJsonObject>();
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Serialized);
    if (!FJsonSerializer::Deserialize(Reader, Clone) || !Clone.IsValid())
    {
        return nullptr;
    }
    return Clone;
}

FString SerializeJsonObjectCondensed(const TSharedPtr<FJsonObject>& Source)
{
    if (!Source.IsValid())
    {
        return TEXT("");
    }

    FString Serialized;
    const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
    FJsonSerializer::Serialize(Source.ToSharedRef(), Writer);
    return Serialized;
}

TSharedPtr<FJsonObject> SummarizeSlateWindow(const TSharedPtr<SWindow>& Window)
{
    if (!Window.IsValid())
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetStringField(TEXT("title"), Window->GetTitle().ToString());
    Summary->SetBoolField(TEXT("isModal"), Window->IsModalWindow());
    Summary->SetBoolField(TEXT("isVisible"), Window->IsVisible());
    Summary->SetBoolField(TEXT("isTopmost"), Window->IsTopmostWindow());
    return Summary;
}

TSharedPtr<FJsonObject> BuildGameThreadTimeoutContext(const FString& Scope, const FString& Target, const int32 TimeoutMs)
{
    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("scope"), Scope);
    Context->SetStringField(TEXT("target"), Target);
    Context->SetNumberField(TEXT("timeoutMs"), TimeoutMs);

    if (!IsInGameThread())
    {
        Context->SetBoolField(TEXT("slateContextUnavailable"), true);
        Context->SetStringField(TEXT("slateContextUnavailableReason"), TEXT("NOT_GAME_THREAD"));
        Context->SetStringField(TEXT("suspectedCause"), TEXT("GAME_THREAD_BLOCKED"));
        Context->SetStringField(TEXT("hint"), TEXT("The game thread did not respond before the timeout expired."));
        return Context;
    }

    const bool bSlateInitialized = FSlateApplication::IsInitialized();
    Context->SetBoolField(TEXT("slateInitialized"), bSlateInitialized);
    if (!bSlateInitialized)
    {
        Context->SetStringField(TEXT("suspectedCause"), TEXT("GAME_THREAD_BLOCKED"));
        Context->SetStringField(TEXT("hint"), TEXT("The game thread did not respond before the timeout expired."));
        return Context;
    }

    FSlateApplication& SlateApp = FSlateApplication::Get();

    const TSharedPtr<SWindow> ActiveModalWindow = SlateApp.GetActiveModalWindow();
    Context->SetBoolField(TEXT("hasActiveModalWindow"), ActiveModalWindow.IsValid());
    if (const TSharedPtr<FJsonObject> ModalSummary = SummarizeSlateWindow(ActiveModalWindow))
    {
        Context->SetObjectField(TEXT("activeModalWindow"), ModalSummary);
    }

    const TSharedPtr<SWindow> ActiveTopLevelWindow = SlateApp.GetActiveTopLevelWindow();
    if (const TSharedPtr<FJsonObject> ActiveSummary = SummarizeSlateWindow(ActiveTopLevelWindow))
    {
        Context->SetObjectField(TEXT("activeTopLevelWindow"), ActiveSummary);
    }

    TArray<TSharedRef<SWindow>> VisibleWindows;
    SlateApp.GetAllVisibleWindowsOrdered(VisibleWindows);
    Context->SetNumberField(TEXT("visibleWindowCount"), VisibleWindows.Num());

    TArray<TSharedPtr<FJsonValue>> VisibleWindowSummaries;
    constexpr int32 MaxVisibleWindowSummaries = 5;
    const int32 SummaryCount = FMath::Min(VisibleWindows.Num(), MaxVisibleWindowSummaries);
    for (int32 Index = 0; Index < SummaryCount; ++Index)
    {
        const TSharedPtr<FJsonObject> WindowSummary = SummarizeSlateWindow(VisibleWindows[Index]);
        if (WindowSummary.IsValid())
        {
            VisibleWindowSummaries.Add(MakeShared<FJsonValueObject>(WindowSummary));
        }
    }
    Context->SetArrayField(TEXT("visibleWindows"), VisibleWindowSummaries);

    if (ActiveModalWindow.IsValid())
    {
        Context->SetStringField(TEXT("suspectedCause"), TEXT("EDITOR_MODAL_WINDOW"));
        Context->SetStringField(TEXT("hint"), TEXT("A modal editor window is active and may be blocking the game thread. Dismiss the dialog and retry the request."));
    }
    else
    {
        Context->SetStringField(TEXT("suspectedCause"), TEXT("GAME_THREAD_BLOCKED"));
        Context->SetStringField(TEXT("hint"), TEXT("The game thread did not respond before the timeout expired."));
    }

    return Context;
}

class FLoomleLogCaptureOutputDevice final : public FOutputDevice
{
public:
    explicit FLoomleLogCaptureOutputDevice(TFunction<void(const FString&, ELogVerbosity::Type, const FName&)>&& InOnLine)
        : OnLine(MoveTemp(InOnLine))
    {
    }

    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        if (!OnLine || V == nullptr)
        {
            return;
        }

        OnLine(FString(V).TrimStartAndEnd(), static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask), Category);
    }

private:
    TFunction<void(const FString&, ELogVerbosity::Type, const FName&)> OnLine;
};

uint64 StableFnv1a64(const FString& Input)
{
    constexpr uint64 OffsetBasis = 0xcbf29ce484222325ull;
    constexpr uint64 Prime = 0x100000001b3ull;

    FTCHARToUTF8 Utf8(*Input);
    const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
    uint64 Hash = OffsetBasis;
    for (int32 Index = 0; Index < Utf8.Length(); ++Index)
    {
        Hash ^= static_cast<uint64>(Bytes[Index]);
        Hash *= Prime;
    }
    return Hash;
}

FString GetLoomleHomeDirectory()
{
#if PLATFORM_WINDOWS
    FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
    if (Home.IsEmpty())
    {
        const FString Drive = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEDRIVE"));
        const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEPATH"));
        Home = Drive + Path;
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

#if PLATFORM_WINDOWS
FString NormalizeProjectRootForPipeName()
{
    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeFilename(ProjectRoot);
    while (ProjectRoot.EndsWith(TEXT("/")))
    {
        ProjectRoot.LeftChopInline(1, EAllowShrinking::No);
    }

    if (ProjectRoot.IsEmpty())
    {
        ProjectRoot = TEXT("/");
    }

    ProjectRoot.ToLowerInline();
    return ProjectRoot;
}

FString GetRpcPipeNameForCurrentProject()
{
    const uint64 Hash = StableFnv1a64(NormalizeProjectRootForPipeName());
    return FString::Printf(TEXT("%s-%016llx"), LoomleBridgeConstants::PipeNamePrefix, static_cast<unsigned long long>(Hash));
}
#endif

FString NormalizeGraphType(FString GraphType)
{
    GraphType = GraphType.TrimStartAndEnd().ToLower();
    if (GraphType.IsEmpty())
    {
        return TEXT("blueprint");
    }
    return GraphType;
}

FString GetGraphTypeFromArgs(const TSharedPtr<FJsonObject>& Arguments)
{
    FString GraphType = TEXT("blueprint");
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("graphType"), GraphType);
    }
    return NormalizeGraphType(GraphType);
}

bool IsSupportedGraphType(const FString& GraphType)
{
    return GraphType.Equals(TEXT("blueprint"))
        || GraphType.Equals(TEXT("material"))
        || GraphType.Equals(TEXT("pcg"));
}

FString NormalizeAssetPath(const FString& InAssetPath)
{
    FString AssetPath = InAssetPath;
    const int32 DotIndex = AssetPath.Find(TEXT("."));
    if (DotIndex > 0)
    {
        AssetPath = AssetPath.Left(DotIndex);
    }
    return AssetPath;
}

FString NormalizeBlueprintAssetPath(const FString& InAssetPath)
{
    return NormalizeAssetPath(InAssetPath);
}

UBlueprint* LoadBlueprintByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeBlueprintAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UBlueprint>(nullptr, *ObjectPath);
}

UMaterial* LoadMaterialByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UMaterial>(nullptr, *ObjectPath);
}

UObject* LoadObjectByAssetPath(const FString& InAssetPath)
{
    const FString AssetPath = NormalizeAssetPath(InAssetPath);
    if (!FPackageName::IsValidLongPackageName(AssetPath))
    {
        return nullptr;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
    return LoadObject<UObject>(nullptr, *ObjectPath);
}

UPCGGraph* ResolvePcgGraphFromAsset(UObject* Asset);
UObject* FindEditedMaterialAsset();
UObject* FindEditedPcgAsset();
UWidgetBlueprint* FindEditedWidgetBlueprint();

bool IsLikelyPcgAsset(const UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    return ResolvePcgGraphFromAsset(const_cast<UObject*>(Asset)) != nullptr;
}

UPCGNode* ResolvePcgNodeFromEditorNode(UEdGraphNode* GraphNode);

bool TryGetAssetPathFromObject(const UObject* Object, FString& OutAssetPath)
{
    OutAssetPath.Empty();
    if (Object == nullptr)
    {
        return false;
    }

    const UPackage* Package = Object->GetPackage();
    OutAssetPath = Package ? Package->GetPathName() : Object->GetPathName();
    OutAssetPath = NormalizeAssetPath(OutAssetPath);
    return !OutAssetPath.IsEmpty() && FPackageName::IsValidLongPackageName(OutAssetPath);
}

bool IsTransientAssetPath(const FString& AssetPath)
{
    return AssetPath.StartsWith(TEXT("/Engine/Transient"));
}

TSharedPtr<FJsonObject> MakeAssetGraphRefJson(
    const FString& InAssetPath,
    const FString& InGraphName = FString(),
    const FString& InGraphId = FString())
{
    TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
    GraphRef->SetStringField(TEXT("kind"), TEXT("asset"));
    GraphRef->SetStringField(TEXT("assetPath"), NormalizeAssetPath(InAssetPath));
    if (!InGraphId.IsEmpty())
    {
        GraphRef->SetStringField(TEXT("graphId"), InGraphId);
        GraphRef->SetStringField(TEXT("id"), InGraphId);
    }
    if (!InGraphName.IsEmpty())
    {
        GraphRef->SetStringField(TEXT("graphName"), InGraphName);
    }
    return GraphRef;
}

TSharedPtr<FJsonObject> MakeInlineGraphRefJson(const FString& InAssetPath, const FString& InNodeGuid)
{
    TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
    GraphRef->SetStringField(TEXT("kind"), TEXT("inline"));
    GraphRef->SetStringField(TEXT("assetPath"), NormalizeAssetPath(InAssetPath));
    GraphRef->SetStringField(TEXT("nodeGuid"), InNodeGuid);
    return GraphRef;
}

FString MakeResolvedGraphRefKey(const FString& GraphType, const TSharedPtr<FJsonObject>& GraphRef)
{
    if (!GraphRef.IsValid())
    {
        return GraphType + TEXT("|invalid");
    }

    FString Kind;
    FString AssetPath;
    FString GraphName;
    FString NodeGuid;
    GraphRef->TryGetStringField(TEXT("kind"), Kind);
    GraphRef->TryGetStringField(TEXT("assetPath"), AssetPath);
    GraphRef->TryGetStringField(TEXT("graphName"), GraphName);
    GraphRef->TryGetStringField(TEXT("nodeGuid"), NodeGuid);

    return GraphType + TEXT("|") + Kind + TEXT("|") + NormalizeAssetPath(AssetPath) + TEXT("|") + GraphName + TEXT("|") + NodeGuid;
}

void AddResolvedGraphRefEntry(
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys,
    const FString& GraphType,
    const TSharedPtr<FJsonObject>& GraphRef,
    const FString& Relation,
    const FString& LoadStatus)
{
    if (!GraphRef.IsValid())
    {
        return;
    }

    const FString SeenKey = MakeResolvedGraphRefKey(GraphType, GraphRef);
    if (SeenKeys.Contains(SeenKey))
    {
        return;
    }

    SeenKeys.Add(SeenKey);

    TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("graphType"), GraphType);
    Entry->SetStringField(TEXT("relation"), Relation);
    Entry->SetStringField(TEXT("loadStatus"), LoadStatus);
    Entry->SetObjectField(TEXT("graphRef"), GraphRef);
    OutRefs.Add(MakeShared<FJsonValueObject>(Entry));
}

void SetResolvedGraphRefsFieldIfAny(const TSharedPtr<FJsonObject>& Target, const TArray<TSharedPtr<FJsonValue>>& Refs)
{
    if (Target.IsValid() && Refs.Num() > 0)
    {
        Target->SetArrayField(TEXT("resolvedGraphRefs"), Refs);
    }
}

void CopyResolvedGraphRefEntries(
    const TArray<TSharedPtr<FJsonValue>>& Source,
    TArray<TSharedPtr<FJsonValue>>& Dest,
    TSet<FString>& DestSeenKeys)
{
    for (const TSharedPtr<FJsonValue>& Value : Source)
    {
        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr || !(*EntryObj).IsValid())
        {
            continue;
        }

        FString GraphType;
        FString Relation;
        FString LoadStatus;
        const TSharedPtr<FJsonObject>* GraphRefObj = nullptr;
        (*EntryObj)->TryGetStringField(TEXT("graphType"), GraphType);
        (*EntryObj)->TryGetStringField(TEXT("relation"), Relation);
        (*EntryObj)->TryGetStringField(TEXT("loadStatus"), LoadStatus);
        if (!(*EntryObj)->TryGetObjectField(TEXT("graphRef"), GraphRefObj) || GraphRefObj == nullptr || !(*GraphRefObj).IsValid())
        {
            continue;
        }

        AddResolvedGraphRefEntry(Dest, DestSeenKeys, GraphType, *GraphRefObj, Relation, LoadStatus);
    }
}

void AppendBlueprintRootGraphRefs(
    UBlueprint* Blueprint,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Blueprint == nullptr)
    {
        return;
    }

    FString AssetPath;
    if (!TryGetAssetPathFromObject(Blueprint, AssetPath))
    {
        return;
    }

    auto AddGraphRefForGraph = [&](UEdGraph* Graph)
    {
        if (Graph == nullptr)
        {
            return;
        }
        AddResolvedGraphRefEntry(
            OutRefs,
            SeenKeys,
            TEXT("blueprint"),
            MakeAssetGraphRefJson(AssetPath, Graph->GetName()),
            Relation,
            TEXT("loaded"));
    };

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        AddGraphRefForGraph(Graph);
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        AddGraphRefForGraph(Graph);
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        AddGraphRefForGraph(Graph);
    }
}

void AppendMaterialGraphRefs(
    UObject* MaterialAsset,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    FString AssetPath;
    if (!TryGetAssetPathFromObject(MaterialAsset, AssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        TEXT("material"),
        MakeAssetGraphRefJson(AssetPath),
        Relation,
        TEXT("loaded"));
}

void AppendPcgGraphRefs(
    UObject* PcgAsset,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    UObject* ResolvedPcgAsset = ResolvePcgGraphFromAsset(PcgAsset);
    if (ResolvedPcgAsset == nullptr)
    {
        ResolvedPcgAsset = PcgAsset;
    }

    FString AssetPath;
    if (!TryGetAssetPathFromObject(ResolvedPcgAsset, AssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        TEXT("pcg"),
        MakeAssetGraphRefJson(AssetPath),
        Relation,
        TEXT("loaded"));
}

void AppendPcgComponentGraphRefs(
    UPCGComponent* PcgComponent,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (PcgComponent == nullptr)
    {
        return;
    }

    auto AppendComponentGraphRef = [&](UPCGComponent* SourceComponent)
    {
        if (SourceComponent == nullptr)
        {
            return;
        }

        if (UPCGGraphInstance* GraphInstance = SourceComponent->GetGraphInstance())
        {
            AppendPcgGraphRefs(GraphInstance, Relation, OutRefs, SeenKeys);
            return;
        }

        if (UPCGGraph* Graph = SourceComponent->GetGraph())
        {
            AppendPcgGraphRefs(Graph, Relation, OutRefs, SeenKeys);
        }
    };

    if (UPCGComponent* OriginalComponent = PcgComponent->GetOriginalComponent())
    {
        if (OriginalComponent != PcgComponent)
        {
            AppendComponentGraphRef(OriginalComponent);
        }
    }

    AppendComponentGraphRef(PcgComponent);
}

void AppendSupportedGraphRefsFromAsset(
    UObject* AssetObject,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (AssetObject == nullptr)
    {
        return;
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetObject))
    {
        AppendBlueprintRootGraphRefs(Blueprint, Relation, OutRefs, SeenKeys);
        return;
    }

    if (AssetObject->IsA<UMaterial>() || AssetObject->IsA<UMaterialFunction>())
    {
        AppendMaterialGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
        return;
    }

    if (IsLikelyPcgAsset(AssetObject))
    {
        AppendPcgGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
    }
}

void AppendSoftGraphRefFromPath(
    const FString& GraphType,
    const FString& AssetPath,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    const FString NormalizedAssetPath = NormalizeAssetPath(AssetPath);
    if (NormalizedAssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(NormalizedAssetPath))
    {
        return;
    }

    AddResolvedGraphRefEntry(
        OutRefs,
        SeenKeys,
        GraphType,
        MakeAssetGraphRefJson(NormalizedAssetPath),
        Relation,
        TEXT("not_loaded"));
}

void AppendSupportedGraphRefsFromObjectProperties(
    UObject* SourceObject,
    const FString& Relation,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (SourceObject == nullptr || SourceObject->GetClass() == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectPropertyBase> PropIt(SourceObject->GetClass()); PropIt; ++PropIt)
    {
        FObjectPropertyBase* Prop = *PropIt;
        UObject* PropValue = Prop ? Prop->GetObjectPropertyValue_InContainer(SourceObject) : nullptr;
        AppendSupportedGraphRefsFromAsset(PropValue, Relation, OutRefs, SeenKeys);
    }

    for (TFieldIterator<FSoftObjectProperty> PropIt(SourceObject->GetClass()); PropIt; ++PropIt)
    {
        FSoftObjectProperty* SoftProp = *PropIt;
        if (SoftProp == nullptr || SoftProp->PropertyClass == nullptr)
        {
            continue;
        }

        const FString PropertyClassPath = SoftProp->PropertyClass->GetPathName();
        const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(SourceObject);
        const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
        if (SoftPath.IsNull())
        {
            continue;
        }

        if (PropertyClassPath.Contains(TEXT("PCGGraph")) || PropertyClassPath.Contains(TEXT("/PCG.")))
        {
            const FString AssetPath = NormalizeAssetPath(SoftPath.GetAssetPathString());
            if (AssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(AssetPath))
            {
                continue;
            }

            if (UObject* AssetObject = LoadObjectByAssetPath(AssetPath))
            {
                if (ResolvePcgGraphFromAsset(AssetObject) != nullptr)
                {
                    AppendPcgGraphRefs(AssetObject, Relation, OutRefs, SeenKeys);
                }
            }
            else
            {
                AppendSoftGraphRefFromPath(TEXT("pcg"), AssetPath, Relation, OutRefs, SeenKeys);
            }
        }
        else if (PropertyClassPath.Contains(TEXT("Material")) || PropertyClassPath.Contains(TEXT("MaterialFunction")))
        {
            AppendSoftGraphRefFromPath(TEXT("material"), SoftPath.GetAssetPathString(), Relation, OutRefs, SeenKeys);
        }
        else if (PropertyClassPath.Contains(TEXT("Blueprint")))
        {
            const FString AssetPath = NormalizeAssetPath(SoftPath.GetAssetPathString());
            if (!AssetPath.IsEmpty() && FPackageName::IsValidLongPackageName(AssetPath))
            {
                UObject* AssetObject = LoadObjectByAssetPath(AssetPath);
                if (AssetObject != nullptr)
                {
                    AppendSupportedGraphRefsFromAsset(AssetObject, Relation, OutRefs, SeenKeys);
                }
            }
        }
    }
}

void AppendPcgSubgraphRefsFromNode(
    UPCGNode* PcgNode,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (PcgNode == nullptr)
    {
        return;
    }

    UPCGSettings* NodeSettings = PcgNode->GetSettings();
    if (NodeSettings == nullptr || NodeSettings->GetClass() == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectPropertyBase> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
    {
        FObjectPropertyBase* Prop = *PropIt;
        UObject* PropValue = Prop ? Prop->GetObjectPropertyValue_InContainer(NodeSettings) : nullptr;
        if (PropValue != nullptr && IsLikelyPcgAsset(PropValue))
        {
            AppendPcgGraphRefs(PropValue, TEXT("child"), OutRefs, SeenKeys);
        }
    }

    for (TFieldIterator<FSoftObjectProperty> PropIt(NodeSettings->GetClass()); PropIt; ++PropIt)
    {
        FSoftObjectProperty* SoftProp = *PropIt;
        if (SoftProp == nullptr || SoftProp->PropertyClass == nullptr)
        {
            continue;
        }

        const FString PropertyClassPath = SoftProp->PropertyClass->GetPathName();
        if (!PropertyClassPath.Contains(TEXT("PCGGraph")) && !PropertyClassPath.Contains(TEXT("/PCG.")))
        {
            continue;
        }

        const FSoftObjectPtr SoftPtr = SoftProp->GetPropertyValue_InContainer(NodeSettings);
        const FSoftObjectPath SoftPath = SoftPtr.ToSoftObjectPath();
        if (!SoftPath.IsNull())
        {
            AppendSoftGraphRefFromPath(TEXT("pcg"), SoftPath.GetAssetPathString(), TEXT("child"), OutRefs, SeenKeys);
        }
    }
}

void AppendActorResolvedGraphRefs(
    AActor* Actor,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Actor == nullptr)
    {
        return;
    }

    if (UBlueprint* GeneratedBlueprint = Cast<UBlueprint>(Actor->GetClass() ? Actor->GetClass()->ClassGeneratedBy : nullptr))
    {
        AppendBlueprintRootGraphRefs(GeneratedBlueprint, TEXT("generated_blueprint"), OutRefs, SeenKeys);
    }

    AppendSupportedGraphRefsFromObjectProperties(Actor, TEXT("attached"), OutRefs, SeenKeys);

    TInlineComponentArray<UActorComponent*> Components(Actor);
    for (UActorComponent* Component : Components)
    {
        if (UPCGComponent* PcgComponent = Cast<UPCGComponent>(Component))
        {
            AppendPcgComponentGraphRefs(PcgComponent, TEXT("component_source"), OutRefs, SeenKeys);
        }
        AppendSupportedGraphRefsFromObjectProperties(Component, TEXT("component_source"), OutRefs, SeenKeys);
    }
}

UObject* ResolveRuntimeObjectFromPath(const FString& InObjectPath)
{
    const FString ObjectPath = InObjectPath.TrimStartAndEnd();
    if (ObjectPath.IsEmpty())
    {
        return nullptr;
    }

    if (UObject* FoundObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath))
    {
        return FoundObject;
    }

    if (ObjectPath.StartsWith(TEXT("/")))
    {
        if (ObjectPath.Contains(TEXT(".")))
        {
            if (UObject* LoadedObject = LoadObject<UObject>(nullptr, *ObjectPath))
            {
                return LoadedObject;
            }
        }

        return LoadObjectByAssetPath(ObjectPath);
    }

    return nullptr;
}

void AppendResolvedGraphRefsFromObject(
    UObject* Object,
    TArray<TSharedPtr<FJsonValue>>& OutRefs,
    TSet<FString>& SeenKeys)
{
    if (Object == nullptr)
    {
        return;
    }

    if (AActor* Actor = Cast<AActor>(Object))
    {
        AppendActorResolvedGraphRefs(Actor, OutRefs, SeenKeys);
        return;
    }

    if (UActorComponent* Component = Cast<UActorComponent>(Object))
    {
        if (UPCGComponent* PcgComponent = Cast<UPCGComponent>(Component))
        {
            AppendPcgComponentGraphRefs(PcgComponent, TEXT("component_source"), OutRefs, SeenKeys);
        }
        AppendSupportedGraphRefsFromObjectProperties(Component, TEXT("component_source"), OutRefs, SeenKeys);
        return;
    }

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
    {
        AppendBlueprintRootGraphRefs(Blueprint, TEXT("direct_asset"), OutRefs, SeenKeys);
        return;
    }

    if (UEdGraph* Graph = Cast<UEdGraph>(Object))
    {
        if (UBlueprint* Blueprint = Graph->GetTypedOuter<UBlueprint>())
        {
            FString BlueprintAssetPath;
            if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
            {
                AddResolvedGraphRefEntry(
                    OutRefs,
                    SeenKeys,
                    TEXT("blueprint"),
                    MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                    TEXT("selected_graph"),
                    TEXT("loaded"));
            }
        }
        return;
    }

    if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
    {
        if (UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(GraphNode))
        {
            if (MaterialGraphNode->MaterialExpression != nullptr)
            {
                AppendResolvedGraphRefsFromObject(MaterialGraphNode->MaterialExpression, OutRefs, SeenKeys);
                return;
            }
        }

        if (UPCGNode* PcgNode = ResolvePcgNodeFromEditorNode(GraphNode))
        {
            AppendResolvedGraphRefsFromObject(PcgNode, OutRefs, SeenKeys);
            return;
        }

        if (UBlueprint* Blueprint = GraphNode->GetTypedOuter<UBlueprint>())
        {
            FString BlueprintAssetPath;
            if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
            {
                if (UEdGraph* Graph = GraphNode->GetGraph())
                {
                    AddResolvedGraphRefEntry(
                        OutRefs,
                        SeenKeys,
                        TEXT("blueprint"),
                        MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                        TEXT("selected_graph"),
                        TEXT("loaded"));
                }

                FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(GraphNode->GetClass(), TEXT("BoundGraph"));
                if (BoundGraphProp != nullptr)
                {
                    UEdGraph* BoundGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(GraphNode));
                    if (BoundGraph != nullptr)
                    {
                        AddResolvedGraphRefEntry(
                            OutRefs,
                            SeenKeys,
                            TEXT("blueprint"),
                            MakeInlineGraphRefJson(
                                BlueprintAssetPath,
                                GraphNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)),
                            TEXT("child"),
                            TEXT("loaded"));
                    }
                }
            }
        }
        return;
    }

    if (UMaterialExpression* Expression = Cast<UMaterialExpression>(Object))
    {
        UObject* MaterialOwner = Expression->GetTypedOuter<UMaterial>();
        FString MaterialOwnerAssetPath;
        if (MaterialOwner != nullptr
            && (!TryGetAssetPathFromObject(MaterialOwner, MaterialOwnerAssetPath)
                || IsTransientAssetPath(MaterialOwnerAssetPath)))
        {
            MaterialOwner = nullptr;
        }
        if (MaterialOwner == nullptr)
        {
            MaterialOwner = Expression->GetTypedOuter<UMaterialFunction>();
            if (MaterialOwner != nullptr
                && (!TryGetAssetPathFromObject(MaterialOwner, MaterialOwnerAssetPath)
                    || IsTransientAssetPath(MaterialOwnerAssetPath)))
            {
                MaterialOwner = nullptr;
            }
        }
        if (MaterialOwner == nullptr)
        {
            MaterialOwner = FindEditedMaterialAsset();
        }
        AppendMaterialGraphRefs(MaterialOwner, TEXT("selected_graph"), OutRefs, SeenKeys);

        if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            AppendMaterialGraphRefs(FuncCall->MaterialFunction, TEXT("child"), OutRefs, SeenKeys);
        }
        return;
    }

    if (UPCGNode* PcgNode = Cast<UPCGNode>(Object))
    {
        UObject* PcgAsset = nullptr;
        if (UPCGGraph* Graph = PcgNode->GetTypedOuter<UPCGGraph>())
        {
            PcgAsset = ResolvePcgGraphFromAsset(Graph);
            FString PcgAssetPath;
            if (PcgAsset == nullptr
                || !TryGetAssetPathFromObject(PcgAsset, PcgAssetPath)
                || IsTransientAssetPath(PcgAssetPath))
            {
                PcgAsset = nullptr;
            }
        }
        if (PcgAsset == nullptr)
        {
            PcgAsset = FindEditedPcgAsset();
        }
        if (PcgAsset != nullptr)
        {
            AppendPcgGraphRefs(PcgAsset, TEXT("selected_graph"), OutRefs, SeenKeys);
        }
        AppendPcgSubgraphRefsFromNode(PcgNode, OutRefs, SeenKeys);
        return;
    }

    AppendSupportedGraphRefsFromAsset(Object, TEXT("direct_asset"), OutRefs, SeenKeys);
    AppendSupportedGraphRefsFromObjectProperties(Object, TEXT("attached"), OutRefs, SeenKeys);
}

void FilterResolvedGraphRefsByType(
    const TArray<TSharedPtr<FJsonValue>>& InRefs,
    const FString& GraphTypeFilter,
    TArray<TSharedPtr<FJsonValue>>& OutRefs)
{
    OutRefs.Reset();
    const FString NormalizedFilter = GraphTypeFilter.TrimStartAndEnd().ToLower();
    if (NormalizedFilter.IsEmpty())
    {
        OutRefs = InRefs;
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : InRefs)
    {
        const TSharedPtr<FJsonObject>* EntryObj = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(EntryObj) || EntryObj == nullptr || !(*EntryObj).IsValid())
        {
            continue;
        }

        FString EntryGraphType;
        (*EntryObj)->TryGetStringField(TEXT("graphType"), EntryGraphType);
        if (EntryGraphType.Equals(NormalizedFilter))
        {
            OutRefs.Add(Value);
        }
    }
}

UPCGGraph* ResolvePcgGraphFromAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (UPCGGraph* Graph = Cast<UPCGGraph>(Asset))
    {
        return Graph;
    }

    if (UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(Asset))
    {
        return GraphInterface->GetMutablePCGGraph();
    }

    return nullptr;
}

UPCGGraph* LoadPcgGraphByAssetPath(const FString& InAssetPath)
{
    return ResolvePcgGraphFromAsset(LoadObjectByAssetPath(InAssetPath));
}

UPCGNode* FindPcgNodeById(UPCGGraph* Graph, const FString& NodeId)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }

    for (UPCGNode* Node : Graph->GetNodes())
    {
        if (Node == nullptr)
        {
            continue;
        }

        if (Node->GetPathName().Equals(NodeId)
            || Node->GetName().Equals(NodeId)
            || Node->NodeTitle.ToString().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }

    return nullptr;
}

UPCGPin* FindPcgPin(UPCGNode* Node, const FString& PinName, bool bOutputPin)
{
    if (Node == nullptr || PinName.IsEmpty())
    {
        return nullptr;
    }

    auto NormalizePinToken = [](const FString& Token) -> FString
    {
        FString Out;
        Out.Reserve(Token.Len());
        for (const TCHAR Character : Token)
        {
            if (Character == TEXT('_') || Character == TEXT('-') || FChar::IsWhitespace(Character))
            {
                continue;
            }

            Out.AppendChar(FChar::ToLower(Character));
        }
        return Out;
    };

    const FString NormalizedRequestedPin = NormalizePinToken(PinName);
    const TArray<TObjectPtr<UPCGPin>>& Pins = bOutputPin ? Node->GetOutputPins() : Node->GetInputPins();
    for (UPCGPin* Pin : Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }

        const FString Label = Pin->Properties.Label.ToString();
        if (Label.Equals(PinName, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
        if (!NormalizedRequestedPin.IsEmpty()
            && NormalizePinToken(Label).Equals(NormalizedRequestedPin, ESearchCase::CaseSensitive))
        {
            return Pin;
        }
    }

    return nullptr;
}

const FPCGSettingsOverridableParam* FindPcgOverridableParamByPinLabel(UPCGSettings* Settings, const FName PinLabel)
{
    if (Settings == nullptr || PinLabel.IsNone())
    {
        return nullptr;
    }

    for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
    {
        if (Param.Label == PinLabel)
        {
            return &Param;
        }
    }

    return nullptr;
}

FString NormalizePcgPathToken(const FString& Token)
{
    FString Out;
    Out.Reserve(Token.Len());
    for (const TCHAR Character : Token)
    {
        if (Character == TEXT('_') || Character == TEXT('-') || FChar::IsWhitespace(Character))
        {
            continue;
        }

        Out.AppendChar(FChar::ToLower(Character));
    }

    return Out;
}

FProperty* FindPcgPropertyByPathToken(UStruct* OwnerStruct, const FString& PathToken)
{
    if (OwnerStruct == nullptr || PathToken.IsEmpty())
    {
        return nullptr;
    }

    const FString NormalizedToken = NormalizePcgPathToken(PathToken);
    for (TFieldIterator<FProperty> It(OwnerStruct, EFieldIterationFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property == nullptr)
        {
            continue;
        }

        if (NormalizePcgPathToken(Property->GetName()).Equals(NormalizedToken, ESearchCase::CaseSensitive))
        {
            return Property;
        }
    }

    return nullptr;
}

bool IsPcgSelectorStruct(const UStruct* Struct)
{
    return Struct == FPCGAttributePropertySelector::StaticStruct()
        || Struct == FPCGAttributePropertyInputSelector::StaticStruct()
        || Struct == FPCGAttributePropertyOutputNoSourceSelector::StaticStruct()
        || Struct == FPCGAttributePropertyOutputSelector::StaticStruct();
}

bool IsPcgGraphReferenceProperty(const FProperty* Property);
bool ValidatePcgGraphAssetReferenceValue(const FString& Value, FString& OutError);

bool TryNormalizePcgSelectorJsonString(const FString& Value, FString& OutSelectorText)
{
    OutSelectorText.Empty();

    const FString TrimmedValue = Value.TrimStartAndEnd();
    if (!TrimmedValue.StartsWith(TEXT("{")))
    {
        return false;
    }

    TSharedPtr<FJsonObject> SelectorObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedValue);
    if (!FJsonSerializer::Deserialize(Reader, SelectorObject) || !SelectorObject.IsValid())
    {
        return false;
    }

    FString Name;
    FString Selection;
    FString Domain;
    FString TextValue;
    SelectorObject->TryGetStringField(TEXT("name"), Name);
    SelectorObject->TryGetStringField(TEXT("selection"), Selection);
    SelectorObject->TryGetStringField(TEXT("domain"), Domain);
    SelectorObject->TryGetStringField(TEXT("text"), TextValue);

    TArray<FString> Accessors;
    const TArray<TSharedPtr<FJsonValue>>* AccessorValues = nullptr;
    if (SelectorObject->TryGetArrayField(TEXT("accessors"), AccessorValues) && AccessorValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& AccessorValue : *AccessorValues)
        {
            FString Accessor;
            if (AccessorValue.IsValid() && AccessorValue->TryGetString(Accessor) && !Accessor.IsEmpty())
            {
                Accessors.Add(Accessor);
            }
        }
    }
    else
    {
        FString AccessorPath;
        if (SelectorObject->TryGetStringField(TEXT("accessorPath"), AccessorPath) && !AccessorPath.IsEmpty())
        {
            AccessorPath.ParseIntoArray(Accessors, TEXT("."), true);
        }
    }

    if (Name.IsEmpty())
    {
        OutSelectorText = TextValue;
        return !OutSelectorText.IsEmpty();
    }

    FString BaseName = Name;
    if (Selection.Equals(TEXT("Property"), ESearchCase::IgnoreCase)
        || Selection.Equals(TEXT("ExtraProperty"), ESearchCase::IgnoreCase))
    {
        if (!BaseName.StartsWith(TEXT("$")))
        {
            BaseName = TEXT("$") + BaseName;
        }
    }

    if (!Domain.IsEmpty())
    {
        OutSelectorText += TEXT("@");
        OutSelectorText += Domain;
        OutSelectorText += TEXT(".");
    }
    OutSelectorText += BaseName;
    for (const FString& Accessor : Accessors)
    {
        OutSelectorText += TEXT(".");
        OutSelectorText += Accessor;
    }

    return !OutSelectorText.IsEmpty();
}

bool SetPcgSelectorStructValue(FStructProperty* StructProperty, void* ValuePtr, const FString& Value, FString& OutError)
{
    if (StructProperty == nullptr || ValuePtr == nullptr || !IsPcgSelectorStruct(StructProperty->Struct))
    {
        return false;
    }

    FPCGAttributePropertySelector* Selector = reinterpret_cast<FPCGAttributePropertySelector*>(ValuePtr);
    if (Selector == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Invalid PCG default value target: selector '%s' storage is unavailable."),
            *StructProperty->GetName());
        return false;
    }

    FString NormalizedValue;
    const FString TrimmedValue = TryNormalizePcgSelectorJsonString(Value, NormalizedValue)
        ? NormalizedValue.TrimStartAndEnd()
        : Value.TrimStartAndEnd();
    if (TrimmedValue.IsEmpty())
    {
        Selector->Reset();
        return true;
    }

    if (!Selector->Update(TrimmedValue))
    {
        Selector->SetAttributeName(*TrimmedValue);
    }

    if (!Selector->IsValid())
    {
        OutError = FString::Printf(
            TEXT("Invalid PCG selector value for '%s': %s"),
            *StructProperty->GetName(),
            *Value);
        return false;
    }

    return true;
}

bool ResolvePcgPropertyPathTarget(
    void* RootContainer,
    UStruct* RootStruct,
    const TArray<FString>& PathSegments,
    void*& OutContainer,
    FProperty*& OutLeafProperty,
    FString& OutError)
{
    OutContainer = nullptr;
    OutLeafProperty = nullptr;

    if (RootContainer == nullptr || RootStruct == nullptr || PathSegments.Num() == 0)
    {
        OutError = TEXT("Invalid PCG default value target: property path is empty.");
        return false;
    }

    void* CurrentContainer = RootContainer;
    UStruct* CurrentStruct = RootStruct;
    for (int32 SegmentIndex = 0; SegmentIndex < PathSegments.Num(); ++SegmentIndex)
    {
        const FString& Segment = PathSegments[SegmentIndex];
        FProperty* Property = FindPcgPropertyByPathToken(CurrentStruct, Segment);
        if (Property == nullptr)
        {
            OutError = FString::Printf(
                TEXT("Invalid PCG default value target: property path segment '%s' was not found."),
                *Segment);
            return false;
        }

        const bool bIsLeaf = SegmentIndex == (PathSegments.Num() - 1);
        if (bIsLeaf)
        {
            OutContainer = CurrentContainer;
            OutLeafProperty = Property;
            return true;
        }

        if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            CurrentContainer = StructProperty->ContainerPtrToValuePtr<void>(CurrentContainer);
            CurrentStruct = StructProperty->Struct;
        }
        else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
        {
            UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(CurrentContainer);
            if (NextObject == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Invalid PCG default value target: object property '%s' is null."),
                    *ObjectProperty->GetName());
                return false;
            }

            CurrentContainer = NextObject;
            CurrentStruct = NextObject->GetClass();
        }
        else
        {
            OutError = FString::Printf(
                TEXT("Invalid PCG default value target: property '%s' is not a nested container."),
                *Property->GetName());
            return false;
        }
    }

    OutError = TEXT("Invalid PCG default value target: property path is empty.");
    return false;
}

bool SetPcgPropertyValueFromString(
    UPCGSettings* Settings,
    const FString& TargetName,
    void* Container,
    FProperty* LeafProperty,
    const FString& Value,
    FString& OutError)
{
    if (Settings == nullptr || Container == nullptr || LeafProperty == nullptr)
    {
        OutError = TEXT("Invalid PCG default value target: property storage is unavailable.");
        return false;
    }

    void* ValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(Container);
    if (ValuePtr == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Invalid PCG default value target: could not resolve value storage for '%s'."),
            *LeafProperty->GetName());
        return false;
    }

    if (FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty))
    {
        if (SetPcgSelectorStructValue(StructProperty, ValuePtr, Value, OutError))
        {
            return true;
        }
    }

    if (IsPcgGraphReferenceProperty(LeafProperty) && !ValidatePcgGraphAssetReferenceValue(Value, OutError))
    {
        return false;
    }

    const TCHAR* ImportResult = LeafProperty->ImportText_Direct(*Value, ValuePtr, Settings, PPF_None);
    if (ImportResult == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Invalid PCG default value for '%s': %s"),
            *TargetName,
            *Value);
        return false;
    }

    return true;
}

bool ResolvePcgOverridableValueTarget(
    UPCGSettings* Settings,
    const FPCGSettingsOverridableParam& Param,
    void*& OutContainer,
    FProperty*& OutLeafProperty,
    FString& OutError)
{
    OutContainer = nullptr;
    OutLeafProperty = nullptr;

    if (Settings == nullptr)
    {
        OutError = TEXT("Invalid PCG default value target: settings object is missing.");
        return false;
    }

    if (Param.Properties.IsEmpty())
    {
        OutError = TEXT("Invalid PCG default value target: overridable property metadata is unavailable.");
        return false;
    }

    OutContainer = Settings;

    for (int32 PropertyIndex = 0; PropertyIndex < Param.Properties.Num() - 1; ++PropertyIndex)
    {
        FProperty* CurrentProperty = const_cast<FProperty*>(Param.Properties[PropertyIndex]);
        if (CurrentProperty == nullptr)
        {
            OutError = TEXT("Invalid PCG default value target: encountered a null property in the override chain.");
            return false;
        }

        if (FStructProperty* StructProperty = CastField<FStructProperty>(CurrentProperty))
        {
            void* NextContainer = StructProperty->ContainerPtrToValuePtr<void>(OutContainer);
            if (NextContainer == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Invalid PCG default value target: struct property '%s' could not be resolved."),
                    *StructProperty->GetName());
                return false;
            }

            OutContainer = NextContainer;
        }
        else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(CurrentProperty))
        {
            UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(OutContainer);
            if (NextObject == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Invalid PCG default value target: object property '%s' is null."),
                    *ObjectProperty->GetName());
                return false;
            }

            OutContainer = NextObject;
        }
        else
        {
            OutError = FString::Printf(
                TEXT("Invalid PCG default value target: unsupported nested property type '%s'."),
                *CurrentProperty->GetClass()->GetName());
            return false;
        }
    }

    OutLeafProperty = const_cast<FProperty*>(Param.Properties.Last());
    if (OutLeafProperty == nullptr)
    {
        OutError = TEXT("Invalid PCG default value target: missing leaf property.");
        return false;
    }

    return true;
}

bool SetPcgOverridableValueFromString(
    UPCGSettings* Settings,
    const FPCGSettingsOverridableParam& Param,
    const FString& Value,
    FString& OutError)
{
    void* Container = nullptr;
    FProperty* LeafProperty = nullptr;
    if (!ResolvePcgOverridableValueTarget(Settings, Param, Container, LeafProperty, OutError))
    {
        return false;
    }

    return SetPcgPropertyValueFromString(Settings, Param.Label.ToString(), Container, LeafProperty, Value, OutError);
}

bool IsPcgGraphReferenceProperty(const FProperty* Property)
{
    if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
    {
        return SoftObjectProperty->PropertyClass != nullptr
            && (SoftObjectProperty->PropertyClass->IsChildOf(UPCGGraph::StaticClass())
                || SoftObjectProperty->PropertyClass->IsChildOf(UPCGGraphInterface::StaticClass()));
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        return ObjectProperty->PropertyClass != nullptr
            && (ObjectProperty->PropertyClass->IsChildOf(UPCGGraph::StaticClass())
                || ObjectProperty->PropertyClass->IsChildOf(UPCGGraphInterface::StaticClass()));
    }

    return false;
}

FString NormalizePcgGraphAssetReference(const FString& Value)
{
    FString Normalized = Value.TrimStartAndEnd();
    Normalized.RemoveFromStart(TEXT("\""));
    Normalized.RemoveFromEnd(TEXT("\""));
    if (Normalized.IsEmpty())
    {
        return FString();
    }

    if (!Normalized.Contains(TEXT(".")) && Normalized.StartsWith(TEXT("/")))
    {
        const FString AssetName = FPackageName::GetShortName(Normalized);
        if (!AssetName.IsEmpty())
        {
            Normalized += TEXT(".");
            Normalized += AssetName;
        }
    }

    return Normalized;
}

bool ValidatePcgGraphAssetReferenceValue(const FString& Value, FString& OutError)
{
    const FString ObjectPath = NormalizePcgGraphAssetReference(Value);
    if (ObjectPath.IsEmpty())
    {
        return true;
    }

    UObject* LoadedGraph = StaticLoadObject(UPCGGraphInterface::StaticClass(), nullptr, *ObjectPath);
    if (LoadedGraph == nullptr)
    {
        OutError = FString::Printf(TEXT("Missing PCG graph asset: %s"), *Value);
        return false;
    }

    return true;
}

struct FPcgQuerySyntheticTarget
{
    FString PinName;
    FString DefaultValue;
    FString DefaultText;
};

bool TryReadPcgPropertyValueForQuery(void* Container, FProperty* LeafProperty, FString& OutValue, FString& OutText);
bool TryReadPcgOverridableValueForQuery(
    UPCGSettings* Settings,
    const FPCGSettingsOverridableParam& Param,
    FString& OutValue,
    FString& OutText,
    FString& OutError);

void AddPcgQuerySyntheticTarget(
    TArray<FPcgQuerySyntheticTarget>& OutTargets,
    TSet<FString>& SeenPinNames,
    const FString& PinName,
    const FString& DefaultValue,
    const FString& DefaultText)
{
    if (PinName.IsEmpty() || SeenPinNames.Contains(PinName))
    {
        return;
    }

    SeenPinNames.Add(PinName);
    FPcgQuerySyntheticTarget& Target = OutTargets.AddDefaulted_GetRef();
    Target.PinName = PinName;
    Target.DefaultValue = DefaultValue;
    Target.DefaultText = DefaultText;
}

bool ShouldSurfacePcgSyntheticLeafProperty(const FProperty* Property)
{
    if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit))
    {
        return false;
    }

    if (Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Deprecated | CPF_Transient))
    {
        return false;
    }

    if (Property->GetName().EndsWith(TEXT("_DEPRECATED")))
    {
        return false;
    }

    if (CastField<FArrayProperty>(Property) != nullptr
        || CastField<FSetProperty>(Property) != nullptr
        || CastField<FMapProperty>(Property) != nullptr)
    {
        return false;
    }

    return CastField<FBoolProperty>(Property) != nullptr
        || CastField<FEnumProperty>(Property) != nullptr
        || CastField<FNumericProperty>(Property) != nullptr
        || CastField<FStrProperty>(Property) != nullptr
        || CastField<FNameProperty>(Property) != nullptr
        || CastField<FTextProperty>(Property) != nullptr
        || CastField<FObjectPropertyBase>(Property) != nullptr
        || CastField<FSoftObjectProperty>(Property) != nullptr
        || (CastField<FStructProperty>(Property) != nullptr
            && IsPcgSelectorStruct(CastField<FStructProperty>(Property)->Struct));
}

void GatherPcgOverridableTargetsForQuery(
    UPCGSettings* Settings,
    TArray<FPcgQuerySyntheticTarget>& OutTargets,
    TSet<FString>& SeenPinNames)
{
    if (Settings == nullptr)
    {
        return;
    }

    for (const FPCGSettingsOverridableParam& Param : Settings->OverridableParams())
    {
        const FString PinName = Param.Label.ToString();
        if (PinName.IsEmpty() || SeenPinNames.Contains(PinName))
        {
            continue;
        }

        FString DefaultValue;
        FString DefaultText;
        FString ReadError;
        if (TryReadPcgOverridableValueForQuery(Settings, Param, DefaultValue, DefaultText, ReadError))
        {
            AddPcgQuerySyntheticTarget(OutTargets, SeenPinNames, PinName, DefaultValue, DefaultText);
        }
    }
}

void GatherPcgEditablePropertyTargetsForQuery(
    UPCGSettings* Settings,
    TArray<FPcgQuerySyntheticTarget>& OutTargets,
    TSet<FString>& SeenPinNames)
{
    if (Settings == nullptr)
    {
        return;
    }

    for (UStruct* CurrentStruct = Settings->GetClass();
         CurrentStruct != nullptr && CurrentStruct != UPCGSettings::StaticClass();
         CurrentStruct = CurrentStruct->GetSuperStruct())
    {
        for (TFieldIterator<FProperty> It(CurrentStruct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (!ShouldSurfacePcgSyntheticLeafProperty(Property))
            {
                continue;
            }

            const FString PinName = Property->GetName();
            if (PinName.IsEmpty() || SeenPinNames.Contains(PinName))
            {
                continue;
            }

            FString DefaultValue;
            FString DefaultText;
            if (!TryReadPcgPropertyValueForQuery(Settings, Property, DefaultValue, DefaultText))
            {
                continue;
            }

            AddPcgQuerySyntheticTarget(OutTargets, SeenPinNames, PinName, DefaultValue, DefaultText);
        }
    }
}

void GatherPcgSyntheticPropertyTargetsForQuery(UPCGSettings* Settings, TArray<FPcgQuerySyntheticTarget>& OutTargets)
{
    OutTargets.Reset();
    if (Settings == nullptr)
    {
        return;
    }

    TSet<FString> SeenPinNames;
    GatherPcgOverridableTargetsForQuery(Settings, OutTargets, SeenPinNames);
    GatherPcgEditablePropertyTargetsForQuery(Settings, OutTargets, SeenPinNames);
    Algo::SortBy(OutTargets, &FPcgQuerySyntheticTarget::PinName);
}

bool FindPcgSyntheticPropertyTargetForQuery(
    UPCGSettings* Settings,
    const FString& PinName,
    FPcgQuerySyntheticTarget& OutTarget)
{
    if (Settings == nullptr || PinName.IsEmpty())
    {
        return false;
    }

    const FString NormalizedRequestedPin = NormalizePcgPathToken(PinName);
    TArray<FPcgQuerySyntheticTarget> SyntheticTargets;
    GatherPcgSyntheticPropertyTargetsForQuery(Settings, SyntheticTargets);
    for (const FPcgQuerySyntheticTarget& SyntheticTarget : SyntheticTargets)
    {
        if (SyntheticTarget.PinName.Equals(PinName, ESearchCase::IgnoreCase)
            || NormalizePcgPathToken(SyntheticTarget.PinName).Equals(NormalizedRequestedPin, ESearchCase::CaseSensitive))
        {
            OutTarget = SyntheticTarget;
            return true;
        }
    }

    return false;
}

bool TryReadPcgPropertyValueForQuery(void* Container, FProperty* LeafProperty, FString& OutValue, FString& OutText)
{
    OutValue.Empty();
    OutText.Empty();
    if (Container == nullptr || LeafProperty == nullptr)
    {
        return false;
    }

    void* ValuePtr = LeafProperty->ContainerPtrToValuePtr<void>(Container);
    if (ValuePtr == nullptr)
    {
        return false;
    }

    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(LeafProperty))
    {
        OutValue = BoolProperty->GetPropertyValue(ValuePtr) ? TEXT("true") : TEXT("false");
        OutText = OutValue;
        return true;
    }

    if (FStrProperty* StrProperty = CastField<FStrProperty>(LeafProperty))
    {
        OutValue = StrProperty->GetPropertyValue(ValuePtr);
        OutText = OutValue;
        return true;
    }

    if (FNameProperty* NameProperty = CastField<FNameProperty>(LeafProperty))
    {
        OutValue = NameProperty->GetPropertyValue(ValuePtr).ToString();
        OutText = OutValue;
        return true;
    }

    if (FTextProperty* TextProperty = CastField<FTextProperty>(LeafProperty))
    {
        OutValue = TextProperty->GetPropertyValue(ValuePtr).ToString();
        OutText = OutValue;
        return true;
    }

    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(LeafProperty))
    {
        const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
        const UEnum* Enum = EnumProperty->GetEnum();
        OutValue = LexToString(EnumValue);
        OutText = Enum ? Enum->GetNameStringByValue(EnumValue) : OutValue;
        return true;
    }

    if (FByteProperty* ByteProperty = CastField<FByteProperty>(LeafProperty))
    {
        const uint8 ByteValue = ByteProperty->GetPropertyValue(ValuePtr);
        OutValue = LexToString(ByteValue);
        OutText = ByteProperty->Enum ? ByteProperty->Enum->GetNameStringByValue(ByteValue) : OutValue;
        return true;
    }

    if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(LeafProperty))
    {
        const FSoftObjectPath ObjectPath = SoftObjectProperty->GetPropertyValue_InContainer(Container).ToSoftObjectPath();
        OutValue = ObjectPath.IsNull() ? FString() : ObjectPath.GetAssetPathString();
        OutText = OutValue;
        return true;
    }

    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(LeafProperty))
    {
        UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue_InContainer(Container);
        if (ObjectValue == nullptr)
        {
            return true;
        }

        UPackage* Package = ObjectValue->GetPackage();
        OutValue = Package ? Package->GetPathName() : ObjectValue->GetPathName();
        OutText = OutValue;
        return true;
    }

    if (FStructProperty* StructProperty = CastField<FStructProperty>(LeafProperty))
    {
        if (IsPcgSelectorStruct(StructProperty->Struct))
        {
            const FPCGAttributePropertySelector* Selector = reinterpret_cast<const FPCGAttributePropertySelector*>(ValuePtr);
            OutValue = Selector ? Selector->ToString() : FString();
            OutText = OutValue;
            return true;
        }
    }

    LeafProperty->ExportTextItem_Direct(OutValue, ValuePtr, nullptr, nullptr, PPF_None);
    OutText = OutValue;
    return true;
}

bool TryReadPcgPropertyValueToString(void* Container, FProperty* LeafProperty, FString& OutValue)
{
    FString IgnoredText;
    return TryReadPcgPropertyValueForQuery(Container, LeafProperty, OutValue, IgnoredText);
}

bool TryReadPcgOverridableValueToString(
    UPCGSettings* Settings,
    const FPCGSettingsOverridableParam& Param,
    FString& OutValue,
    FString& OutError)
{
    void* Container = nullptr;
    FProperty* LeafProperty = nullptr;
    if (!ResolvePcgOverridableValueTarget(Settings, Param, Container, LeafProperty, OutError))
    {
        return false;
    }

    return TryReadPcgPropertyValueToString(Container, LeafProperty, OutValue);
}

bool TryReadPcgOverridableValueForQuery(
    UPCGSettings* Settings,
    const FPCGSettingsOverridableParam& Param,
    FString& OutValue,
    FString& OutText,
    FString& OutError)
{
    void* Container = nullptr;
    FProperty* LeafProperty = nullptr;
    if (!ResolvePcgOverridableValueTarget(Settings, Param, Container, LeafProperty, OutError))
    {
        return false;
    }

    return TryReadPcgPropertyValueForQuery(Container, LeafProperty, OutValue, OutText);
}

bool TryReadPcgPinDefaultValueForQuery(UPCGNode* Node, const FString& PinName, FString& OutValue, FString& OutText)
{
    OutValue.Empty();
    OutText.Empty();
    if (Node == nullptr || PinName.IsEmpty())
    {
        return false;
    }

    UPCGSettings* Settings = Node->GetSettings();
    if (Settings == nullptr)
    {
        return false;
    }

    if (UPCGPin* TargetPin = FindPcgPin(Node, PinName, false))
    {
        const FPCGSettingsOverridableParam* Param = FindPcgOverridableParamByPinLabel(Settings, TargetPin->Properties.Label);
        if (Param != nullptr)
        {
            FString ReadError;
            if (TryReadPcgOverridableValueForQuery(Settings, *Param, OutValue, OutText, ReadError))
            {
                return true;
            }
        }
    }

    FPcgQuerySyntheticTarget SyntheticTarget;
    if (FindPcgSyntheticPropertyTargetForQuery(Settings, PinName, SyntheticTarget))
    {
        OutValue = SyntheticTarget.DefaultValue;
        OutText = SyntheticTarget.DefaultText;
        return true;
    }

    TArray<FString> PathSegments;
    PinName.ParseIntoArray(PathSegments, TEXT("/"), true);
    if (PathSegments.Num() == 0)
    {
        return false;
    }

    void* Container = nullptr;
    FProperty* LeafProperty = nullptr;
    FString ResolveError;
    if (!ResolvePcgPropertyPathTarget(Settings, Settings->GetClass(), PathSegments, Container, LeafProperty, ResolveError))
    {
        return false;
    }

    return TryReadPcgPropertyValueForQuery(Container, LeafProperty, OutValue, OutText);
}

bool TryReadPcgPinDefaultValue(UPCGNode* Node, const FString& PinName, FString& OutValue)
{
    FString IgnoredText;
    return TryReadPcgPinDefaultValueForQuery(Node, PinName, OutValue, IgnoredText);
}

TSharedPtr<FJsonObject> BuildPcgSetPinDefaultDiagnostics(UPCGNode* Node, const FString& RequestedPinName)
{
    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    if (!RequestedPinName.IsEmpty())
    {
        Details->SetStringField(TEXT("requestedPin"), RequestedPinName);
    }

    TArray<TSharedPtr<FJsonValue>> ExpectedTargetForms;
    ExpectedTargetForms.Add(MakeShared<FJsonValueString>(TEXT("args.target.nodeId + args.target.pin")));
    ExpectedTargetForms.Add(MakeShared<FJsonValueString>(TEXT("args.target.nodeRef + args.target.pin")));
    ExpectedTargetForms.Add(MakeShared<FJsonValueString>(TEXT("args.target.nodePath + args.target.pin")));
    ExpectedTargetForms.Add(MakeShared<FJsonValueString>(TEXT("args.target.path + args.target.pin")));
    Details->SetArrayField(TEXT("expectedTargetForms"), ExpectedTargetForms);

    TArray<TSharedPtr<FJsonValue>> CandidatePins;
    TSet<FString> EmittedPins;
    auto AddCandidatePin = [&CandidatePins, &EmittedPins](const FString& PinName, const bool bConnected, const bool bSyntheticDefaultTarget)
    {
        if (PinName.IsEmpty() || EmittedPins.Contains(PinName))
        {
            return;
        }

        EmittedPins.Add(PinName);
        TSharedPtr<FJsonObject> Candidate = MakeShared<FJsonObject>();
        Candidate->SetStringField(TEXT("pinName"), PinName);
        Candidate->SetBoolField(TEXT("connected"), bConnected);
        Candidate->SetBoolField(TEXT("isSyntheticDefaultTarget"), bSyntheticDefaultTarget);
        CandidatePins.Add(MakeShared<FJsonValueObject>(Candidate));
    };

    if (Node != nullptr)
    {
        for (UPCGPin* InputPin : Node->GetInputPins())
        {
            if (InputPin == nullptr)
            {
                continue;
            }

            AddCandidatePin(InputPin->Properties.Label.ToString(), InputPin->IsConnected(), false);
        }

        if (UPCGSettings* Settings = Node->GetSettings())
        {
            TArray<FPcgQuerySyntheticTarget> SyntheticTargets;
            GatherPcgSyntheticPropertyTargetsForQuery(Settings, SyntheticTargets);
            for (const FPcgQuerySyntheticTarget& SyntheticTarget : SyntheticTargets)
            {
                AddCandidatePin(SyntheticTarget.PinName, false, true);
            }
        }
    }

    Details->SetArrayField(TEXT("candidatePins"), CandidatePins);
    return Details;
}

bool SetPcgPinDefaultValue(UPCGNode* Node, const FString& PinName, const FString& Value, FString& OutError, const bool bApplyChange = true)
{
    if (Node == nullptr)
    {
        OutError = TEXT("PCG node not found.");
        return false;
    }

    UPCGPin* TargetPin = FindPcgPin(Node, PinName, false);
    if (TargetPin != nullptr && TargetPin->IsConnected())
    {
        OutError = TEXT("Invalid PCG default value target: input pin is connected. Disconnect it before setting a default value.");
        return false;
    }

    UPCGSettings* Settings = Node->GetSettings();
    if (Settings == nullptr)
    {
        OutError = TEXT("Invalid PCG default value target: node settings are missing.");
        return false;
    }

    if (!bApplyChange)
    {
        Settings = DuplicateObject<UPCGSettings>(Settings, GetTransientPackage());
        if (Settings == nullptr)
        {
            OutError = TEXT("Failed to create a temporary PCG settings copy for validation.");
            return false;
        }
    }

    if (TargetPin != nullptr)
    {
        const FName PinLabel = TargetPin->Properties.Label;
        if (IPCGSettingsDefaultValueProvider* DefaultValueProvider = Cast<IPCGSettingsDefaultValueProvider>(Settings))
        {
            if (DefaultValueProvider->IsPinDefaultValueEnabled(PinLabel))
            {
                if (bApplyChange)
                {
                    Settings->Modify();
                }
                DefaultValueProvider->SetPinDefaultValue(PinLabel, Value, true);
                if (!DefaultValueProvider->IsPinDefaultValueActivated(PinLabel))
                {
                    DefaultValueProvider->SetPinDefaultValueIsActivated(PinLabel, true);
                }
                return true;
            }
        }

        const FPCGSettingsOverridableParam* Param = FindPcgOverridableParamByPinLabel(Settings, PinLabel);
        if (Param != nullptr)
        {
            if (bApplyChange)
            {
                Settings->Modify();
            }
            if (!SetPcgOverridableValueFromString(Settings, *Param, Value, OutError))
            {
                return false;
            }

            if (bApplyChange)
            {
#if WITH_EDITOR
                Settings->OnSettingsChangedDelegate.Broadcast(Settings, EPCGChangeType::Settings);
#endif
            }
            return true;
        }
    }

    TArray<FString> PathSegments;
    PinName.ParseIntoArray(PathSegments, TEXT("/"), true);
    if (PathSegments.Num() == 0)
    {
        OutError = TEXT("Invalid PCG default value target: input pin/property path is empty.");
        return false;
    }

    void* Container = nullptr;
    FProperty* LeafProperty = nullptr;
    if (!ResolvePcgPropertyPathTarget(Settings, Settings->GetClass(), PathSegments, Container, LeafProperty, OutError))
    {
        if (TargetPin == nullptr && OutError.IsEmpty())
        {
            OutError = TEXT("PCG input pin/property path not found.");
        }
        return false;
    }

    if (bApplyChange)
    {
        Settings->Modify();
    }
    if (!SetPcgPropertyValueFromString(Settings, PinName, Container, LeafProperty, Value, OutError))
    {
        return false;
    }

    if (bApplyChange)
    {
#if WITH_EDITOR
        Settings->OnSettingsChangedDelegate.Broadcast(Settings, EPCGChangeType::Settings);
#endif
    }
    return true;
}

UMaterialExpression* FindMaterialExpressionById(UMaterial* Material, const FString& NodeId)
{
    if (Material == nullptr || NodeId.IsEmpty())
    {
        return nullptr;
    }

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression == nullptr)
        {
            continue;
        }

        if (Expression->GetPathName().Equals(NodeId)
            || Expression->GetName().Equals(NodeId))
        {
            return Expression;
        }
    }

    UMaterialGraph* MaterialGraph = Material->MaterialGraph;
    if (MaterialGraph != nullptr)
    {
        for (UEdGraphNode* Node : MaterialGraph->Nodes)
        {
            UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(Node);
            UMaterialExpression* Expression = MaterialGraphNode ? MaterialGraphNode->MaterialExpression : nullptr;
            if (Expression == nullptr)
            {
                continue;
            }

            if (Expression->GetPathName().Equals(NodeId)
                || Expression->GetName().Equals(NodeId))
            {
                return Expression;
            }
        }
    }

    return nullptr;
}

FString MaterialExpressionId(const UMaterialExpression* Expression)
{
    return Expression ? Expression->GetPathName() : FString();
}

void AddUniqueMaterialPinCandidate(TArray<FString>& Candidates, const FString& Candidate)
{
    if (Candidate.IsEmpty())
    {
        return;
    }

    for (const FString& Existing : Candidates)
    {
        if (Existing.Equals(Candidate, ESearchCase::IgnoreCase))
        {
            return;
        }
    }

    Candidates.Add(Candidate);
}

FString TrimMaterialPinName(const FString& PinName)
{
    return PinName.TrimStartAndEnd();
}

FString StripMaterialPinDisplaySuffix(const FString& PinName)
{
    const FString Trimmed = TrimMaterialPinName(PinName);
    int32 OpenParenIndex = INDEX_NONE;
    if (Trimmed.EndsWith(TEXT(")")) && Trimmed.FindLastChar(TEXT('('), OpenParenIndex) && OpenParenIndex > 0)
    {
        const FString Prefix = Trimmed.Left(OpenParenIndex).TrimEnd();
        if (!Prefix.IsEmpty())
        {
            return Prefix;
        }
    }
    return Trimmed;
}

FString CanonicalizeMaterialOutputPinName(const FString& PinName)
{
    const FString Trimmed = TrimMaterialPinName(PinName);
    if (Trimmed.IsEmpty()
        || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase)
        || Trimmed.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
    {
        return TEXT("");
    }
    return StripMaterialPinDisplaySuffix(Trimmed);
}

bool AreMaterialOutputPinNamesEquivalent(const FString& LeftPinName, const FString& RightPinName)
{
    return CanonicalizeMaterialOutputPinName(LeftPinName)
        .Equals(CanonicalizeMaterialOutputPinName(RightPinName), ESearchCase::IgnoreCase);
}

UMaterialGraphNode* FindMaterialGraphNodeByExpression(UMaterial* Material, UMaterialExpression* Expression)
{
    if (Material == nullptr || Expression == nullptr || Material->MaterialGraph == nullptr)
    {
        return nullptr;
    }

    for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
    {
        UMaterialGraphNode* MaterialGraphNode = Cast<UMaterialGraphNode>(Node);
        if (MaterialGraphNode != nullptr && MaterialGraphNode->MaterialExpression == Expression)
        {
            return MaterialGraphNode;
        }
    }

    return nullptr;
}

bool TryResolveMaterialOutputPinName(
    UMaterial* Material,
    UMaterialExpression* Expression,
    const FString& RequestedPinName,
    FString& OutResolvedPinName,
    TArray<FString>* OutAvailablePinNames)
{
    OutResolvedPinName.Reset();
    if (OutAvailablePinNames != nullptr)
    {
        OutAvailablePinNames->Reset();
    }

    if (Expression == nullptr)
    {
        return false;
    }

    const FString RequestedTrimmed = TrimMaterialPinName(RequestedPinName);
    const FString RequestedStripped = StripMaterialPinDisplaySuffix(RequestedTrimmed);
    const FString RequestedCanonical = CanonicalizeMaterialOutputPinName(RequestedTrimmed);
    if (RequestedCanonical.IsEmpty())
    {
        OutResolvedPinName = TEXT("");
        return true;
    }

    UMaterialGraphNode* MaterialGraphNode = FindMaterialGraphNodeByExpression(Material, Expression);
    if (MaterialGraphNode == nullptr)
    {
        return false;
    }

    for (UEdGraphPin* Pin : MaterialGraphNode->Pins)
    {
        if (Pin == nullptr || Pin->Direction != EGPD_Output)
        {
            continue;
        }

        const FString GraphPinName = TrimMaterialPinName(Pin->PinName.ToString());
        const FString GraphPinNameStripped = StripMaterialPinDisplaySuffix(GraphPinName);
        const FString GraphPinNameCanonical = CanonicalizeMaterialOutputPinName(GraphPinName);
        if (OutAvailablePinNames != nullptr)
        {
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, GraphPinNameCanonical.IsEmpty() ? GraphPinName : GraphPinNameCanonical);
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, GraphPinNameStripped);
        }

        const bool bMatches =
            (!RequestedTrimmed.IsEmpty() && GraphPinName.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && GraphPinName.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedCanonical.IsEmpty() && GraphPinName.Equals(RequestedCanonical, ESearchCase::IgnoreCase))
            || (!RequestedTrimmed.IsEmpty() && GraphPinNameStripped.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && GraphPinNameStripped.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedCanonical.IsEmpty() && GraphPinNameCanonical.Equals(RequestedCanonical, ESearchCase::IgnoreCase));

        if (bMatches)
        {
            OutResolvedPinName = GraphPinNameCanonical;
            return true;
        }
    }

    return false;
}

bool TryResolveMaterialOutputPin(
    UMaterial* Material,
    UMaterialExpression* Expression,
    const FString& RequestedPinName,
    int32& OutOutputIndex,
    FString& OutResolvedPinName,
    TArray<FString>* OutAvailablePinNames)
{
    OutOutputIndex = INDEX_NONE;
    if (!TryResolveMaterialOutputPinName(Material, Expression, RequestedPinName, OutResolvedPinName, OutAvailablePinNames))
    {
        return false;
    }

    if (Expression == nullptr)
    {
        return false;
    }

    TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
    if (Outputs.Num() == 0)
    {
        return false;
    }

    if (OutResolvedPinName.IsEmpty())
    {
        OutOutputIndex = 0;
        return true;
    }

    const FName RequestedOutputName(*OutResolvedPinName);
    for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
    {
        bool bFoundMatch = false;
        FExpressionOutput& Output = Outputs[OutputIndex];
        if (!Output.OutputName.IsNone())
        {
            bFoundMatch = RequestedOutputName == Output.OutputName;
        }
        else if (RequestedOutputName == TEXT("R"))
        {
            bFoundMatch = Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA;
        }
        else if (RequestedOutputName == TEXT("G"))
        {
            bFoundMatch = !Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA;
        }
        else if (RequestedOutputName == TEXT("B"))
        {
            bFoundMatch = !Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA;
        }
        else if (RequestedOutputName == TEXT("A"))
        {
            bFoundMatch = !Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA;
        }

        if (bFoundMatch)
        {
            OutOutputIndex = OutputIndex;
            return true;
        }
    }

    return false;
}

bool TryResolveMaterialInputPinName(
    UMaterialExpression* Expression,
    const FString& RequestedPinName,
    int32& OutInputIndex,
    FString& OutResolvedPinName,
    TArray<FString>* OutAvailablePinNames)
{
    OutInputIndex = INDEX_NONE;
    OutResolvedPinName.Reset();
    if (OutAvailablePinNames != nullptr)
    {
        OutAvailablePinNames->Reset();
    }

    if (Expression == nullptr)
    {
        return false;
    }

    const FString RequestedTrimmed = TrimMaterialPinName(RequestedPinName);
    const FString RequestedStripped = StripMaterialPinDisplaySuffix(RequestedTrimmed);
    int32 InputCount = 0;
    int32 FirstInputIndex = INDEX_NONE;
    FString FirstResolvedPinName;

    const int32 MaxInputs = 128;
    for (int32 Index = 0; Index < MaxInputs; ++Index)
    {
        FExpressionInput* Input = Expression->GetInput(Index);
        if (Input == nullptr)
        {
            break;
        }

        ++InputCount;
        if (FirstInputIndex == INDEX_NONE)
        {
            FirstInputIndex = Index;
        }

        const FString DisplayName = TrimMaterialPinName(Expression->GetInputName(Index).ToString());
        const FString DisplayNameStripped = StripMaterialPinDisplaySuffix(DisplayName);
        const FString RawName = TrimMaterialPinName(Input->InputName.ToString());
        const FString RawNameStripped = StripMaterialPinDisplaySuffix(RawName);
        const FString ResolvedPinName = !DisplayName.IsEmpty()
            ? DisplayName
            : (!DisplayNameStripped.IsEmpty()
                ? DisplayNameStripped
                : (!RawName.IsEmpty() ? RawName : RawNameStripped));

        if (FirstResolvedPinName.IsEmpty())
        {
            FirstResolvedPinName = ResolvedPinName;
        }

        if (OutAvailablePinNames != nullptr)
        {
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, ResolvedPinName);
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, RawNameStripped);
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, DisplayName);
            AddUniqueMaterialPinCandidate(*OutAvailablePinNames, DisplayNameStripped);
        }

        const bool bMatches =
            (!RequestedTrimmed.IsEmpty() && DisplayName.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && DisplayName.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedTrimmed.IsEmpty() && DisplayNameStripped.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && DisplayNameStripped.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedTrimmed.IsEmpty() && RawName.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && RawName.Equals(RequestedStripped, ESearchCase::IgnoreCase))
            || (!RequestedTrimmed.IsEmpty() && RawNameStripped.Equals(RequestedTrimmed, ESearchCase::IgnoreCase))
            || (!RequestedStripped.IsEmpty() && RawNameStripped.Equals(RequestedStripped, ESearchCase::IgnoreCase));

        if (bMatches)
        {
            OutInputIndex = Index;
            OutResolvedPinName = ResolvedPinName;
            return true;
        }
    }

    if (RequestedTrimmed.IsEmpty() && InputCount == 1)
    {
        OutInputIndex = FirstInputIndex;
        OutResolvedPinName = FirstResolvedPinName;
        return OutInputIndex != INDEX_NONE;
    }

    return false;
}

int32 GetMaterialExpressionInputCount(UMaterialExpression* Expression)
{
    if (Expression == nullptr)
    {
        return 0;
    }

    int32 InputCount = 0;
    const int32 MaxInputs = 128;
    for (int32 Index = 0; Index < MaxInputs; ++Index)
    {
        if (Expression->GetInput(Index) == nullptr)
        {
            break;
        }

        ++InputCount;
    }

    return InputCount;
}

int32 FindMaterialInputIndexByName(UMaterialExpression* Expression, const FString& PinName)
{
    int32 InputIndex = INDEX_NONE;
    FString ResolvedPinName;
    return TryResolveMaterialInputPinName(Expression, PinName, InputIndex, ResolvedPinName, nullptr)
        ? InputIndex
        : INDEX_NONE;
}

UEdGraph* ResolveBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }

    auto MatchGraphName = [&GraphName](UEdGraph* Graph) -> bool
    {
        return Graph != nullptr && (Graph->GetName().Equals(GraphName) || Graph->GetFName().ToString().Equals(GraphName));
    };

    const FString EffectiveGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (MatchGraphName(Graph))
        {
            return Graph;
        }
    }
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        for (UEdGraph* Graph : InterfaceDesc.Graphs)
        {
            if (MatchGraphName(Graph))
            {
                return Graph;
            }
        }
    }

    if (EffectiveGraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
    {
        return FBlueprintEditorUtils::FindEventGraph(Blueprint);
    }
    return nullptr;
}

UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& NodeGuidText)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }

    FGuid NodeGuid;
    if (!FGuid::Parse(NodeGuidText, NodeGuid))
    {
        return nullptr;
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr && Node->NodeGuid == NodeGuid)
        {
            return Node;
        }
    }
    return nullptr;
}

UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName)
{
    if (Node == nullptr || PinName.IsEmpty())
    {
        return nullptr;
    }

    if (UEdGraphPin* Pin = Node->FindPin(*PinName))
    {
        return Pin;
    }

    auto NormalizePinToken = [](FString Value) -> FString
    {
        Value = Value.ToLower();
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("_"), TEXT(""));
        Value.ReplaceInline(TEXT("-"), TEXT(""));
        return Value;
    };

    const FString RequestedKey = NormalizePinToken(PinName);

    auto TrySchemaPin = [Node](const FName& SchemaPinName) -> UEdGraphPin*
    {
        return Node->FindPin(SchemaPinName);
    };

    if (RequestedKey.Equals(TEXT("execute")) || RequestedKey.Equals(TEXT("execin")))
    {
        if (UEdGraphPin* Pin = TrySchemaPin(UEdGraphSchema_K2::PN_Execute))
        {
            return Pin;
        }
    }

    if (RequestedKey.Equals(TEXT("then")) || RequestedKey.Equals(TEXT("execout")))
    {
        if (UEdGraphPin* Pin = TrySchemaPin(UEdGraphSchema_K2::PN_Then))
        {
            return Pin;
        }
    }

    if (RequestedKey.Equals(TEXT("input")) || RequestedKey.Equals(TEXT("inputpin")))
    {
        if (UEdGraphPin* Pin = Node->FindPin(TEXT("InputPin")))
        {
            return Pin;
        }
    }

    if (RequestedKey.Equals(TEXT("output")) || RequestedKey.Equals(TEXT("outputpin")))
    {
        if (UEdGraphPin* Pin = Node->FindPin(TEXT("OutputPin")))
        {
            return Pin;
        }
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && NormalizePinToken(Pin->PinName.ToString()).Equals(RequestedKey))
        {
            return Pin;
        }
    }

    return nullptr;
}

TSharedPtr<SWindow> ResolveActiveTopLevelWindow()
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid())
        {
            ActiveWindow = FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef());
        }
    }

    return ActiveWindow;
}

bool WindowHasNativeCaptureHandle(const TSharedPtr<SWindow>& Window)
{
#if PLATFORM_WINDOWS
    return Window.IsValid()
        && Window->GetNativeWindow().IsValid()
        && Window->GetNativeWindow()->GetOSWindowHandle() != nullptr;
#else
    return Window.IsValid();
#endif
}

void RefreshSlateWindowForCapture(const TSharedRef<SWindow>& Window)
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    FSlateApplication& SlateApp = FSlateApplication::Get();

#if PLATFORM_WINDOWS
    // Windows capture uses the native HWND path below, so avoid driving the Slate
    // redraw path here. ForceRedrawWindow can hit platform-specific restore-size
    // assertions on editor windows during automation.
    (void)Window;
    SlateApp.InvalidateAllWidgets(false);
    return;
#else
    SlateApp.InvalidateAllWidgets(false);
    SlateApp.Tick(ESlateTickType::All);
    SlateApp.ForceRedrawWindow(Window);
#endif
}

TSharedPtr<SWindow> ResolveCaptureTopLevelWindow()
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
    if (!ActiveWindow.IsValid())
    {
        ActiveWindow = ResolveActiveTopLevelWindow();
    }
    if (WindowHasNativeCaptureHandle(ActiveWindow))
    {
        return ActiveWindow;
    }

    if (FModuleManager::Get().IsModuleLoaded(TEXT("MainFrame")))
    {
        IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
        const TSharedPtr<SWindow> MainFrameWindow = MainFrameModule.GetParentWindow();
        if (WindowHasNativeCaptureHandle(MainFrameWindow))
        {
            return MainFrameWindow;
        }
    }

    TArray<TSharedRef<SWindow>> CandidateWindows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
    if (CandidateWindows.Num() == 0)
    {
        FSlateApplication::Get().GetAllVisibleWindowsOrdered(CandidateWindows);
    }

    for (int32 Index = CandidateWindows.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWindow>& Candidate = CandidateWindows[Index];
        if (WindowHasNativeCaptureHandle(Candidate))
        {
            return Candidate;
        }
    }

    return ActiveWindow;
}

#if PLATFORM_WINDOWS
HWND ResolveCaptureWindowHandle(const TSharedPtr<SWindow>& PreferredWindow)
{
    if (WindowHasNativeCaptureHandle(PreferredWindow))
    {
        return reinterpret_cast<HWND>(PreferredWindow->GetNativeWindow()->GetOSWindowHandle());
    }

    if (FSlateApplication::IsInitialized())
    {
        const TSharedPtr<SWindow> ActiveRegularWindow = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
        if (WindowHasNativeCaptureHandle(ActiveRegularWindow))
        {
            return reinterpret_cast<HWND>(ActiveRegularWindow->GetNativeWindow()->GetOSWindowHandle());
        }

        if (FModuleManager::Get().IsModuleLoaded(TEXT("MainFrame")))
        {
            IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
            const TSharedPtr<SWindow> MainFrameWindow = MainFrameModule.GetParentWindow();
            if (WindowHasNativeCaptureHandle(MainFrameWindow))
            {
                return reinterpret_cast<HWND>(MainFrameWindow->GetNativeWindow()->GetOSWindowHandle());
            }
        }
    }

    return ::GetForegroundWindow();
}
#endif

FString ResolveScreenshotOutputPath(const FString& RequestedPath)
{
    FString OutputPath = RequestedPath.TrimStartAndEnd();
    if (OutputPath.IsEmpty())
    {
        const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutputPath = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Loomle"),
            TEXT("captures"),
            FString::Printf(TEXT("capture-%s.png"), *Timestamp));
    }
    else if (FPaths::IsRelative(OutputPath))
    {
        OutputPath = FPaths::Combine(FPaths::ProjectDir(), OutputPath);
    }

    if (!OutputPath.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase))
    {
        OutputPath += TEXT(".png");
    }

    return FPaths::ConvertRelativePathToFull(OutputPath);
}

TSharedPtr<FJsonObject> BuildActiveWindowJson()
{
    TSharedPtr<FJsonObject> Window = MakeShared<FJsonObject>();
    Window->SetBoolField(TEXT("isValid"), false);
    Window->SetStringField(TEXT("title"), TEXT(""));

    TSharedPtr<SWindow> ActiveWindow = ResolveCaptureTopLevelWindow();

    if (!ActiveWindow.IsValid())
    {
        return Window;
    }

    Window->SetBoolField(TEXT("isValid"), true);
    Window->SetStringField(TEXT("title"), ActiveWindow->GetTitle().ToString());
    return Window;
}

const FName MaterialEditorPreviewTabId(TEXT("MaterialEditor_Preview"));
const FName MaterialEditorPropertiesTabId(TEXT("MaterialEditor_MaterialProperties"));
const FName MaterialEditorPaletteTabId(TEXT("MaterialEditor_Palette"));
const FName MaterialEditorFindTabId(TEXT("MaterialEditor_Find"));
const FName MaterialEditorGraphTabId(TEXT("Document"));

FString NormalizeEditorPanel(FString Panel)
{
    Panel = Panel.TrimStartAndEnd().ToLower();
    return Panel;
}

bool IsMaterialLikeAsset(const UObject* Asset)
{
    return Asset != nullptr
        && (Asset->IsA<UMaterial>()
            || Asset->IsA<UMaterialFunctionInterface>());
}

void RefreshMaterialEditorVisuals(UObject* MaterialAsset)
{
    if (!MaterialAsset || !IsMaterialLikeAsset(MaterialAsset))
    {
        return;
    }

    if (const TSharedPtr<IMaterialEditor> MaterialEditor = FMaterialEditorUtilities::GetIMaterialEditorForObject(MaterialAsset))
    {
        MaterialEditor->UpdateMaterialAfterGraphChange();
        MaterialEditor->ForceRefreshExpressionPreviews();
    }

    if (const UMaterial* Material = Cast<UMaterial>(MaterialAsset))
    {
        if (Material->MaterialGraph != nullptr)
        {
            FMaterialEditorUtilities::UpdateMaterialAfterGraphChange(Material->MaterialGraph);
            FMaterialEditorUtilities::ForceRefreshExpressionPreviews(Material->MaterialGraph);
        }
    }
}

void RefreshPcgEditorVisuals(UObject* PcgAsset)
{
    UPCGGraph* Graph = Cast<UPCGGraph>(PcgAsset);
    if (Graph == nullptr)
    {
        return;
    }

    Graph->PostEditChange();

    FPropertyChangedEvent EmptyEvent(nullptr);
    FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Graph, EmptyEvent);

    if (GEditor == nullptr)
    {
        return;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        return;
    }

    if (IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Graph, true))
    {
        if (FPCGEditor* PCGEditor = static_cast<FPCGEditor*>(EditorInstance))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::GraphEditor);
            EditorInstance->FocusWindow(Graph);
        }
    }
}

FString GetActiveWindowTitle()
{
    TSharedPtr<SWindow> ActiveWindow = ResolveActiveTopLevelWindow();

    return ActiveWindow.IsValid() ? ActiveWindow->GetTitle().ToString() : FString();
}

UBlueprint* FindEditedBlueprint()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UBlueprint* FallbackBlueprint = nullptr;
    UBlueprint* BestTitleMatchBlueprint = nullptr;
    int32 BestTitleMatchNameLen = -1;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
        if (!Blueprint)
        {
            continue;
        }

        if (!FallbackBlueprint)
        {
            FallbackBlueprint = Blueprint;
        }

        if (!ActiveWindowTitle.IsEmpty()
            && ActiveWindowTitle.Contains(Blueprint->GetName(), ESearchCase::IgnoreCase))
        {
            const int32 NameLen = Blueprint->GetName().Len();
            if (NameLen > BestTitleMatchNameLen)
            {
                BestTitleMatchNameLen = NameLen;
                BestTitleMatchBlueprint = Blueprint;
            }
        }
    }

    if (BestTitleMatchBlueprint)
    {
        return BestTitleMatchBlueprint;
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackBlueprint : nullptr;
}

enum class EGraphSelectionDomain : uint8
{
    Unknown,
    Blueprint,
    Material,
    Pcg
};

bool CollectSelectedGraphObjectsFromActiveWindow(TArray<UObject*>& OutSelectedObjects, EGraphSelectionDomain& OutDomain);
void CollectGraphEditorsFromWidgetTree(const TSharedRef<SWidget>& RootWidget, TArray<TSharedPtr<SGraphEditor>>& OutGraphEditors);
EGraphSelectionDomain DetectGraphSelectionDomain(const TArray<UObject*>& SelectedObjects);

FString GetBlueprintEditorKind(const UBlueprint* Blueprint)
{
    return Blueprint != nullptr && Blueprint->IsA<UWidgetBlueprint>() ? TEXT("widgetBlueprint") : TEXT("blueprint");
}

FString GetGraphId(const UEdGraph* Graph)
{
    return Graph != nullptr ? Graph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
}

TSharedPtr<FJsonObject> MakeBlueprintGraphSelectionRef(UBlueprint* Blueprint, UEdGraph* Graph)
{
    FString AssetPath;
    if (Blueprint == nullptr || Graph == nullptr || !TryGetAssetPathFromObject(Blueprint, AssetPath))
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> GraphRef = MakeAssetGraphRefJson(AssetPath, Graph->GetName(), GetGraphId(Graph));
    GraphRef->SetStringField(TEXT("name"), Graph->GetName());
    return GraphRef;
}

TSharedPtr<FJsonObject> MakeBlueprintActiveAssetJson(UBlueprint* Blueprint)
{
    TSharedPtr<FJsonObject> ActiveAsset = MakeShared<FJsonObject>();
    if (Blueprint == nullptr)
    {
        ActiveAsset->SetStringField(TEXT("kind"), TEXT("unknown"));
        return ActiveAsset;
    }

    FString AssetPath;
    TryGetAssetPathFromObject(Blueprint, AssetPath);
    ActiveAsset->SetStringField(TEXT("kind"), GetBlueprintEditorKind(Blueprint));
    ActiveAsset->SetStringField(TEXT("domain"), TEXT("blueprint"));
    ActiveAsset->SetStringField(TEXT("assetPath"), AssetPath);
    ActiveAsset->SetStringField(TEXT("assetName"), Blueprint->GetName());
    ActiveAsset->SetStringField(TEXT("assetClass"), Blueprint->GetClass() ? Blueprint->GetClass()->GetPathName() : TEXT(""));
    return ActiveAsset;
}

TSharedPtr<FJsonObject> MakeBlueprintActiveEditorJson(UBlueprint* Blueprint, const FString& Source)
{
    TSharedPtr<FJsonObject> ActiveEditor = MakeBlueprintActiveAssetJson(Blueprint);
    ActiveEditor->SetStringField(TEXT("source"), Source);
    return ActiveEditor;
}

TSharedPtr<FJsonObject> MakeBlueprintSelectedNodeJson(UBlueprint* Blueprint, UEdGraphNode* Node)
{
    if (Blueprint == nullptr || Node == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
    Item->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
    Item->SetStringField(TEXT("name"), Node->GetName());
    Item->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Item->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT(""));
    Item->SetStringField(TEXT("path"), Node->GetPathName());
    Item->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
    Item->SetNumberField(TEXT("nodePosY"), Node->NodePosY);

    TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), Node->NodePosX);
    Position->SetNumberField(TEXT("y"), Node->NodePosY);
    Item->SetObjectField(TEXT("position"), Position);

    if (UEdGraph* Graph = Node->GetGraph())
    {
        Item->SetStringField(TEXT("graphName"), Graph->GetName());
        Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
        TSharedPtr<FJsonObject> GraphRef = MakeBlueprintGraphSelectionRef(Blueprint, Graph);
        if (GraphRef.IsValid())
        {
            Item->SetObjectField(TEXT("graph"), GraphRef);
        }
    }

    Item->SetObjectField(TEXT("nodeEditCapabilities"), BuildBlueprintNodeEditCapabilities(Item));
    return Item;
}

TSharedPtr<FJsonObject> MakeBlueprintPinJson(UBlueprint* Blueprint, UEdGraphPin* Pin, const FString& Source)
{
    if (Blueprint == nullptr || Pin == nullptr)
    {
        return nullptr;
    }

    UEdGraphNode* OwningNode = Pin->GetOwningNode();
    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
    PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphensLower));
    PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
    PinObj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
    PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
    PinObj->SetStringField(TEXT("source"), Source);
    if (OwningNode != nullptr)
    {
        PinObj->SetStringField(TEXT("nodeId"), OwningNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
        if (UEdGraph* Graph = OwningNode->GetGraph())
        {
            TSharedPtr<FJsonObject> GraphRef = MakeBlueprintGraphSelectionRef(Blueprint, Graph);
            if (GraphRef.IsValid())
            {
                PinObj->SetObjectField(TEXT("graph"), GraphRef);
            }
        }
    }
    return PinObj;
}

bool ResolveActiveBlueprintGraphEditor(
    TSharedPtr<SGraphEditor>& OutGraphEditor,
    UEdGraph*& OutGraph,
    UBlueprint*& OutBlueprint,
    FString& OutReason)
{
    OutGraphEditor.Reset();
    OutGraph = nullptr;
    OutBlueprint = nullptr;
    OutReason.Empty();

    TSharedPtr<SWindow> ActiveWindow = ResolveActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        OutReason = TEXT("NO_ACTIVE_WINDOW");
        return false;
    }

    TArray<TSharedPtr<SGraphEditor>> GraphEditors;
    CollectGraphEditorsFromWidgetTree(ActiveWindow.ToSharedRef(), GraphEditors);
    if (GraphEditors.Num() == 0)
    {
        OutBlueprint = FindEditedBlueprint();
        OutReason = OutBlueprint != nullptr ? TEXT("FOCUS_NOT_GRAPH_EDITOR") : TEXT("NO_ACTIVE_EDITOR");
        return false;
    }

    int32 BestScore = TNumericLimits<int32>::Min();
    TSharedPtr<SGraphEditor> BestGraphEditor;
    UEdGraph* BestGraph = nullptr;
    UBlueprint* BestBlueprint = nullptr;

    for (const TSharedPtr<SGraphEditor>& GraphEditor : GraphEditors)
    {
        if (!GraphEditor.IsValid())
        {
            continue;
        }

        UEdGraph* Graph = GraphEditor->GetCurrentGraph();
        UBlueprint* Blueprint = Graph != nullptr ? FBlueprintEditorUtils::FindBlueprintForGraph(Graph) : nullptr;
        if (Blueprint == nullptr)
        {
            continue;
        }

        int32 Score = 1000;
        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        Score += SelectedNodes.Num() * 10;

        TArray<UObject*> SelectionObjects;
        SelectionObjects.Reserve(SelectedNodes.Num());
        for (UObject* SelectedObject : SelectedNodes)
        {
            if (SelectedObject != nullptr)
            {
                SelectionObjects.Add(SelectedObject);
            }
        }
        if (DetectGraphSelectionDomain(SelectionObjects) == EGraphSelectionDomain::Blueprint)
        {
            Score += 100;
        }

        if (Score > BestScore)
        {
            BestScore = Score;
            BestGraphEditor = GraphEditor;
            BestGraph = Graph;
            BestBlueprint = Blueprint;
        }
    }

    if (!BestGraphEditor.IsValid() || BestGraph == nullptr || BestBlueprint == nullptr)
    {
        OutReason = TEXT("ACTIVE_EDITOR_NOT_BLUEPRINT");
        return false;
    }

    OutGraphEditor = BestGraphEditor;
    OutGraph = BestGraph;
    OutBlueprint = BestBlueprint;
    return true;
}

bool BuildBlueprintEditorSelectionContextSnapshot(TSharedPtr<FJsonObject>& OutSnapshot)
{
    OutSnapshot.Reset();

    TSharedPtr<SGraphEditor> GraphEditor;
    UEdGraph* ActiveGraph = nullptr;
    UBlueprint* Blueprint = nullptr;
    FString Reason;
    const bool bHasGraphEditor = ResolveActiveBlueprintGraphEditor(GraphEditor, ActiveGraph, Blueprint, Reason);
    if (!bHasGraphEditor && Blueprint == nullptr)
    {
        return false;
    }

    OutSnapshot = MakeShared<FJsonObject>();
    OutSnapshot->SetBoolField(TEXT("isError"), false);
    OutSnapshot->SetObjectField(TEXT("activeEditor"), MakeBlueprintActiveEditorJson(Blueprint, bHasGraphEditor ? TEXT("focusedGraphEditor") : TEXT("editedAsset")));
    OutSnapshot->SetObjectField(TEXT("activeAsset"), MakeBlueprintActiveAssetJson(Blueprint));

    TSharedPtr<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetBoolField(TEXT("isError"), false);
    Selection->SetStringField(TEXT("editorType"), TEXT("blueprint"));
    Selection->SetStringField(TEXT("provider"), TEXT("blueprint_adapter"));
    Selection->SetStringField(TEXT("kind"), TEXT("graph"));
    Selection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    FString AssetPath;
    if (Blueprint != nullptr && TryGetAssetPathFromObject(Blueprint, AssetPath))
    {
        Selection->SetStringField(TEXT("assetPath"), AssetPath);
    }

    if (bHasGraphEditor && ActiveGraph != nullptr)
    {
        TSharedPtr<FJsonObject> ActiveGraphRef = MakeBlueprintGraphSelectionRef(Blueprint, ActiveGraph);
        if (ActiveGraphRef.IsValid())
        {
            OutSnapshot->SetObjectField(TEXT("activeGraph"), ActiveGraphRef);
            Selection->SetObjectField(TEXT("activeGraph"), ActiveGraphRef);
        }
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> Pins;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;

    if (bHasGraphEditor && GraphEditor.IsValid())
    {
        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        for (UObject* SelectedObject : SelectedNodes)
        {
            UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
            if (Node == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeObj = MakeBlueprintSelectedNodeJson(Blueprint, Node);
            if (!NodeObj.IsValid())
            {
                continue;
            }

            TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
            TSet<FString> ItemSeenGraphRefs;
            if (!AssetPath.IsEmpty())
            {
                if (UEdGraph* Graph = Node->GetGraph())
                {
                    AddResolvedGraphRefEntry(
                        ItemResolvedGraphRefs,
                        ItemSeenGraphRefs,
                        TEXT("blueprint"),
                        MakeAssetGraphRefJson(AssetPath, Graph->GetName(), GetGraphId(Graph)),
                        TEXT("selected_graph"),
                        TEXT("loaded"));
                }

                FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
                if (BoundGraphProp != nullptr)
                {
                    UEdGraph* BoundGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
                    if (BoundGraph != nullptr)
                    {
                        AddResolvedGraphRefEntry(
                            ItemResolvedGraphRefs,
                            ItemSeenGraphRefs,
                            TEXT("blueprint"),
                            MakeInlineGraphRefJson(AssetPath, Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)),
                            TEXT("child"),
                            TEXT("loaded"));
                    }
                }
            }

            SetResolvedGraphRefsFieldIfAny(NodeObj, ItemResolvedGraphRefs);
            CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
            Items.Add(MakeShared<FJsonValueObject>(NodeObj));
        }

        if (UEdGraphPin* MenuPin = GraphEditor->GetGraphPinForMenu())
        {
            TSharedPtr<FJsonObject> PinObj = MakeBlueprintPinJson(Blueprint, MenuPin, TEXT("graphPinForMenu"));
            if (PinObj.IsValid())
            {
                Pins.Add(MakeShared<FJsonValueObject>(PinObj));
            }
        }
    }

    Selection->SetArrayField(TEXT("nodes"), Nodes);
    Selection->SetArrayField(TEXT("items"), Items);
    Selection->SetNumberField(TEXT("count"), Nodes.Num());
    Selection->SetArrayField(TEXT("pins"), Pins);
    Selection->SetStringField(TEXT("pinSelectionStatus"), Pins.Num() > 0 ? TEXT("resolved") : TEXT("not_available_from_graph_editor_api"));
    Selection->SetStringField(TEXT("status"), Nodes.Num() > 0 ? TEXT("resolved") : TEXT("unavailable"));
    if (Nodes.Num() == 0)
    {
        Selection->SetStringField(TEXT("reason"), Reason.IsEmpty() ? TEXT("NO_SELECTED_NODES") : Reason);
    }
    SetResolvedGraphRefsFieldIfAny(Selection, ResolvedGraphRefs);

    OutSnapshot->SetObjectField(TEXT("selection"), Selection);
    return true;
}

bool CollectSelectedBlueprintNodes(TArray<UEdGraphNode*>& OutNodes, UBlueprint*& OutBlueprint)
{
    OutNodes.Reset();
    OutBlueprint = nullptr;

    UBlueprint* EditedBlueprint = FindEditedBlueprint();
    if (!EditedBlueprint)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Blueprint)
    {
        return false;
    }

    for (UObject* SelectedObject : SelectedObjects)
    {
        UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObject);
        if (Node)
        {
            OutNodes.Add(Node);
        }
    }

    OutBlueprint = EditedBlueprint;
    return OutNodes.Num() > 0;
}

bool BuildBlueprintSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();

    TArray<UEdGraphNode*> SelectedNodes;
    UBlueprint* Blueprint = nullptr;
    if (!CollectSelectedBlueprintNodes(SelectedNodes, Blueprint) || !Blueprint)
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("blueprint"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("blueprint_adapter"));
    OutSelection->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    Items.Reserve(SelectedNodes.Num());
    for (UEdGraphNode* Node : SelectedNodes)
    {
        if (!Node)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower));
        Item->SetStringField(TEXT("name"), Node->GetName());
        Item->SetStringField(TEXT("class"), Node->GetClass() ? Node->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), Node->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), Node->NodePosX);
        Item->SetNumberField(TEXT("nodePosY"), Node->NodePosY);
        if (UEdGraph* Graph = Node->GetGraph())
        {
            Item->SetStringField(TEXT("graphName"), Graph->GetName());
            Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
        }

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        FString BlueprintAssetPath;
        if (TryGetAssetPathFromObject(Blueprint, BlueprintAssetPath))
        {
            if (UEdGraph* Graph = Node->GetGraph())
            {
                AddResolvedGraphRefEntry(
                    ItemResolvedGraphRefs,
                    ItemSeenGraphRefs,
                    TEXT("blueprint"),
                    MakeAssetGraphRefJson(BlueprintAssetPath, Graph->GetName()),
                    TEXT("selected_graph"),
                    TEXT("loaded"));
            }

            FObjectPropertyBase* BoundGraphProp = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("BoundGraph"));
            if (BoundGraphProp != nullptr)
            {
                UEdGraph* BoundGraph = Cast<UEdGraph>(BoundGraphProp->GetObjectPropertyValue_InContainer(Node));
                if (BoundGraph != nullptr)
                {
                    AddResolvedGraphRefEntry(
                        ItemResolvedGraphRefs,
                        ItemSeenGraphRefs,
                        TEXT("blueprint"),
                        MakeInlineGraphRefJson(
                            BlueprintAssetPath,
                            Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)),
                        TEXT("child"),
                        TEXT("loaded"));
                }
            }
        }

        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
    return true;
}

UObject* FindEditedMaterialAsset()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UObject* FallbackMaterialAsset = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (!Asset || (!Asset->IsA<UMaterial>() && !Asset->IsA<UMaterialFunction>()))
        {
            continue;
        }

        if (!FallbackMaterialAsset)
        {
            FallbackMaterialAsset = Asset;
        }

        if (!ActiveWindowTitle.IsEmpty()
            && ActiveWindowTitle.Contains(Asset->GetName(), ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackMaterialAsset : nullptr;
}

UObject* FindEditedPcgAsset()
{
    if (!GEditor)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!AssetEditorSubsystem)
    {
        return nullptr;
    }

    const FString ActiveWindowTitle = GetActiveWindowTitle();
    UObject* FallbackAsset = nullptr;

    const TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
    for (UObject* Asset : EditedAssets)
    {
        if (!IsLikelyPcgAsset(Asset))
        {
            continue;
        }

        if (!FallbackAsset)
        {
            FallbackAsset = Asset;
        }

        if (!ActiveWindowTitle.IsEmpty() && ActiveWindowTitle.Contains(Asset->GetName(), ESearchCase::IgnoreCase))
        {
            return Asset;
        }
    }

    return ActiveWindowTitle.IsEmpty() ? FallbackAsset : nullptr;
}

UWidgetBlueprint* FindEditedWidgetBlueprint()
{
    return Cast<UWidgetBlueprint>(FindEditedBlueprint());
}

TSharedPtr<FJsonObject> MakeActiveAssetJson(UObject* Asset, const FString& Kind, const FString& Domain)
{
    TSharedPtr<FJsonObject> ActiveAsset = MakeShared<FJsonObject>();
    ActiveAsset->SetStringField(TEXT("kind"), Asset != nullptr ? Kind : TEXT("unknown"));
    ActiveAsset->SetStringField(TEXT("domain"), Domain);
    if (Asset != nullptr)
    {
        FString AssetPath;
        TryGetAssetPathFromObject(Asset, AssetPath);
        ActiveAsset->SetStringField(TEXT("assetPath"), AssetPath);
        ActiveAsset->SetStringField(TEXT("assetName"), Asset->GetName());
        ActiveAsset->SetStringField(TEXT("assetClass"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
    }
    return ActiveAsset;
}

TSharedPtr<FJsonObject> MakeActiveEditorJson(UObject* Asset, const FString& Kind, const FString& Domain, const FString& Source)
{
    TSharedPtr<FJsonObject> ActiveEditor = MakeActiveAssetJson(Asset, Kind, Domain);
    ActiveEditor->SetStringField(TEXT("source"), Source);
    return ActiveEditor;
}

TSharedPtr<FJsonObject> MakeGraphPositionJson(const double X, const double Y)
{
    TSharedPtr<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), X);
    Position->SetNumberField(TEXT("y"), Y);
    return Position;
}

bool ResolveActiveGraphEditorByDomain(
    const EGraphSelectionDomain RequestedDomain,
    TSharedPtr<SGraphEditor>& OutGraphEditor,
    UEdGraph*& OutGraph,
    FString& OutReason)
{
    OutGraphEditor.Reset();
    OutGraph = nullptr;
    OutReason.Empty();

    TSharedPtr<SWindow> ActiveWindow = ResolveActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        OutReason = TEXT("NO_ACTIVE_WINDOW");
        return false;
    }

    TArray<TSharedPtr<SGraphEditor>> GraphEditors;
    CollectGraphEditorsFromWidgetTree(ActiveWindow.ToSharedRef(), GraphEditors);
    if (GraphEditors.Num() == 0)
    {
        OutReason = TEXT("FOCUS_NOT_GRAPH_EDITOR");
        return false;
    }

    int32 BestScore = TNumericLimits<int32>::Min();
    TSharedPtr<SGraphEditor> BestGraphEditor;
    UEdGraph* BestGraph = nullptr;
    for (const TSharedPtr<SGraphEditor>& GraphEditor : GraphEditors)
    {
        if (!GraphEditor.IsValid())
        {
            continue;
        }

        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        TArray<UObject*> SelectionObjects;
        SelectionObjects.Reserve(SelectedNodes.Num());
        for (UObject* SelectedObject : SelectedNodes)
        {
            if (SelectedObject != nullptr)
            {
                SelectionObjects.Add(SelectedObject);
            }
        }

        const EGraphSelectionDomain CurrentDomain = DetectGraphSelectionDomain(SelectionObjects);
        int32 Score = 1000 + SelectedNodes.Num();
        if (CurrentDomain == RequestedDomain)
        {
            Score += 1000;
        }
        else if (SelectedNodes.Num() > 0)
        {
            Score -= 100;
        }

        if (Score > BestScore)
        {
            BestScore = Score;
            BestGraphEditor = GraphEditor;
            BestGraph = GraphEditor->GetCurrentGraph();
        }
    }

    if (!BestGraphEditor.IsValid() || BestGraph == nullptr)
    {
        OutReason = TEXT("ACTIVE_GRAPH_UNRESOLVED");
        return false;
    }

    OutGraphEditor = BestGraphEditor;
    OutGraph = BestGraph;
    return true;
}

TSharedPtr<FJsonObject> MakeMaterialSelectedNodeJson(UObject* MaterialAsset, UMaterialExpression* Expression, UEdGraph* Graph)
{
    if (MaterialAsset == nullptr || Expression == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("id"), Expression->GetPathName());
    Item->SetStringField(TEXT("name"), Expression->GetName());
    Item->SetStringField(TEXT("title"), Expression->GetName());
    Item->SetStringField(TEXT("class"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
    Item->SetStringField(TEXT("path"), Expression->GetPathName());
    Item->SetNumberField(TEXT("nodePosX"), Expression->MaterialExpressionEditorX);
    Item->SetNumberField(TEXT("nodePosY"), Expression->MaterialExpressionEditorY);
    Item->SetObjectField(TEXT("position"), MakeGraphPositionJson(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY));
    if (Graph != nullptr)
    {
        Item->SetStringField(TEXT("graphName"), Graph->GetName());
        Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
    }
    FString AssetPath;
    if (TryGetAssetPathFromObject(MaterialAsset, AssetPath))
    {
        Item->SetObjectField(TEXT("graph"), MakeAssetGraphRefJson(AssetPath, Graph != nullptr ? Graph->GetName() : FString()));
    }
    return Item;
}

bool BuildMaterialEditorSelectionContextSnapshot(TSharedPtr<FJsonObject>& OutSnapshot)
{
    OutSnapshot.Reset();
    UObject* MaterialAsset = FindEditedMaterialAsset();
    if (MaterialAsset == nullptr)
    {
        return false;
    }

    TSharedPtr<SGraphEditor> GraphEditor;
    UEdGraph* ActiveGraph = nullptr;
    FString Reason;
    const bool bHasGraphEditor = ResolveActiveGraphEditorByDomain(EGraphSelectionDomain::Material, GraphEditor, ActiveGraph, Reason);

    OutSnapshot = MakeShared<FJsonObject>();
    OutSnapshot->SetBoolField(TEXT("isError"), false);
    OutSnapshot->SetObjectField(TEXT("activeEditor"), MakeActiveEditorJson(MaterialAsset, TEXT("material"), TEXT("material"), bHasGraphEditor ? TEXT("focusedGraphEditor") : TEXT("editedAsset")));
    OutSnapshot->SetObjectField(TEXT("activeAsset"), MakeActiveAssetJson(MaterialAsset, TEXT("material"), TEXT("material")));

    FString AssetPath;
    TryGetAssetPathFromObject(MaterialAsset, AssetPath);
    if (bHasGraphEditor)
    {
        TSharedPtr<FJsonObject> ActiveGraphRef = MakeAssetGraphRefJson(AssetPath, ActiveGraph != nullptr ? ActiveGraph->GetName() : FString());
        OutSnapshot->SetObjectField(TEXT("activeGraph"), ActiveGraphRef);
    }

    TSharedPtr<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetBoolField(TEXT("isError"), false);
    Selection->SetStringField(TEXT("editorType"), TEXT("material"));
    Selection->SetStringField(TEXT("provider"), TEXT("material"));
    Selection->SetStringField(TEXT("kind"), TEXT("graph"));
    Selection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
    Selection->SetStringField(TEXT("assetPath"), AssetPath);
    if (bHasGraphEditor)
    {
        Selection->SetObjectField(TEXT("activeGraph"), MakeAssetGraphRefJson(AssetPath, ActiveGraph != nullptr ? ActiveGraph->GetName() : FString()));
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    if (bHasGraphEditor && GraphEditor.IsValid())
    {
        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        for (UObject* SelectedObject : SelectedNodes)
        {
            UMaterialExpression* Expression = Cast<UMaterialExpression>(SelectedObject);
            if (Expression == nullptr)
            {
                if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(SelectedObject))
                {
                    Expression = GraphNode->MaterialExpression;
                }
            }
            if (Expression == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeObj = MakeMaterialSelectedNodeJson(MaterialAsset, Expression, ActiveGraph);
            if (!NodeObj.IsValid())
            {
                continue;
            }

            TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
            TSet<FString> ItemSeenGraphRefs;
            AppendMaterialGraphRefs(MaterialAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
            if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
            {
                AppendMaterialGraphRefs(FuncCall->MaterialFunction, TEXT("child"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
            }
            SetResolvedGraphRefsFieldIfAny(NodeObj, ItemResolvedGraphRefs);
            CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
            Items.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    Selection->SetArrayField(TEXT("nodes"), Nodes);
    Selection->SetArrayField(TEXT("items"), Items);
    Selection->SetNumberField(TEXT("count"), Nodes.Num());
    Selection->SetArrayField(TEXT("pins"), TArray<TSharedPtr<FJsonValue>>());
    Selection->SetStringField(TEXT("pinSelectionStatus"), TEXT("not_available_from_graph_editor_api"));
    Selection->SetStringField(TEXT("status"), Nodes.Num() > 0 ? TEXT("resolved") : TEXT("unavailable"));
    if (Nodes.Num() == 0)
    {
        Selection->SetStringField(TEXT("reason"), Reason.IsEmpty() ? TEXT("NO_SELECTED_NODES") : Reason);
    }
    SetResolvedGraphRefsFieldIfAny(Selection, ResolvedGraphRefs);
    OutSnapshot->SetObjectField(TEXT("selection"), Selection);
    return true;
}

TSharedPtr<FJsonObject> MakePcgSelectedNodeJson(UObject* PcgAsset, UPCGNode* PcgNode, UEdGraphNode* GraphNode)
{
    if (PcgAsset == nullptr || PcgNode == nullptr || GraphNode == nullptr)
    {
        return nullptr;
    }

    int32 NodePosX = GraphNode->NodePosX;
    int32 NodePosY = GraphNode->NodePosY;
    PcgNode->GetNodePosition(NodePosX, NodePosY);

    const UClass* NodeClass = PcgNode->GetClass();
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("id"), PcgNode->GetPathName());
    Item->SetStringField(TEXT("name"), PcgNode->NodeTitle.IsNone() ? PcgNode->GetName() : PcgNode->NodeTitle.ToString());
    Item->SetStringField(TEXT("title"), PcgNode->NodeTitle.IsNone() ? PcgNode->GetName() : PcgNode->NodeTitle.ToString());
    Item->SetStringField(TEXT("class"), NodeClass ? NodeClass->GetPathName() : TEXT(""));
    Item->SetStringField(TEXT("path"), PcgNode->GetPathName());
    Item->SetNumberField(TEXT("nodePosX"), NodePosX);
    Item->SetNumberField(TEXT("nodePosY"), NodePosY);
    Item->SetObjectField(TEXT("position"), MakeGraphPositionJson(NodePosX, NodePosY));
    if (UEdGraph* Graph = GraphNode->GetGraph())
    {
        Item->SetStringField(TEXT("graphName"), Graph->GetName());
        Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
    }
    FString AssetPath;
    if (TryGetAssetPathFromObject(PcgAsset, AssetPath))
    {
        Item->SetObjectField(TEXT("graph"), MakeAssetGraphRefJson(AssetPath));
    }
    return Item;
}

bool BuildPcgEditorSelectionContextSnapshot(TSharedPtr<FJsonObject>& OutSnapshot)
{
    OutSnapshot.Reset();
    UObject* PcgAsset = FindEditedPcgAsset();
    if (PcgAsset == nullptr)
    {
        return false;
    }

    TSharedPtr<SGraphEditor> GraphEditor;
    UEdGraph* ActiveGraph = nullptr;
    FString Reason;
    const bool bHasGraphEditor = ResolveActiveGraphEditorByDomain(EGraphSelectionDomain::Pcg, GraphEditor, ActiveGraph, Reason);

    FString AssetPath;
    TryGetAssetPathFromObject(PcgAsset, AssetPath);
    OutSnapshot = MakeShared<FJsonObject>();
    OutSnapshot->SetBoolField(TEXT("isError"), false);
    OutSnapshot->SetObjectField(TEXT("activeEditor"), MakeActiveEditorJson(PcgAsset, TEXT("pcg"), TEXT("pcg"), bHasGraphEditor ? TEXT("focusedGraphEditor") : TEXT("editedAsset")));
    OutSnapshot->SetObjectField(TEXT("activeAsset"), MakeActiveAssetJson(PcgAsset, TEXT("pcg"), TEXT("pcg")));
    if (bHasGraphEditor)
    {
        OutSnapshot->SetObjectField(TEXT("activeGraph"), MakeAssetGraphRefJson(AssetPath, ActiveGraph != nullptr ? ActiveGraph->GetName() : FString()));
    }

    TSharedPtr<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetBoolField(TEXT("isError"), false);
    Selection->SetStringField(TEXT("editorType"), TEXT("pcg"));
    Selection->SetStringField(TEXT("provider"), TEXT("pcg"));
    Selection->SetStringField(TEXT("kind"), TEXT("graph"));
    Selection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
    Selection->SetStringField(TEXT("assetPath"), AssetPath);
    if (bHasGraphEditor)
    {
        Selection->SetObjectField(TEXT("activeGraph"), MakeAssetGraphRefJson(AssetPath, ActiveGraph != nullptr ? ActiveGraph->GetName() : FString()));
    }

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    if (bHasGraphEditor && GraphEditor.IsValid())
    {
        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        for (UObject* SelectedObject : SelectedNodes)
        {
            UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedObject);
            UPCGNode* PcgNode = ResolvePcgNodeFromEditorNode(GraphNode);
            if (PcgNode == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> NodeObj = MakePcgSelectedNodeJson(PcgAsset, PcgNode, GraphNode);
            if (!NodeObj.IsValid())
            {
                continue;
            }

            TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
            TSet<FString> ItemSeenGraphRefs;
            AppendPcgGraphRefs(PcgAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
            AppendPcgSubgraphRefsFromNode(PcgNode, ItemResolvedGraphRefs, ItemSeenGraphRefs);
            SetResolvedGraphRefsFieldIfAny(NodeObj, ItemResolvedGraphRefs);
            CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeObj));
            Items.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }

    Selection->SetArrayField(TEXT("nodes"), Nodes);
    Selection->SetArrayField(TEXT("items"), Items);
    Selection->SetNumberField(TEXT("count"), Nodes.Num());
    Selection->SetArrayField(TEXT("pins"), TArray<TSharedPtr<FJsonValue>>());
    Selection->SetStringField(TEXT("pinSelectionStatus"), TEXT("not_available_from_graph_editor_api"));
    Selection->SetStringField(TEXT("status"), Nodes.Num() > 0 ? TEXT("resolved") : TEXT("unavailable"));
    if (Nodes.Num() == 0)
    {
        Selection->SetStringField(TEXT("reason"), Reason.IsEmpty() ? TEXT("NO_SELECTED_NODES") : Reason);
    }
    SetResolvedGraphRefsFieldIfAny(Selection, ResolvedGraphRefs);
    OutSnapshot->SetObjectField(TEXT("selection"), Selection);
    return true;
}

FWidgetBlueprintEditor* FindEditedWidgetBlueprintEditor(UWidgetBlueprint* WidgetBlueprint)
{
    if (GEditor == nullptr || WidgetBlueprint == nullptr)
    {
        return nullptr;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        return nullptr;
    }

    IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(WidgetBlueprint, true);
    return static_cast<FWidgetBlueprintEditor*>(EditorInstance);
}

TSharedPtr<FJsonObject> MakeWidgetSelectionJson(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget)
{
    if (WidgetBlueprint == nullptr || Widget == nullptr)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
    WidgetObj->SetStringField(TEXT("id"), Widget->GetName());
    WidgetObj->SetStringField(TEXT("name"), Widget->GetName());
    WidgetObj->SetStringField(TEXT("class"), Widget->GetClass() ? Widget->GetClass()->GetPathName() : TEXT(""));
    WidgetObj->SetStringField(TEXT("path"), Widget->GetPathName());
    WidgetObj->SetBoolField(TEXT("isVariable"), Widget->bIsVariable);
    if (UPanelSlot* Slot = Widget->Slot)
    {
        WidgetObj->SetStringField(TEXT("slotClass"), Slot->GetClass() ? Slot->GetClass()->GetPathName() : TEXT(""));
        WidgetObj->SetStringField(TEXT("slotName"), Slot->GetName());
    }

    FString AssetPath;
    if (TryGetAssetPathFromObject(WidgetBlueprint, AssetPath))
    {
        WidgetObj->SetStringField(TEXT("assetPath"), AssetPath);
    }
    return WidgetObj;
}

bool BuildWidgetDesignerSelectionContextSnapshot(TSharedPtr<FJsonObject>& OutSnapshot)
{
    OutSnapshot.Reset();
    UWidgetBlueprint* WidgetBlueprint = FindEditedWidgetBlueprint();
    if (WidgetBlueprint == nullptr)
    {
        return false;
    }

    FWidgetBlueprintEditor* WidgetEditor = FindEditedWidgetBlueprintEditor(WidgetBlueprint);
    if (WidgetEditor == nullptr)
    {
        return false;
    }

    FString AssetPath;
    TryGetAssetPathFromObject(WidgetBlueprint, AssetPath);

    OutSnapshot = MakeShared<FJsonObject>();
    OutSnapshot->SetBoolField(TEXT("isError"), false);
    OutSnapshot->SetObjectField(TEXT("activeEditor"), MakeActiveEditorJson(WidgetBlueprint, TEXT("widgetBlueprint"), TEXT("widget"), TEXT("widgetDesigner")));
    OutSnapshot->SetObjectField(TEXT("activeAsset"), MakeActiveAssetJson(WidgetBlueprint, TEXT("widgetBlueprint"), TEXT("widget")));

    TArray<TSharedPtr<FJsonValue>> Widgets;
    const TSet<FWidgetReference>& SelectedWidgets = WidgetEditor->GetSelectedWidgets();
    for (const FWidgetReference& WidgetRef : SelectedWidgets)
    {
        UWidget* TemplateWidget = WidgetRef.GetTemplate();
        TSharedPtr<FJsonObject> WidgetObj = MakeWidgetSelectionJson(WidgetBlueprint, TemplateWidget);
        if (WidgetObj.IsValid())
        {
            Widgets.Add(MakeShared<FJsonValueObject>(WidgetObj));
        }
    }

    TSharedPtr<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetBoolField(TEXT("isError"), false);
    Selection->SetStringField(TEXT("editorType"), TEXT("widgetBlueprint"));
    Selection->SetStringField(TEXT("provider"), TEXT("widget"));
    Selection->SetStringField(TEXT("kind"), TEXT("widgetTree"));
    Selection->SetStringField(TEXT("selectionKind"), TEXT("widget"));
    Selection->SetStringField(TEXT("assetPath"), AssetPath);
    Selection->SetArrayField(TEXT("widgets"), Widgets);
    Selection->SetArrayField(TEXT("items"), Widgets);
    Selection->SetNumberField(TEXT("count"), Widgets.Num());
    Selection->SetStringField(TEXT("status"), Widgets.Num() > 0 ? TEXT("resolved") : TEXT("unavailable"));
    if (Widgets.Num() == 0)
    {
        Selection->SetStringField(TEXT("reason"), TEXT("NO_SELECTED_WIDGETS"));
    }
    OutSnapshot->SetObjectField(TEXT("selection"), Selection);
    return true;
}

bool CollectSelectedMaterialExpressions(TArray<UMaterialExpression*>& OutExpressions, UObject*& OutMaterialAsset)
{
    OutExpressions.Reset();
    OutMaterialAsset = nullptr;

    UObject* EditedMaterialAsset = FindEditedMaterialAsset();
    if (!EditedMaterialAsset)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Material)
    {
        return false;
    }

    for (UObject* SelectedObject : SelectedObjects)
    {
        UMaterialExpression* Expression = Cast<UMaterialExpression>(SelectedObject);
        if (!Expression)
        {
            if (UMaterialGraphNode* GraphNode = Cast<UMaterialGraphNode>(SelectedObject))
            {
                Expression = GraphNode->MaterialExpression;
            }
        }

        if (Expression)
        {
            OutExpressions.Add(Expression);
        }
    }

    OutMaterialAsset = EditedMaterialAsset;
    return OutExpressions.Num() > 0 && OutMaterialAsset != nullptr;
}

bool BuildMaterialSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();
    TArray<UMaterialExpression*> SelectedExpressions;
    UObject* MaterialAsset = nullptr;
    if (!CollectSelectedMaterialExpressions(SelectedExpressions, MaterialAsset))
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("material"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("material"));
    OutSelection->SetStringField(TEXT("assetPath"), MaterialAsset->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    Items.Reserve(SelectedExpressions.Num());
    for (UMaterialExpression* Expression : SelectedExpressions)
    {
        if (!Expression)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Expression->GetPathName());
        Item->SetStringField(TEXT("name"), Expression->GetName());
        Item->SetStringField(TEXT("class"), Expression->GetClass() ? Expression->GetClass()->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), Expression->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), Expression->MaterialExpressionEditorX);
        Item->SetNumberField(TEXT("nodePosY"), Expression->MaterialExpressionEditorY);

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        AppendMaterialGraphRefs(MaterialAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        if (UMaterialExpressionMaterialFunctionCall* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
        {
            AppendMaterialGraphRefs(FuncCall->MaterialFunction, TEXT("child"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        }
        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
    return true;
}

TSharedPtr<SWindow> GetActiveWindow()
{
    if (!FSlateApplication::IsInitialized())
    {
        return nullptr;
    }

    TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        const TSharedPtr<SWidget> FocusedWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid())
        {
            ActiveWindow = FSlateApplication::Get().FindWidgetWindow(FocusedWidget.ToSharedRef());
        }
    }
    return ActiveWindow;
}

void CollectGraphEditorsFromWidgetTree(const TSharedRef<SWidget>& RootWidget, TArray<TSharedPtr<SGraphEditor>>& OutGraphEditors)
{
    TArray<TSharedRef<SWidget>> Stack;
    Stack.Add(RootWidget);

    while (Stack.Num() > 0)
    {
        const TSharedRef<SWidget> CurrentWidget = Stack.Pop(EAllowShrinking::No);

        if (CurrentWidget->GetType() == FName(TEXT("SGraphEditor")))
        {
            OutGraphEditors.Add(StaticCastSharedRef<SGraphEditor>(CurrentWidget));
        }

        FChildren* Children = CurrentWidget->GetAllChildren();
        if (!Children)
        {
            continue;
        }

        for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
        {
            TSharedRef<SWidget> ChildWidget = Children->GetChildAt(ChildIndex);
            Stack.Add(ChildWidget);
        }
    }
}

EGraphSelectionDomain DetectGraphSelectionDomain(const TArray<UObject*>& SelectedObjects)
{
    bool bHasPcgNode = false;
    bool bHasMaterialNode = false;
    bool bHasGenericGraphNode = false;

    for (UObject* SelectedObject : SelectedObjects)
    {
        if (!SelectedObject)
        {
            continue;
        }

        if (SelectedObject->IsA<UMaterialExpression>() || SelectedObject->IsA<UMaterialGraphNode>())
        {
            bHasMaterialNode = true;
        }

        if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedObject))
        {
            bHasGenericGraphNode = true;

            if (GraphNode->IsA<UMaterialGraphNode>())
            {
                bHasMaterialNode = true;
            }

            const UClass* NodeClass = GraphNode->GetClass();
            const FString NodeClassPath = NodeClass ? NodeClass->GetPathName() : FString();
            if (NodeClassPath.Contains(TEXT("PCGEditorGraphNode")))
            {
                bHasPcgNode = true;
            }
        }
    }

    if (bHasPcgNode)
    {
        return EGraphSelectionDomain::Pcg;
    }
    if (bHasMaterialNode)
    {
        return EGraphSelectionDomain::Material;
    }
    if (bHasGenericGraphNode)
    {
        return EGraphSelectionDomain::Blueprint;
    }
    return EGraphSelectionDomain::Unknown;
}

bool CollectSelectedGraphObjectsFromActiveWindow(TArray<UObject*>& OutSelectedObjects, EGraphSelectionDomain& OutDomain)
{
    OutSelectedObjects.Reset();
    OutDomain = EGraphSelectionDomain::Unknown;

    TSharedPtr<SWindow> ActiveWindow = GetActiveWindow();
    if (!ActiveWindow.IsValid())
    {
        return false;
    }

    TArray<TSharedPtr<SGraphEditor>> GraphEditors;
    CollectGraphEditorsFromWidgetTree(ActiveWindow.ToSharedRef(), GraphEditors);

    int32 BestScore = TNumericLimits<int32>::Min();
    TArray<UObject*> BestSelection;
    EGraphSelectionDomain BestDomain = EGraphSelectionDomain::Unknown;

    for (const TSharedPtr<SGraphEditor>& GraphEditor : GraphEditors)
    {
        if (!GraphEditor.IsValid())
        {
            continue;
        }

        const FGraphPanelSelectionSet& SelectedNodes = GraphEditor->GetSelectedNodes();
        if (SelectedNodes.Num() == 0)
        {
            continue;
        }

        TArray<UObject*> CurrentSelection;
        CurrentSelection.Reserve(SelectedNodes.Num());
        for (UObject* SelectedObject : SelectedNodes)
        {
            if (!SelectedObject)
            {
                continue;
            }
            CurrentSelection.Add(SelectedObject);
        }

        if (CurrentSelection.Num() == 0)
        {
            continue;
        }

        const EGraphSelectionDomain CurrentDomain = DetectGraphSelectionDomain(CurrentSelection);
        int32 Score = CurrentSelection.Num();
        if (CurrentDomain != EGraphSelectionDomain::Unknown)
        {
            Score += 1000;
        }
        if (CurrentDomain == EGraphSelectionDomain::Material || CurrentDomain == EGraphSelectionDomain::Pcg)
        {
            Score += 100;
        }

        if (Score > BestScore)
        {
            BestScore = Score;
            BestSelection = MoveTemp(CurrentSelection);
            BestDomain = CurrentDomain;
        }
    }

    if (BestSelection.Num() == 0)
    {
        return false;
    }

    OutSelectedObjects = MoveTemp(BestSelection);
    OutDomain = BestDomain;
    return true;
}

UPCGNode* ResolvePcgNodeFromEditorNode(UEdGraphNode* GraphNode)
{
    if (!GraphNode)
    {
        return nullptr;
    }

    FObjectPropertyBase* PcgNodeProperty = FindFProperty<FObjectPropertyBase>(GraphNode->GetClass(), TEXT("PCGNode"));
    if (!PcgNodeProperty)
    {
        return nullptr;
    }

    UObject* PcgNodeObject = PcgNodeProperty->GetObjectPropertyValue_InContainer(GraphNode);
    return Cast<UPCGNode>(PcgNodeObject);
}

bool BuildPcgSelectionSnapshot(TSharedPtr<FJsonObject>& OutSelection)
{
    OutSelection.Reset();

    UObject* PcgAsset = FindEditedPcgAsset();
    if (!PcgAsset)
    {
        return false;
    }

    TArray<UObject*> SelectedObjects;
    EGraphSelectionDomain SelectedDomain = EGraphSelectionDomain::Unknown;
    if (!CollectSelectedGraphObjectsFromActiveWindow(SelectedObjects, SelectedDomain)
        || SelectedDomain != EGraphSelectionDomain::Pcg)
    {
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    Items.Reserve(SelectedObjects.Num());

    for (UObject* SelectedObject : SelectedObjects)
    {
        UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedObject);
        if (!GraphNode)
        {
            continue;
        }

        UPCGNode* PcgNode = ResolvePcgNodeFromEditorNode(GraphNode);
        if (!PcgNode)
        {
            continue;
        }

        int32 NodePosX = GraphNode->NodePosX;
        int32 NodePosY = GraphNode->NodePosY;
        PcgNode->GetNodePosition(NodePosX, NodePosY);

        const UClass* NodeClass = PcgNode->GetClass();
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), PcgNode->GetPathName());
        Item->SetStringField(TEXT("name"), PcgNode->NodeTitle.IsNone() ? PcgNode->GetName() : PcgNode->NodeTitle.ToString());
        Item->SetStringField(TEXT("class"), NodeClass ? NodeClass->GetPathName() : TEXT(""));
        Item->SetStringField(TEXT("path"), PcgNode->GetPathName());
        Item->SetNumberField(TEXT("nodePosX"), NodePosX);
        Item->SetNumberField(TEXT("nodePosY"), NodePosY);
        if (UEdGraph* Graph = GraphNode->GetGraph())
        {
            Item->SetStringField(TEXT("graphName"), Graph->GetName());
            Item->SetStringField(TEXT("graphPath"), Graph->GetPathName());
        }

        TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
        TSet<FString> ItemSeenGraphRefs;
        AppendPcgGraphRefs(PcgAsset, TEXT("selected_graph"), ItemResolvedGraphRefs, ItemSeenGraphRefs);
        AppendPcgSubgraphRefsFromNode(PcgNode, ItemResolvedGraphRefs, ItemSeenGraphRefs);
        SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
        CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    if (Items.Num() == 0)
    {
        return false;
    }

    OutSelection = MakeShared<FJsonObject>();
    OutSelection->SetBoolField(TEXT("isError"), false);
    OutSelection->SetStringField(TEXT("editorType"), TEXT("pcg"));
    OutSelection->SetStringField(TEXT("provider"), TEXT("pcg"));
    OutSelection->SetStringField(TEXT("assetPath"), PcgAsset->GetPathName());
    OutSelection->SetStringField(TEXT("selectionKind"), TEXT("graph_node"));
    OutSelection->SetArrayField(TEXT("items"), Items);
    OutSelection->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(OutSelection, ResolvedGraphRefs);
    return true;
}

}

bool FLoomleBridgeModule::TickHealthSnapshot(float DeltaTime)
{
    (void)DeltaTime;
    UpdateHealthSnapshot();
    return true;
}

void FLoomleBridgeModule::UpdateHealthSnapshot()
{
    const bool bBridgeRunning = PipeServer.IsValid();
    const IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    const bool bPythonReady = PythonScriptPlugin != nullptr && PythonScriptPlugin->IsPythonInitialized();
    const bool bIsPIE = GEditor != nullptr && GEditor->IsPlayingSessionInEditor();

    bBridgeRunningSnapshot.Store(bBridgeRunning);
    bPythonReadySnapshot.Store(bPythonReady);
    bIsPIESnapshot.Store(bIsPIE);
}

void FLoomleBridgeModule::RegisterStatusBarWidget()
{
    if (!UToolMenus::IsToolMenuUIEnabled())
    {
        return;
    }

    RegisterStatusBarMenus();
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
        UE_LOG(LogLoomleBridge, Warning, TEXT("Unable to register Loomle status bar icon: plugin descriptor not found."));
        return;
    }

    LoomleSlateStyle = MakeShared<FSlateStyleSet>(LoomleBridgeConstants::SlateStyleSetName);
    LoomleSlateStyle->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
    LoomleSlateStyle->Set(
        LoomleBridgeConstants::StatusBarIconBrushName,
        new FSlateImageBrush(
            LoomleSlateStyle->RootToContentDir(TEXT("LoomleToolbarIcon"), TEXT(".png")),
            FVector2D(16.0f, 16.0f)));
    FSlateStyleRegistry::RegisterSlateStyle(*LoomleSlateStyle);
}

void FLoomleBridgeModule::UnregisterLoomleSlateStyle()
{
    if (!LoomleSlateStyle.IsValid())
    {
        return;
    }

    FSlateStyleRegistry::UnRegisterSlateStyle(*LoomleSlateStyle);
    LoomleSlateStyle.Reset();
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
    Section.AddDynamicEntry(TEXT("LoomleBridgeStatusEntry"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
    {
        InSection.AddEntry(FToolMenuEntry::InitWidget(
            TEXT("LoomleBridgeStatusWidget"),
            SNew(SComboButton)
            .HasDownArrow(false)
            .ContentPadding(FMargin(6.0f, 0.0f))
            .ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(TEXT("SimpleComboButton")))
            .MenuPlacement(MenuPlacement_AboveAnchor)
            .OnGetMenuContent_Raw(this, &FLoomleBridgeModule::CreateSetupStatusPanel)
            .ButtonContent()
            [
                CreateStatusBarButtonContent()
            ],
            FText::GetEmpty(),
            true,
            false));
    }));
}

TSharedRef<SWidget> FLoomleBridgeModule::CreateStatusBarButtonContent()
{
    return SNew(SHorizontalBox)
        .ToolTipText_Lambda([this]()
        {
            return GetToolbarStatusTooltip();
        })
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 4.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(16.0f)
            .HeightOverride(16.0f)
            [
                SNew(SImage)
                .Image_Lambda([this]() -> const FSlateBrush*
                {
                    if (LoomleSlateStyle.IsValid())
                    {
                        return LoomleSlateStyle->GetBrush(LoomleBridgeConstants::StatusBarIconBrushName);
                    }
                    return FAppStyle::GetBrush(TEXT("Icons.Warning"));
                })
                .ColorAndOpacity_Lambda([this]()
                {
                    return GetToolbarStatusColor();
                })
            ]
        ]
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Loomle")))
            .ToolTipText_Lambda([this]()
            {
                return GetToolbarStatusTooltip();
            })
        ];
}

TSharedRef<SWidget> FLoomleBridgeModule::CreateSetupStatusPanel()
{
    const FString PluginBaseDir = GetLoomleBridgePluginBaseDir();
    const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString InstallScope = GetLoomleBridgePluginInstallScope(ProjectRoot, PluginBaseDir);
    const FString ManagedBy = GetLoomleBridgePluginManagedBy(ProjectRoot, PluginBaseDir);
    const bool bFabPythonAvailable = HasFabPythonMcpServer();
    const bool bReady = bBridgeRunningSnapshot.Load() && bPythonReadySnapshot.Load();
    const int32 ActiveConnectionCount = PipeServer.IsValid() ? PipeServer->GetActiveConnectionCount() : 0;
    const bool bClientConnected = ActiveConnectionCount > 0;
    const FString BridgeStatus = GetToolbarStatusKey().ToLower();
    FString ClientStatus;
    FString ClientDetail;
    bool bClientRecent = false;
    const bool bHasAnyClientActivity = GetClientActivitySummary(ClientStatus, ClientDetail, bClientRecent);
    if (bClientConnected)
    {
        ClientStatus = FString::Printf(TEXT("connected (%d)"), ActiveConnectionCount);
    }
    else if (bHasAnyClientActivity)
    {
        ClientStatus = TEXT("not connected");
    }
    const FString LastActivityText = bHasAnyClientActivity ? ClientDetail : TEXT("none since editor start");
    const FSetupAutoConfigureSummary SetupSummary = bReady ? AutoConfigureDetectedHostsIfSafe() : FSetupAutoConfigureSummary();

    TArray<FString> ConfiguredHosts;
    TArray<FString> ExistingHosts;
    TArray<FString> BlockedMessages;
    for (const FLoomleHostSetupResult& Host : SetupSummary.Hosts)
    {
        if (Host.bChanged)
        {
            ConfiguredHosts.Add(Host.Host);
        }
        else if (Host.ExistingOwner != ELoomleMcpEntryOwner::None)
        {
            ExistingHosts.Add(Host.Host);
        }
        else if (Host.bError)
        {
            BlockedMessages.AddUnique(Host.Message);
        }
    }

    FString PanelTitle = TEXT("Loomle ") + GetToolbarStatusKey();
    FString MainText = GetSetupPanelNextActionText().ToString();
    const bool bShowSetupPrompt = bReady && !SetupSummary.bAnyDetected;
    if (bReady)
    {
        if (bClientConnected)
        {
            MainText = TEXT("Bridge is ready.\n\nAI client connected to this Unreal project.");
        }
        else if (!ConfiguredHosts.IsEmpty())
        {
            MainText = FString::Printf(
                TEXT("Bridge is ready.\n\n%s %s configured. Restart or refresh your AI tool."),
                *FString::Join(ConfiguredHosts, TEXT(" and ")),
                ConfiguredHosts.Num() == 1 ? TEXT("was") : TEXT("were"));
        }
        else if (!ExistingHosts.IsEmpty())
        {
            MainText = TEXT("Bridge is ready.\n\nMCP setup found. No AI client is connected right now.");
        }
        else if (SetupSummary.bAnyDetected && SetupSummary.bAnyBlocked)
        {
            if (!HasSetupPanelMcpServerPayload())
            {
                MainText = TEXT("Bridge is ready.\n\nMCP setup found, but no MCP server payload is available.");
            }
            else
            {
                MainText = BlockedMessages.IsEmpty()
                ? TEXT("Bridge is ready.\n\nMCP config found. Loomle did not change it automatically.")
                : FString::Printf(TEXT("Bridge is ready.\n\n%s"), *FString::Join(BlockedMessages, TEXT("\n")));
            }
        }
        else
        {
            MainText = TEXT("Bridge is ready.\n\nNo MCP setup was found. Copy the setup prompt into your AI tool to get started.");
        }
    }

    const FString AdvancedText = FString::Printf(
        TEXT("Bridge: %s\nProject: %s\nEndpoint: %s\n\nMCP setup: %s\nMCP connection: %s\nLast activity: %s\n\nPlugin: %s\nVersion: %s\nScope: %s\nManaged by: %s\nPython MCP: %s\n\nCodex config: %s\nClaude config: %s"),
        *BridgeStatus,
        FApp::GetProjectName(),
        *GetRuntimeEndpointDisplayString(),
        SetupSummary.bAnyDetected ? TEXT("found") : TEXT("not found"),
        *ClientStatus,
        *LastActivityText,
        *PluginBaseDir,
        *GetLoomleBridgePluginVersion(),
        *InstallScope,
        *ManagedBy,
        bFabPythonAvailable ? TEXT("available") : TEXT("missing"),
        *GetCodexConfigPath(),
        *GetClaudeDesktopConfigPath());

    return SNew(SBox)
        .WidthOverride(460.0f)
        [
            SNew(SBorder)
            .Padding(12.0f)
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(PanelTitle))
                    .Font(FAppStyle::GetFontStyle(TEXT("HeadingExtraSmall")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(MainText))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Visibility(bShowSetupPrompt ? EVisibility::Visible : EVisibility::Collapsed)
                    .Text(FText::FromString(TEXT("Copy Setup Prompt")))
                    .OnClicked_Lambda([this]()
                    {
                        return CopySetupPrompt();
                    })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SNew(SExpandableArea)
                    .InitiallyCollapsed(true)
                    .HeaderContent()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Advanced details")))
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                    ]
                    .BodyContent()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(AdvancedText))
                        .AutoWrapText(true)
                        .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                    ]
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

bool FLoomleBridgeModule::GetClientActivitySummary(FString& OutStatus, FString& OutDetail, bool& bOutRecent) const
{
    FScopeLock Lock(&ClientActivityMutex);
    if (!bHasClientActivity)
    {
        OutStatus = TEXT("no activity yet");
        OutDetail = TEXT("none since editor start");
        bOutRecent = false;
        return false;
    }

    const FTimespan Age = FDateTime::UtcNow() - LastClientActivityAt;
    const double AgeSeconds = Age.GetTotalSeconds();
    bOutRecent = AgeSeconds <= 120.0;
    OutStatus = bOutRecent ? TEXT("active") : TEXT("idle");

    FString AgeText;
    if (AgeSeconds < 1.0)
    {
        AgeText = TEXT("just now");
    }
    else if (AgeSeconds < 60.0)
    {
        AgeText = FString::Printf(TEXT("%d seconds ago"), FMath::Max(1, FMath::FloorToInt(AgeSeconds)));
    }
    else
    {
        AgeText = FString::Printf(TEXT("%d minutes ago"), FMath::Max(1, FMath::FloorToInt(AgeSeconds / 60.0)));
    }

    FString LastCall = LastClientMethod;
    if (!LastClientTool.IsEmpty())
    {
        LastCall += TEXT(" / ");
        LastCall += LastClientTool;
    }
    OutDetail = FString::Printf(TEXT("%s, %s. Calls since editor start: %llu"), *LastCall, *AgeText, static_cast<unsigned long long>(ClientActivityCount));
    return true;
}

FText FLoomleBridgeModule::GetToolbarStatusLabel() const
{
    return FText::FromString(FString::Printf(TEXT("Loomle %s"), *GetToolbarStatusKey()));
}

FText FLoomleBridgeModule::GetToolbarStatusTooltip() const
{
    return FText::FromString(FString::Printf(
        TEXT("Loomle %s - Click for MCP status."),
        *GetToolbarStatusKey()));
}

FSlateColor FLoomleBridgeModule::GetToolbarStatusColor() const
{
    const FString Key = GetToolbarStatusKey();
    if (Key.Equals(TEXT("Ready")))
    {
        return FSlateColor(FLinearColor(0.15f, 0.55f, 0.22f, 1.0f));
    }
    if (Key.Equals(TEXT("Starting")))
    {
        return FSlateColor(FLinearColor(0.75f, 0.55f, 0.15f, 1.0f));
    }
    if (Key.Equals(TEXT("PIE")))
    {
        return FSlateColor(FLinearColor(0.16f, 0.36f, 0.78f, 1.0f));
    }
    if (Key.Equals(TEXT("Degraded")))
    {
        return FSlateColor(FLinearColor(0.78f, 0.42f, 0.12f, 1.0f));
    }
    return FSlateColor(FLinearColor(0.45f, 0.18f, 0.18f, 1.0f));
}

FString FLoomleBridgeModule::GetToolbarStatusKey() const
{
    const bool bBridgeRunning = bBridgeRunningSnapshot.Load();
    const bool bPythonReady = bPythonReadySnapshot.Load();
    const bool bIsPIE = bIsPIESnapshot.Load();

    if (!bBridgeRunning)
    {
        return TEXT("Offline");
    }
    if (bBridgeRunning && !bPythonReady)
    {
        return TEXT("Starting");
    }
    if (bBridgeRunning && bPythonReady && bIsPIE)
    {
        return TEXT("PIE");
    }
    if (bBridgeRunning && bPythonReady)
    {
        return TEXT("Ready");
    }
    return TEXT("Degraded");
}

FString FLoomleBridgeModule::GetRuntimeEndpointDisplayString() const
{
#if PLATFORM_WINDOWS
    return FString::Printf(TEXT("\\\\.\\pipe\\%s"), *GetRpcPipeNameForCurrentProject());
#else
    return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock"));
#endif
}

FText FLoomleBridgeModule::GetSetupPanelNextActionText() const
{
    if (!bBridgeRunningSnapshot.Load())
    {
        return FText::FromString(TEXT("Bridge is offline. Make sure the LoomleBridge plugin is enabled and restart the editor if needed."));
    }
    if (!bPythonReadySnapshot.Load())
    {
        return FText::FromString(TEXT("Bridge is starting. Wait for Unreal Python to finish initializing, then refresh your MCP host."));
    }
    if (IsNativeLoomleConfigured())
    {
        return FText::FromString(TEXT("Loomle is ready. Your existing Loomle MCP setup can connect to this Unreal project."));
    }
    if (HasFabPythonMcpServer())
    {
        return FText::FromString(TEXT("Loomle is ready. I will configure detected AI tools automatically."));
    }
    if (!FPaths::FileExists(GetNativeLoomleCliPath()))
    {
        return FText::FromString(TEXT("Loomle Bridge is ready, but no MCP server payload is available for automatic setup."));
    }
    return FText::FromString(TEXT("Loomle is ready. Install native LOOMLE or use a Fab package with bundled Python MCP to connect an AI tool."));
}

FString FLoomleBridgeModule::GetSetupPanelSetupPrompt() const
{
    return TEXT("Set up Loomle MCP for this machine if needed, then use Loomle to attach to my open Unreal project and read the current project context before making changes. If setup is not complete, follow https://loomle.ai/install.html and configure an MCP server named \"loomle\".");
}

FReply FLoomleBridgeModule::CopySetupPrompt()
{
    FPlatformApplicationMisc::ClipboardCopy(*GetSetupPanelSetupPrompt());
    return FReply::Handled();
}

void FLoomleBridgeModule::WriteProjectRegistration(const FString& ProjectRoot, const FString& ProjectId)
{
    const FString LoomleRoot = FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".loomle"));
    const FString ProjectDir = FPaths::Combine(LoomleRoot, TEXT("state"), TEXT("projects"));
    IFileManager::Get().MakeDirectory(*ProjectDir, true);

    const FString RegistrationPath = FPaths::Combine(ProjectDir, ProjectId + TEXT(".json"));
    const FString TempPath = RegistrationPath + TEXT(".tmp");
    FString Existing;
    FFileHelper::LoadFileToString(Existing, *RegistrationPath);
    TSharedPtr<FJsonObject> ExistingRecord;
    if (!Existing.IsEmpty())
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Existing);
        FJsonSerializer::Deserialize(Reader, ExistingRecord);
    }

    const FString Now = FDateTime::UtcNow().ToIso8601();
    FString RegisteredAt = Now;
    if (ExistingRecord.IsValid())
    {
        FString ExistingRegisteredAt;
        if (ExistingRecord->TryGetStringField(TEXT("registeredAt"), ExistingRegisteredAt) && !ExistingRegisteredAt.IsEmpty())
        {
            RegisteredAt = ExistingRegisteredAt;
        }
    }

    TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetNumberField(TEXT("schemaVersion"), 1);
    Record->SetStringField(TEXT("projectId"), ProjectId);
    Record->SetStringField(TEXT("name"), FApp::GetProjectName());
    Record->SetStringField(TEXT("projectRoot"), ProjectRoot);
    Record->SetStringField(TEXT("uproject"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    const FString PluginBaseDir = GetLoomleBridgePluginBaseDir();
    Record->SetStringField(TEXT("pluginPath"), PluginBaseDir);
    Record->SetStringField(TEXT("pluginInstallScope"), GetLoomleBridgePluginInstallScope(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginManagedBy"), GetLoomleBridgePluginManagedBy(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginVersion"), GetLoomleBridgePluginVersion());
    Record->SetStringField(TEXT("platform"), GetLoomlePlatformName());
    Record->SetStringField(TEXT("registeredAt"), RegisteredAt);
    Record->SetStringField(TEXT("lastSeenAt"), Now);
    Record->SetStringField(TEXT("source"), TEXT("runtime"));

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Record.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Output + TEXT("\n"), *TempPath))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to write LOOMLE project registration temp file %s"), *TempPath);
        return;
    }
    if (!IFileManager::Get().Move(*RegistrationPath, *TempPath, true, true))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to publish LOOMLE project registration %s"), *RegistrationPath);
        IFileManager::Get().Delete(*TempPath, false, true);
        return;
    }
}

void FLoomleBridgeModule::WriteRuntimeRegistration()
{
    FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeFilename(ProjectRoot);
    while (ProjectRoot.EndsWith(TEXT("/")))
    {
        ProjectRoot.LeftChopInline(1, EAllowShrinking::No);
    }
    if (ProjectRoot.IsEmpty())
    {
        ProjectRoot = TEXT("/");
    }

    FString NormalizedProjectRoot = ProjectRoot;
    NormalizedProjectRoot.ToLowerInline();
    const FString ProjectId = FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(StableFnv1a64(NormalizedProjectRoot)));
    const FString RuntimeId = ProjectId;
    const FString LoomleRoot = FPaths::Combine(GetLoomleHomeDirectory(), TEXT(".loomle"));
    const FString RuntimeDir = FPaths::Combine(LoomleRoot, TEXT("state"), TEXT("runtimes"));
    IFileManager::Get().MakeDirectory(*RuntimeDir, true);

    RuntimeRegistrationPath = FPaths::Combine(RuntimeDir, RuntimeId + TEXT(".json"));
    const FString TempPath = RuntimeRegistrationPath + TEXT(".tmp");

    TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetNumberField(TEXT("schemaVersion"), 1);
    Record->SetStringField(TEXT("runtimeId"), RuntimeId);
    Record->SetStringField(TEXT("projectId"), ProjectId);
    Record->SetStringField(TEXT("name"), FApp::GetProjectName());
    Record->SetStringField(TEXT("projectRoot"), ProjectRoot);
    Record->SetStringField(TEXT("uproject"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    FString Endpoint = GetRuntimeEndpointDisplayString();
#if !PLATFORM_WINDOWS
    Endpoint = FPaths::ConvertRelativePathToFull(Endpoint);
#endif
    Record->SetStringField(TEXT("endpoint"), Endpoint);
    Record->SetStringField(TEXT("platform"), GetLoomlePlatformName());
    Record->SetNumberField(TEXT("pid"), static_cast<double>(FPlatformProcess::GetCurrentProcessId()));
    const FString PluginBaseDir = GetLoomleBridgePluginBaseDir();
    Record->SetStringField(TEXT("pluginPath"), PluginBaseDir);
    Record->SetStringField(TEXT("pluginInstallScope"), GetLoomleBridgePluginInstallScope(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginManagedBy"), GetLoomleBridgePluginManagedBy(ProjectRoot, PluginBaseDir));
    Record->SetStringField(TEXT("pluginVersion"), GetLoomleBridgePluginVersion());
    Record->SetNumberField(TEXT("protocolVersion"), LoomleBridgeConstants::ProtocolVersion);
    const FString Now = FDateTime::UtcNow().ToIso8601();
    Record->SetStringField(TEXT("startedAt"), Now);
    Record->SetStringField(TEXT("lastSeenAt"), Now);

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Record.ToSharedRef(), Writer);

    if (!FFileHelper::SaveStringToFile(Output + TEXT("\n"), *TempPath))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to write LOOMLE runtime registration temp file %s"), *TempPath);
        return;
    }
    if (!IFileManager::Get().Move(*RuntimeRegistrationPath, *TempPath, true, true))
    {
        UE_LOG(LogLoomleBridge, Warning, TEXT("Failed to publish LOOMLE runtime registration %s"), *RuntimeRegistrationPath);
        IFileManager::Get().Delete(*TempPath, false, true);
        return;
    }

    UE_LOG(LogLoomleBridge, Display, TEXT("LOOMLE runtime registered at %s"), *RuntimeRegistrationPath);
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

void FLoomleBridgeModule::CleanupExecutePythonGlobalsForShutdown()
{
    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr || !PythonScriptPlugin->IsPythonInitialized())
    {
        return;
    }

    FPythonCommandEx PythonCommand;
    PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.Command =
        TEXT("import gc, signal\n")
        TEXT("for _loomle_name in [\n")
        TEXT("    '_loomle_original_load_asset',\n")
        TEXT("    '_loomle_original_editor_load_asset',\n")
        TEXT("    '_loomle_patched_load_asset',\n")
        TEXT("    '_loomle_patched_editor_load_asset',\n")
        TEXT("    '_loomle_unreal',\n")
        TEXT("    '_loomle_safe_load_asset',\n")
        TEXT("    '_loomle_safe_editor_load_asset',\n")
        TEXT("    '_loomle_validate_asset_load_path',\n")
        TEXT("    '_loomle_timeout_handler',\n")
        TEXT("    '_LOOMLE_TIMEOUT',\n")
        TEXT("    '_LOOMLE_PREV_SIGALRM',\n")
        TEXT("]:\n")
        TEXT("    globals().pop(_loomle_name, None)\n")
        TEXT("globals().pop('_loomle_name', None)\n")
        TEXT("try:\n")
        TEXT("    signal.alarm(0)\n")
        TEXT("except Exception:\n")
        TEXT("    pass\n")
        TEXT("gc.collect()\n");
    PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
}

void FLoomleBridgeModule::StopBridgeRuntime(bool bWaitForWorkers)
{
    RemoveRuntimeRegistration();

    {
        FScopeLock Lock(&JobRegistryMutex);
        JobQueue.Reset();
    }

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

    bBridgeRunningSnapshot.Store(false);
    bPythonReadySnapshot.Store(false);
    bIsPIESnapshot.Store(false);
}

void FLoomleBridgeModule::HandlePreExit()
{
    bIsShuttingDown.Store(true);
    StopBridgeRuntime(false);
    CleanupExecutePythonGlobalsForShutdown();
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorMutationLifecycleBlockResult(
    const FString& ToolName,
    const TSharedPtr<FJsonObject>& Arguments,
    const FString& AssetPath,
    const FString& GraphName)
{
    auto ReadStringField = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName) -> FString
    {
        FString Value;
        if (Object.IsValid())
        {
            Object->TryGetStringField(FieldName, Value);
        }
        return Value;
    };

    FString Code;
    FString Message;
    if (bIsShuttingDown.Load())
    {
        Code = TEXT("EDITOR_SHUTTING_DOWN");
        Message = TEXT("Editor is shutting down; graph/tree edits are not safe during shutdown.");
    }
    else if (GEditor == nullptr)
    {
        Code = TEXT("EDITOR_UNAVAILABLE");
        Message = TEXT("Editor is unavailable; graph/tree edits require an active editor.");
    }
    else if (GEditor->ShouldEndPlayMap())
    {
        Code = TEXT("PIE_STOPPING");
        Message = TEXT("Play-in-editor is stopping; retry after the editor returns to edit mode.");
    }
    else if (GEditor->IsPlaySessionRequestQueued())
    {
        Code = TEXT("PIE_STARTING");
        Message = TEXT("Play-in-editor is starting; retry after the editor reaches a stable play or edit state.");
    }
    else if (GEditor->IsPlaySessionInProgress() || GEditor->IsPlayingSessionInEditor() || GEditor->PlayWorld != nullptr)
    {
        Code = TEXT("PIE_ACTIVE");
        Message = TEXT("Play-in-editor is active; graph/tree edits are only allowed in edit mode.");
    }

    if (Code.IsEmpty())
    {
        return nullptr;
    }

    TArray<FString> CommandKinds;
    bool bCompileRequested = false;
    bool bSaveRequested = false;
    bool bLayoutRequested = false;
    auto AddCommandKind = [&](const FString& RawKind)
    {
        const FString Kind = RawKind.TrimStartAndEnd();
        if (Kind.IsEmpty())
        {
            return;
        }
        CommandKinds.Add(Kind);
        const FString LowerKind = Kind.ToLower();
        bCompileRequested = bCompileRequested || LowerKind.Contains(TEXT("compile"));
        bSaveRequested = bSaveRequested || LowerKind.Contains(TEXT("save"));
        bLayoutRequested = bLayoutRequested || LowerKind.Contains(TEXT("layout")) || LowerKind.Contains(TEXT("format"));
    };

    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("ops"), Ops) && Ops != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& OpValue : *Ops)
        {
            const TSharedPtr<FJsonObject>* OpObject = nullptr;
            if (!OpValue.IsValid() || !OpValue->TryGetObject(OpObject) || OpObject == nullptr || !(*OpObject).IsValid())
            {
                continue;
            }
            FString Op;
            if ((*OpObject)->TryGetStringField(TEXT("op"), Op) || (*OpObject)->TryGetStringField(TEXT("kind"), Op))
            {
                AddCommandKind(Op);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetArrayField(TEXT("commands"), Commands) && Commands != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& CommandValue : *Commands)
        {
            const TSharedPtr<FJsonObject>* CommandObject = nullptr;
            if (!CommandValue.IsValid() || !CommandValue->TryGetObject(CommandObject) || CommandObject == nullptr || !(*CommandObject).IsValid())
            {
                continue;
            }
            FString Kind;
            if ((*CommandObject)->TryGetStringField(TEXT("kind"), Kind) || (*CommandObject)->TryGetStringField(TEXT("op"), Kind))
            {
                AddCommandKind(Kind);
            }
        }
    }
    AddCommandKind(ReadStringField(Arguments, TEXT("operation")));

    FString BatchId = ReadStringField(Arguments, TEXT("idempotencyKey"));
    if (BatchId.IsEmpty())
    {
        BatchId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    FString NormalizedAssetPath = AssetPath;
    if (!NormalizedAssetPath.IsEmpty())
    {
        NormalizedAssetPath = NormalizeAssetPath(NormalizedAssetPath);
    }

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetBoolField(TEXT("isShuttingDown"), bIsShuttingDown.Load());
    Runtime->SetBoolField(TEXT("hasEditor"), GEditor != nullptr);
    Runtime->SetBoolField(TEXT("isPlaySessionInProgress"), GEditor != nullptr && GEditor->IsPlaySessionInProgress());
    Runtime->SetBoolField(TEXT("isPlayingSessionInEditor"), GEditor != nullptr && GEditor->IsPlayingSessionInEditor());
    Runtime->SetBoolField(TEXT("isPlaySessionRequestQueued"), GEditor != nullptr && GEditor->IsPlaySessionRequestQueued());
    Runtime->SetBoolField(TEXT("shouldEndPlayMap"), GEditor != nullptr && GEditor->ShouldEndPlayMap());
    Runtime->SetBoolField(TEXT("hasPlayWorld"), GEditor != nullptr && GEditor->PlayWorld != nullptr);

    TSharedPtr<FJsonObject> AssetEditor = MakeShared<FJsonObject>();
    AssetEditor->SetStringField(TEXT("state"), TEXT("not_checked_during_unsafe_lifecycle"));
    AssetEditor->SetBoolField(TEXT("known"), false);

    bool bDryRun = false;
    bool bContinueOnError = false;
    if (Arguments.IsValid())
    {
        Arguments->TryGetBoolField(TEXT("dryRun"), bDryRun);
        Arguments->TryGetBoolField(TEXT("continueOnError"), bContinueOnError);
    }

    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("tool"), ToolName);
    Context->SetStringField(TEXT("batchId"), BatchId);
    Context->SetStringField(TEXT("assetPath"), NormalizedAssetPath);
    Context->SetStringField(TEXT("graphName"), GraphName);
    Context->SetStringField(TEXT("blockCode"), Code);
    Context->SetStringField(TEXT("blockReason"), Message);
    Context->SetBoolField(TEXT("dryRun"), bDryRun);
    Context->SetBoolField(TEXT("continueOnError"), bContinueOnError);
    Context->SetStringField(TEXT("expectedRevision"), ReadStringField(Arguments, TEXT("expectedRevision")));
    Context->SetNumberField(TEXT("commandCount"), CommandKinds.Num());
    Context->SetBoolField(TEXT("compileRequested"), bCompileRequested);
    Context->SetBoolField(TEXT("saveRequested"), bSaveRequested);
    Context->SetBoolField(TEXT("layoutRequested"), bLayoutRequested);
    Context->SetObjectField(TEXT("runtime"), Runtime);
    Context->SetObjectField(TEXT("assetEditor"), AssetEditor);

    TArray<TSharedPtr<FJsonValue>> CommandKindValues;
    for (const FString& Kind : CommandKinds)
    {
        CommandKindValues.Add(MakeShared<FJsonValueString>(Kind));
    }
    Context->SetArrayField(TEXT("commandKinds"), CommandKindValues);

    AppendDiagnosticEvent(TEXT("error"), TEXT("runtime"), ToolName, Message, Context);

    TArray<TSharedPtr<FJsonValue>> OpResults;
    for (int32 Index = 0; Index < CommandKinds.Num(); ++Index)
    {
        TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
        OpResult->SetNumberField(TEXT("index"), Index);
        OpResult->SetStringField(TEXT("op"), CommandKinds[Index]);
        OpResult->SetBoolField(TEXT("ok"), false);
        OpResult->SetBoolField(TEXT("skipped"), true);
        OpResult->SetBoolField(TEXT("changed"), false);
        OpResult->SetStringField(TEXT("errorCode"), Code);
        OpResult->SetStringField(TEXT("errorMessage"), Message);
        OpResults.Add(MakeShared<FJsonValueObject>(OpResult));
    }

    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("severity"), TEXT("error"));
    Diagnostic->SetStringField(TEXT("message"), Message);
    Diagnostic->SetStringField(TEXT("sourceKind"), TEXT("editor.lifecycle"));
    Diagnostic->SetObjectField(TEXT("context"), Context);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), true);
    Result->SetStringField(TEXT("code"), Code);
    Result->SetStringField(TEXT("message"), Message);
    Result->SetBoolField(TEXT("applied"), false);
    Result->SetBoolField(TEXT("partialApplied"), false);
    Result->SetStringField(TEXT("assetPath"), NormalizedAssetPath);
    Result->SetStringField(TEXT("graphName"), GraphName);
    Result->SetStringField(TEXT("batchId"), BatchId);
    Result->SetObjectField(TEXT("mutationContext"), Context);
    Result->SetArrayField(TEXT("opResults"), OpResults);
    Result->SetArrayField(TEXT("diagnostics"), {MakeShared<FJsonValueObject>(Diagnostic)});
    return Result;
}

void FLoomleBridgeModule::StartupModule()
{
    RegisterLoomleSlateStyle();
    if (!PreExitHandle.IsValid())
    {
        PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FLoomleBridgeModule::HandlePreExit);
    }

#if PLATFORM_WINDOWS
    const FString PipeName = GetRpcPipeNameForCurrentProject();
#endif
    PipeServer = MakeShared<FLoomlePipeServer, ESPMode::ThreadSafe>(
#if PLATFORM_WINDOWS
        PipeName,
#else
        LoomleBridgeConstants::PipeNamePrefix,
#endif
        [this](int32 ConnectionSerial, const FString& RequestLine)
        {
            return HandleRequest(ConnectionSerial, RequestLine);
        });

    if (!PipeServer->Start())
    {
        UE_LOG(LogLoomleBridge, Error, TEXT("Failed to start Loomle pipe server."));
        PipeServer.Reset();
        if (PreExitHandle.IsValid())
        {
            FCoreDelegates::OnPreExit.Remove(PreExitHandle);
            PreExitHandle.Reset();
        }
        bBridgeRunningSnapshot.Store(false);
        bPythonReadySnapshot.Store(false);
        bIsPIESnapshot.Store(false);
        UnregisterLoomleSlateStyle();
        return;
    }

    UpdateHealthSnapshot();
    InitializeDiagnosticStore();
    InitializeLogStore();
    if (GLog != nullptr && LogCaptureOutputDevice == nullptr)
    {
        LogCaptureOutputDevice = new FLoomleLogCaptureOutputDevice(
            [this](const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category)
            {
                HandleLogLine(Message, Verbosity, Category);
            });
        GLog->AddOutputDevice(LogCaptureOutputDevice);
    }
    if (GEditor != nullptr && !BlueprintCompiledHandle.IsValid())
    {
        BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FLoomleBridgeModule::HandleBlueprintCompiled);
    }
    HealthSnapshotTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FLoomleBridgeModule::TickHealthSnapshot),
        0.1f);
    if (!StatusBarStartupHandle.IsValid())
    {
        StatusBarStartupHandle = UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLoomleBridgeModule::RegisterStatusBarMenus));
    }
    RegisterStatusBarWidget();
    WriteRuntimeRegistration();

#if PLATFORM_WINDOWS
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on named pipe \\\\.\\pipe\\%s"), *PipeName);
#else
    UE_LOG(LogLoomleBridge, Display, TEXT("Loomle bridge started on unix socket %s"), *FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("loomle.sock")));
#endif

    bGraphMutateInProgress.Store(false);
}

void FLoomleBridgeModule::ShutdownModule()
{
    bIsShuttingDown.Store(true);

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

    if (BlueprintCompiledHandle.IsValid())
    {
        if (GEditor != nullptr)
        {
            GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
        }
        BlueprintCompiledHandle.Reset();
    }
    if (LogCaptureOutputDevice != nullptr)
    {
        if (GLog != nullptr)
        {
            GLog->RemoveOutputDevice(LogCaptureOutputDevice);
        }
        delete LogCaptureOutputDevice;
        LogCaptureOutputDevice = nullptr;
    }

    if (HealthSnapshotTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(HealthSnapshotTickerHandle);
        HealthSnapshotTickerHandle.Reset();
    }

}

#include "LoomleBridgeRpc.inl"
#include "LoomleBridgeBlueprint.inl"
#include "LoomleBridgeMaterial.inl"
#include "LoomleBridgePcg.inl"
#include "LoomleBridgeGraphShared.inl"

#include "LoomleWidgetAdapter.h"
#include "WidgetBlueprint.h"
#include "LoomleBridgeWidget.inl"

#include "LoomleBridgeRuntime.inl"

IMPLEMENT_MODULE(FLoomleBridgeModule, LoomleBridge)
