// Copyright 2026 Loomle contributors.

#include "SalReferenceInterface.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "BlueprintAssetHandler.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Sal/SalDiagnostics.h"
#include "Sal/SalModel.h"
#include "Sal/SalObjectBuilder.h"
#include "SalReferenceFacts.h"
#include "Templates/Atomic.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace Loomle::Sal
{
namespace ReferenceInterfacePrivate
{
constexpr int32 DefaultPageLimit = 50;
constexpr int32 MaxPageLimit = 200;
constexpr int32 MaxContainersPerCall = 8;
constexpr double MaxScanSecondsPerCall = 0.050;
constexpr double SessionTtlSeconds = 15.0 * 60.0;
constexpr int32 MaxSessions = 128;

struct FReferenceRecord
{
    EReferenceUseSiteKind Kind = EReferenceUseSiteKind::Node;
    FString Key;

    FString ContainerPath;
    FString ContainerName;
    FString ContainerType;

    FString BlueprintPath;
    FString BlueprintName;
    FString BlueprintId;

    FString GraphName;
    FString GraphId;

    FString NodeName;
    FString NodeId;
    FString NodeType;
    FString NodeTitle;
    TArray<FString> NodeHealth;
    FString StaleBlueprintStatus;

    FString WidgetName;
    FString WidgetId;
    FString WidgetType;
    FString WidgetDisplayLabel;

    FString VariableName;
    FString VariableId;
    FString VariableType;

    TArray<FString> MatchedPaths;
    bool bCompound = false;
};

struct FReferenceSession
{
    FString Id;
    FString RequestHash;
    FString IdentityKey;
    FString RootsHash;
    FString HandlerHash;
    FString SnapshotHash = TEXT("local");
    uint64 Generation = 0;
    int32 Revision = 0;
    double LastAccess = 0.0;

    bool bProject = false;
    bool bAwaitingRegistry = false;
    bool bScanComplete = false;

    TArray<FAssetData> Containers;
    int32 NextContainer = 0;
    TArray<FReferenceRecord> Buffered;
    int32 NextBuffered = 0;
    TSet<FString> SeenSites;
    int32 TotalEmitted = 0;
};

TMap<FString, TSharedPtr<FReferenceSession>> GReferenceSessions;
TAtomic<uint64> GReferenceGeneration{1};
FDelegateHandle GObjectModifiedHandle;
FDelegateHandle GAssetAddedHandle;
FDelegateHandle GAssetRemovedHandle;
FDelegateHandle GAssetRenamedHandle;
FDelegateHandle GAssetUpdatedOnDiskHandle;
FDelegateHandle GInMemoryAssetCreatedHandle;
FDelegateHandle GInMemoryAssetDeletedHandle;
FDelegateHandle GFilesLoadedHandle;
bool bReferenceStarted = false;

FString GuidText(const FGuid& Guid)
{
    return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
}

void AdvanceGeneration()
{
    ++GReferenceGeneration;
}

bool IsAuthoredObject(const UObject* Object)
{
    if (Object == nullptr || Object->HasAnyFlags(RF_Transient | RF_ClassDefaultObject))
    {
        return false;
    }
    if (Cast<UBlueprint>(Object) != nullptr || Object->GetTypedOuter<UBlueprint>() != nullptr)
    {
        return true;
    }
    if (Object->HasAnyFlags(RF_ArchetypeObject))
    {
        return false;
    }
    const UPackage* Package = Object->GetOutermost();
    return Object->IsAsset()
        && Package != nullptr
        && Package != GetTransientPackage();
}

void OnObjectModified(UObject* Object)
{
    if (IsAuthoredObject(Object))
    {
        AdvanceGeneration();
    }
}

void OnAssetChanged(const FAssetData&)
{
    AdvanceGeneration();
}

void OnAssetRenamed(const FAssetData&, const FString&)
{
    AdvanceGeneration();
}

void OnInMemoryAssetChanged(UObject*)
{
    AdvanceGeneration();
}

void OnFilesLoaded()
{
    AdvanceGeneration();
}

FString HashText(const FString& Text)
{
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(*Text, Text.Len() * sizeof(TCHAR), Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

void AppendToken(FString& Out, const TCHAR Prefix, const FString& Text)
{
    Out.AppendChar(Prefix);
    Out += LexToString(Text.Len());
    Out.AppendChar(TEXT(':'));
    Out += Text;
}

void AppendCanonicalJson(FString& Out, const TSharedPtr<FJsonValue>& Json)
{
    if (!Json.IsValid() || Json->IsNull())
    {
        Out += TEXT("z;");
        return;
    }
    FString String;
    bool bBool = false;
    double Number = 0.0;
    if (Json->TryGetString(String))
    {
        AppendToken(Out, TEXT('s'), String);
        return;
    }
    if (Json->TryGetBool(bBool))
    {
        Out += bBool ? TEXT("b1;") : TEXT("b0;");
        return;
    }
    if (Json->TryGetNumber(Number))
    {
        AppendToken(Out, TEXT('n'), FString::SanitizeFloat(Number));
        return;
    }
    const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
    if (Json->TryGetArray(Array) && Array != nullptr)
    {
        Out += TEXT("a[");
        for (const TSharedPtr<FJsonValue>& Item : *Array)
        {
            AppendCanonicalJson(Out, Item);
        }
        Out += TEXT("];");
        return;
    }
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (Json->TryGetObject(Object) && Object != nullptr && (*Object).IsValid())
    {
        TArray<FString> Keys;
        (*Object)->Values.GetKeys(Keys);
        Keys.Sort();
        Out += TEXT("o{");
        for (const FString& Key : Keys)
        {
            AppendToken(Out, TEXT('k'), Key);
            AppendCanonicalJson(Out, (*Object)->Values.FindRef(Key));
        }
        Out += TEXT("};");
        return;
    }
    Out += TEXT("u;");
}

FString RequestHash(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target,
    const FCanonicalReference& Identity,
    const bool bProject,
    const int32 EffectiveLimit)
{
    FString Canonical;
    AppendToken(Canonical, TEXT('k'), LexToString(static_cast<uint8>(Target.Kind)));
    AppendToken(Canonical, TEXT('a'), Target.AssetPath);
    AppendToken(Canonical, TEXT('i'), Target.Id);
    AppendToken(Canonical, TEXT('n'), Target.Name);
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueObject>(Query.TargetValue));
    AppendCanonicalJson(Canonical, MakeShared<FJsonValueObject>(Query.Operation));
    AppendToken(Canonical, TEXT('r'), Identity.StableKey());
    AppendToken(Canonical, TEXT('s'), bProject ? TEXT("project") : TEXT("local"));
    AppendToken(Canonical, TEXT('l'), LexToString(EffectiveLimit));
    return HashText(Canonical);
}

TArray<FString> ProjectRoots()
{
    TArray<FString> Roots = {TEXT("/Game")};
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        const EPluginType Type = Plugin->GetType();
        if (!Plugin->IsEnabled()
            || !Plugin->IsMounted()
            || !Plugin->CanContainContent()
            || !(Type == EPluginType::Project || Type == EPluginType::Mod || Type == EPluginType::External))
        {
            continue;
        }
        FString Root = Plugin->GetMountedAssetPath();
        while (Root.EndsWith(TEXT("/")))
        {
            Root.LeftChopInline(1, EAllowShrinking::No);
        }
        if (!Root.IsEmpty())
        {
            Roots.AddUnique(Root);
        }
    }
    Roots.Sort();
    return Roots;
}

TArray<FTopLevelAssetPath> HandlerClassPaths()
{
    TArray<FTopLevelAssetPath> Paths;
    for (const FTopLevelAssetPath& Path : FBlueprintAssetHandler::Get().GetRegisteredClassNames())
    {
        Paths.Add(Path);
    }
    Paths.Sort([](const FTopLevelAssetPath& Left, const FTopLevelAssetPath& Right)
    {
        return Left.ToString() < Right.ToString();
    });
    return Paths;
}

FString StringArrayHash(const TArray<FString>& Values)
{
    FString Canonical;
    for (const FString& Value : Values)
    {
        AppendToken(Canonical, TEXT('v'), Value);
    }
    return HashText(Canonical);
}

FString HandlerHash(const TArray<FTopLevelAssetPath>& Paths)
{
    TArray<FString> Values;
    Values.Reserve(Paths.Num());
    for (const FTopLevelAssetPath& Path : Paths)
    {
        Values.Add(Path.ToString());
    }
    return StringArrayHash(Values);
}

bool IsInProjectRoots(const FString& PackageName, const TArray<FString>& Roots)
{
    for (const FString& Root : Roots)
    {
        if (PackageName == Root || PackageName.StartsWith(Root + TEXT("/")))
        {
            return true;
        }
    }
    return false;
}

bool HasBlueprintSentinel(const FAssetData& Data)
{
    return Data.TagsAndValues.Contains(FBlueprintTags::BlueprintPathWithinPackage)
        || Data.TagsAndValues.Contains(FBlueprintTags::FindInBlueprintsData)
        || Data.TagsAndValues.Contains(FBlueprintTags::UnversionedFindInBlueprintsData);
}

TSharedPtr<FJsonObject> ReferenceDiagnostic(
    const FString& Code,
    const FString& Message,
    const FString& Ref = FString(),
    const TArray<FString>& Matches = {})
{
    FSalDiagnosticBuilder Diagnostic = FSalDiagnostics::Error(Code, Message)
        .Operation(TEXT("references"));
    if (!Ref.IsEmpty())
    {
        Diagnostic.Ref(Ref);
    }
    if (!Matches.IsEmpty())
    {
        Diagnostic.Matches(Matches);
    }
    return FSalDiagnostics::Result(Diagnostic.Build());
}

FString IssueMessage(const TArray<FReferenceCoverageIssue>& Issues, const FString& Fallback)
{
    TArray<FString> Messages;
    for (const FReferenceCoverageIssue& Issue : Issues)
    {
        FString Message = Issue.Message;
        if (!Issue.ObjectRef.IsEmpty())
        {
            Message = Issue.ObjectRef + (Issue.FieldPath.IsEmpty() ? FString() : TEXT(".") + Issue.FieldPath)
                + TEXT(": ") + Message;
        }
        if (!Message.IsEmpty())
        {
            Messages.Add(Message);
        }
        if (Messages.Num() == 4)
        {
            break;
        }
    }
    return Messages.IsEmpty() ? Fallback : FString::Join(Messages, TEXT("; "));
}

TSharedPtr<FJsonObject> SubjectError(const FReferenceSubjectResolution& Resolution)
{
    FString Code;
    switch (Resolution.Status)
    {
    case EReferenceResolutionStatus::Ambiguous:
        Code = TEXT("resolution.reference_ambiguous");
        break;
    case EReferenceResolutionStatus::Broken:
        Code = TEXT("resolution.reference_broken");
        break;
    case EReferenceResolutionStatus::Unsupported:
        Code = TEXT("capability.reference_unavailable");
        break;
    case EReferenceResolutionStatus::NotFound:
    default:
        Code = TEXT("resolution.reference_not_found");
        break;
    }
    const FString Message = !Resolution.Message.IsEmpty()
        ? Resolution.Message
        : IssueMessage(Resolution.Issues, TEXT("The references subject could not be resolved factually."));
    return ReferenceDiagnostic(Code, Message, Resolution.Subject.QueryRef, Resolution.Matches);
}

FString PinTypeText(const FEdGraphPinType& Type)
{
    FString Text;
    FEdGraphPinType::StaticStruct()->ExportText(Text, &Type, nullptr, nullptr, PPF_None, nullptr);
    return Text;
}

FString NodeSeverityText(const int32 ErrorType)
{
    if (ErrorType == 0)
    {
        return TEXT("CriticalError");
    }
    switch (ErrorType)
    {
    case EMessageSeverity::Error: return TEXT("Error");
    case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
    case EMessageSeverity::Warning: return TEXT("Warning");
    case EMessageSeverity::Info: return TEXT("Info");
    default: return FString::Printf(TEXT("ErrorType=%d"), ErrorType);
    }
}

FString HealthComment(const FString& Header, const FString& Message)
{
    return Message.Contains(TEXT("\n")) ? Header + TEXT("\n") + Message : Header + TEXT(": ") + Message;
}

UObject* FindTopLevelContainer(UBlueprint* Blueprint, IAssetRegistry* Registry)
{
    UObject* TopLevel = nullptr;
    for (UObject* Current = Blueprint; Current != nullptr && !Current->IsA<UPackage>(); Current = Current->GetOuter())
    {
        if (Current->IsAsset())
        {
            TopLevel = Current;
        }
    }
    if (TopLevel != nullptr || Blueprint == nullptr || Registry == nullptr)
    {
        return TopLevel;
    }

    TArray<FAssetData> PackageAssets;
    Registry->GetAssetsByPackageName(Blueprint->GetOutermost()->GetFName(), PackageAssets, false);
    for (const FAssetData& Data : PackageAssets)
    {
        if (UObject* Candidate = Data.FastGetAsset(false))
        {
            if (Blueprint == Candidate || Blueprint->IsIn(Candidate))
            {
                return Candidate;
            }
        }
    }
    return nullptr;
}

UBlueprint* SiteBlueprint(const FReferenceUseSite& Site)
{
    if (Site.Blueprint != nullptr)
    {
        return Site.Blueprint;
    }
    if (Site.Graph != nullptr)
    {
        return FBlueprintEditorUtils::FindBlueprintForGraph(Site.Graph);
    }
    if (Site.Node != nullptr && Site.Node->GetGraph() != nullptr)
    {
        return FBlueprintEditorUtils::FindBlueprintForGraph(Site.Node->GetGraph());
    }
    if (Site.Widget != nullptr)
    {
        return Site.Widget->GetTypedOuter<UWidgetBlueprint>();
    }
    return nullptr;
}

void AddNodeHealth(FReferenceRecord& Record, const UEdGraphNode* Node, const UBlueprint* Blueprint)
{
    if (Node == nullptr)
    {
        return;
    }
    if (Node->bHasCompilerMessage && !Node->ErrorMsg.IsEmpty())
    {
        Record.NodeHealth.Add(
            FString::Printf(TEXT("UE node diagnostic: %s\n%s"), *NodeSeverityText(Node->ErrorType), *Node->ErrorMsg));
        if (Blueprint != nullptr && Blueprint->Status == BS_Dirty)
        {
            Record.StaleBlueprintStatus = TEXT("BS_Dirty");
        }
        else if (Blueprint != nullptr && Blueprint->Status == BS_Unknown)
        {
            Record.StaleBlueprintStatus = TEXT("BS_Unknown");
        }
    }
#if WITH_EDITORONLY_DATA
    if (!Node->NodeUpgradeMessage.IsEmpty())
    {
        Record.NodeHealth.Add(HealthComment(TEXT("UE node upgrade note"), Node->NodeUpgradeMessage.ToString()));
    }
#endif
    if (Node->ShowVisualWarning())
    {
        const FString Message = Node->GetVisualWarningTooltipText().ToString();
        Record.NodeHealth.Add(Message.IsEmpty()
            ? TEXT("UE node visual warning")
            : HealthComment(TEXT("UE node visual warning"), Message));
    }
}

bool MakeRecord(
    const FReferenceUseSite& Site,
    UObject* KnownContainer,
    IAssetRegistry* Registry,
    FReferenceRecord& OutRecord,
    FString& OutError)
{
    UBlueprint* Blueprint = SiteBlueprint(Site);
    if (Blueprint == nullptr || !Blueprint->GetBlueprintGuid().IsValid())
    {
        OutError = TEXT("A reference use-site has no valid owning Blueprint identity.");
        return false;
    }
    UObject* Container = KnownContainer;
    if (Container == nullptr || !(Blueprint == Container || Blueprint->IsIn(Container)))
    {
        Container = FindTopLevelContainer(Blueprint, Registry);
    }
    if (Container == nullptr || !Container->IsAsset())
    {
        OutError = FString::Printf(
            TEXT("Could not resolve the top-level asset container for Blueprint %s."),
            *Blueprint->GetPathName());
        return false;
    }

    OutRecord.Kind = Site.Kind;
    OutRecord.ContainerPath = Container->GetPathName();
    OutRecord.ContainerName = Container->GetName();
    OutRecord.ContainerType = Container->GetClass()->GetPathName();
    OutRecord.BlueprintPath = Blueprint->GetPathName();
    OutRecord.BlueprintName = Blueprint->GetName();
    OutRecord.BlueprintId = GuidText(Blueprint->GetBlueprintGuid());
    OutRecord.MatchedPaths = Site.MatchedPaths;
    OutRecord.bCompound = Site.bCompound;

    UEdGraph* Graph = Site.Graph;
    if (Graph == nullptr && Site.Node != nullptr)
    {
        Graph = Site.Node->GetGraph();
    }

    if (Site.Kind == EReferenceUseSiteKind::Node)
    {
        if (Site.Node == nullptr || Graph == nullptr || !Site.Node->NodeGuid.IsValid() || !Graph->GraphGuid.IsValid())
        {
            OutError = TEXT("A matching Node use-site has no stable Graph or Node identity.");
            return false;
        }
        OutRecord.GraphName = Graph->GetName();
        OutRecord.GraphId = GuidText(Graph->GraphGuid);
        OutRecord.NodeName = Site.Node->GetName();
        OutRecord.NodeId = GuidText(Site.Node->NodeGuid);
        OutRecord.NodeType = Site.Node->GetClass()->GetPathName();
        OutRecord.NodeTitle = Site.Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        AddNodeHealth(OutRecord, Site.Node, Blueprint);
        OutRecord.Key = OutRecord.BlueprintPath + TEXT("|node|") + OutRecord.GraphId + TEXT("|") + OutRecord.NodeId;
        return true;
    }
    if (Site.Kind == EReferenceUseSiteKind::Graph)
    {
        if (Graph == nullptr || !Graph->GraphGuid.IsValid())
        {
            OutError = TEXT("A matching Graph use-site has no stable identity.");
            return false;
        }
        OutRecord.GraphName = Graph->GetName();
        OutRecord.GraphId = GuidText(Graph->GraphGuid);
        OutRecord.Key = OutRecord.BlueprintPath + TEXT("|graph|") + OutRecord.GraphId;
        return true;
    }
    if (Site.Kind == EReferenceUseSiteKind::Widget)
    {
        UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
        const FGuid* WidgetGuid = WidgetBlueprint != nullptr && Site.Widget != nullptr
            ? WidgetBlueprint->WidgetVariableNameToGuidMap.Find(Site.Widget->GetFName())
            : nullptr;
        if (Site.Widget == nullptr || WidgetGuid == nullptr || !WidgetGuid->IsValid())
        {
            OutError = TEXT("A matching Widget use-site has no persistent Widget GUID.");
            return false;
        }
        OutRecord.WidgetName = Site.Widget->GetName();
        OutRecord.WidgetId = GuidText(*WidgetGuid);
        OutRecord.WidgetType = Site.Widget->GetClass()->GetPathName();
        OutRecord.WidgetDisplayLabel = Site.Widget->GetDisplayLabel();
        OutRecord.Key = OutRecord.BlueprintPath + TEXT("|widget|") + OutRecord.WidgetId;
        return true;
    }
    if (Site.Kind == EReferenceUseSiteKind::Variable)
    {
        if (Site.Variable == nullptr || !Site.Variable->VarGuid.IsValid())
        {
            OutError = TEXT("A matching Variable use-site has no stable Variable GUID.");
            return false;
        }
        OutRecord.VariableName = Site.Variable->VarName.ToString();
        OutRecord.VariableId = GuidText(Site.Variable->VarGuid);
        OutRecord.VariableType = PinTypeText(Site.Variable->VarType);
        OutRecord.Key = OutRecord.BlueprintPath + TEXT("|variable|") + OutRecord.VariableId;
        return true;
    }
    if (Site.Kind == EReferenceUseSiteKind::Blueprint)
    {
        OutRecord.Key = OutRecord.BlueprintPath + TEXT("|blueprint|") + OutRecord.BlueprintId;
        return true;
    }

    OutError = TEXT("The reference provider returned an unsupported authored use-site kind.");
    return false;
}

bool AppendScanResult(
    FReferenceSession& Session,
    const FReferenceScanResult& Scan,
    UObject* Container,
    IAssetRegistry* Registry,
    FString& OutError)
{
    if (!Scan.IsComplete())
    {
        OutError = IssueMessage(
            Scan.Issues,
            TEXT("The native reference provider could not verify every potentially matching fact."));
        return false;
    }
    for (const FReferenceUseSite& Site : Scan.Sites)
    {
        FReferenceRecord Record;
        if (!MakeRecord(Site, Container, Registry, Record, OutError))
        {
            return false;
        }
        if (Session.SeenSites.Contains(Record.Key))
        {
            OutError = FString::Printf(
                TEXT("Stable authored identity collision while scanning references: %s."),
                *Record.Key);
            return false;
        }
        Session.SeenSites.Add(Record.Key);
        Session.Buffered.Add(MoveTemp(Record));
    }
    return true;
}

enum class ESnapshotResult : uint8
{
    Ready,
    Pending,
    Error
};

ESnapshotResult BuildProjectSnapshot(
    FReferenceSession& Session,
    IAssetRegistry& Registry,
    FString& OutError)
{
    if (Registry.IsLoadingAssets())
    {
        return ESnapshotResult::Pending;
    }

    const TArray<FString> Roots = ProjectRoots();
    const TArray<FTopLevelAssetPath> HandlerPaths = HandlerClassPaths();
    const FString CurrentRootsHash = StringArrayHash(Roots);
    const FString CurrentHandlerHash = HandlerHash(HandlerPaths);
    if (CurrentRootsHash != Session.RootsHash || CurrentHandlerHash != Session.HandlerHash)
    {
        OutError = TEXT("Project roots or Blueprint asset handlers changed before the reference snapshot was frozen.");
        return ESnapshotResult::Error;
    }

    TSet<FTopLevelAssetPath> EligibleClasses;
    Registry.GetDerivedClassNames(HandlerPaths, {}, EligibleClasses);
    for (const FTopLevelAssetPath& Path : HandlerPaths)
    {
        EligibleClasses.Add(Path);
    }

    TArray<FAssetData> Assets;
    if (!Registry.GetAllAssets(Assets, false))
    {
        OutError = TEXT("Asset Registry could not produce the authoritative live project snapshot.");
        return ESnapshotResult::Error;
    }

    TArray<FAssetData> ProjectAssets;
    ProjectAssets.Reserve(Assets.Num());
    for (const FAssetData& Data : Assets)
    {
        if (Data.IsValid() && IsInProjectRoots(Data.PackageName.ToString(), Roots))
        {
            ProjectAssets.Add(Data);
        }
    }
    Assets = MoveTemp(ProjectAssets);
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        const int32 PackageOrder = Left.PackageName.Compare(Right.PackageName);
        if (PackageOrder != 0)
        {
            return PackageOrder < 0;
        }
        const int32 NameOrder = Left.AssetName.Compare(Right.AssetName);
        if (NameOrder != 0)
        {
            return NameOrder < 0;
        }
        return Left.AssetClassPath.ToString() < Right.AssetClassPath.ToString();
    });

    TArray<FAssetData> Containers;
    for (const FAssetData& Data : Assets)
    {
        const bool bEligible = EligibleClasses.Contains(Data.AssetClassPath);
        if (!bEligible)
        {
            if (HasBlueprintSentinel(Data))
            {
                OutError = FString::Printf(
                    TEXT("Project asset %s advertises embedded Blueprint state but no frozen handler/provider can verify it."),
                    *Data.GetSoftObjectPath().ToString());
                return ESnapshotResult::Error;
            }
            continue;
        }

        UClass* AssetClass = Data.GetClass(EResolveClass::Yes);
        const IBlueprintAssetHandler* Handler = AssetClass != nullptr
            ? FBlueprintAssetHandler::Get().FindHandler(AssetClass)
            : nullptr;
        if (Handler == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The frozen Blueprint handler could not be resolved for project asset %s."),
                *Data.GetSoftObjectPath().ToString());
            return ESnapshotResult::Error;
        }

        const bool bHandlerClaimsBlueprint = Handler->AssetContainsBlueprint(Data);
        const bool bSentinelClaimsBlueprint = HasBlueprintSentinel(Data);
        if (UObject* LoadedAsset = Data.FastGetAsset(false))
        {
            if (Handler->RetrieveBlueprint(LoadedAsset) != nullptr)
            {
                Containers.Add(Data);
            }
            else if (bHandlerClaimsBlueprint || bSentinelClaimsBlueprint)
            {
                OutError = FString::Printf(
                    TEXT("Project asset %s advertises Blueprint state but its frozen handler cannot retrieve it."),
                    *Data.GetSoftObjectPath().ToString());
                return ESnapshotResult::Error;
            }
        }
        else if (bHandlerClaimsBlueprint)
        {
            Containers.Add(Data);
        }
        else if (bSentinelClaimsBlueprint)
        {
            OutError = FString::Printf(
                TEXT("Project asset %s advertises Blueprint state that its frozen handler cannot verify."),
                *Data.GetSoftObjectPath().ToString());
            return ESnapshotResult::Error;
        }
    }

    FString SnapshotCanonical;
    AppendToken(SnapshotCanonical, TEXT('r'), Session.RootsHash);
    AppendToken(SnapshotCanonical, TEXT('h'), Session.HandlerHash);
    for (const FAssetData& Data : Containers)
    {
        AppendToken(SnapshotCanonical, TEXT('p'), Data.GetSoftObjectPath().ToString());
        AppendToken(SnapshotCanonical, TEXT('c'), Data.AssetClassPath.ToString());
    }
    if (CurrentRootsHash != StringArrayHash(ProjectRoots())
        || CurrentHandlerHash != HandlerHash(HandlerClassPaths()))
    {
        OutError = TEXT("Project roots or Blueprint asset handlers changed while the reference snapshot was being built.");
        return ESnapshotResult::Error;
    }
    Session.Containers = MoveTemp(Containers);
    Session.SnapshotHash = HashText(SnapshotCanonical);
    Session.NextContainer = 0;
    Session.Generation = GReferenceGeneration.Load();
    Session.bAwaitingRegistry = false;
    Session.bScanComplete = Session.Containers.IsEmpty();
    return ESnapshotResult::Ready;
}

enum class EContainerProcessResult : uint8
{
    Success,
    ContainerFailed,
    ScanIncomplete
};

EContainerProcessResult ProcessContainer(
    FReferenceSession& Session,
    const FAssetData& Data,
    IAssetRegistry& Registry,
    const FCanonicalReference& Identity,
    FString& OutError)
{
    UObject* Asset = Data.FastGetAsset(false);
    if (Asset == nullptr)
    {
        Asset = Data.GetAsset();
    }
    if (Asset == nullptr)
    {
        OutError = FString::Printf(TEXT("Could not load reference container %s."), *Data.GetSoftObjectPath().ToString());
        return EContainerProcessResult::ContainerFailed;
    }
    const IBlueprintAssetHandler* Handler = FBlueprintAssetHandler::Get().FindHandler(Asset->GetClass());
    if (Handler == nullptr)
    {
        OutError = FString::Printf(TEXT("Reference container %s no longer has its frozen Blueprint handler."), *Asset->GetPathName());
        return EContainerProcessResult::ContainerFailed;
    }
    UBlueprint* Blueprint = Handler->RetrieveBlueprint(Asset);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(
            TEXT("Reference container %s promised Blueprint state but could not retrieve it."),
            *Asset->GetPathName());
        return EContainerProcessResult::ContainerFailed;
    }
    if (!AppendScanResult(
            Session,
            FSalReferenceFacts::ScanBlueprint(Blueprint, nullptr, Identity),
            Asset,
            &Registry,
            OutError))
    {
        return EContainerProcessResult::ScanIncomplete;
    }
    return EContainerProcessResult::Success;
}

bool RemoveOldestSession()
{
    FString OldestId;
    double OldestTime = TNumericLimits<double>::Max();
    for (const TPair<FString, TSharedPtr<FReferenceSession>>& Pair : GReferenceSessions)
    {
        if (Pair.Value.IsValid() && Pair.Value->LastAccess < OldestTime)
        {
            OldestTime = Pair.Value->LastAccess;
            OldestId = Pair.Key;
        }
    }
    return !OldestId.IsEmpty() && GReferenceSessions.Remove(OldestId) > 0;
}

void CleanupSessions()
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = GReferenceSessions.CreateIterator(); It; ++It)
    {
        if (!It.Value().IsValid() || Now - It.Value()->LastAccess > SessionTtlSeconds)
        {
            It.RemoveCurrent();
        }
    }
    while (GReferenceSessions.Num() > MaxSessions)
    {
        if (!RemoveOldestSession())
        {
            break;
        }
    }
}

void ReserveSessionSlot()
{
    while (GReferenceSessions.Num() >= MaxSessions)
    {
        if (!RemoveOldestSession())
        {
            break;
        }
    }
}

FString EncodeCursor(const FReferenceSession& Session)
{
    return FString::Printf(
        TEXT("reference1:%s:%s:%d:%llu:%s"),
        *Session.Id,
        *Session.RequestHash,
        Session.Revision,
        static_cast<unsigned long long>(Session.Generation),
        *Session.SnapshotHash);
}

bool DecodeCursor(
    const FString& Cursor,
    FString& OutSessionId,
    FString& OutRequestHash,
    int32& OutRevision,
    uint64& OutGeneration,
    FString& OutSnapshotHash)
{
    TArray<FString> Parts;
    Cursor.ParseIntoArray(Parts, TEXT(":"), false);
    return Parts.Num() == 6
        && Parts[0] == TEXT("reference1")
        && !Parts[1].IsEmpty()
        && !Parts[2].IsEmpty()
        && LexTryParseString(OutRevision, *Parts[3])
        && OutRevision >= 0
        && LexTryParseString(OutGeneration, *Parts[4])
        && !Parts[5].IsEmpty()
        && (OutSessionId = Parts[1], true)
        && (OutRequestHash = Parts[2], true)
        && (OutSnapshotHash = Parts[5], true);
}

TSharedPtr<FJsonValue> Call(const FString& Callee, const TSharedPtr<FJsonObject>& Args)
{
    return Value::Call(Callee, Args);
}

class FReferenceObjectEncoder
{
public:
    void Add(const FReferenceRecord& Record)
    {
        const FString BlueprintAlias = EnsureBlueprint(Record);
        switch (Record.Kind)
        {
        case EReferenceUseSiteKind::Node:
            AddNode(Record, EnsureGraph(Record, BlueprintAlias));
            break;
        case EReferenceUseSiteKind::Graph:
            EnsureGraph(Record, BlueprintAlias);
            AddMatchedComments(Record);
            break;
        case EReferenceUseSiteKind::Variable:
            AddVariable(Record, BlueprintAlias);
            break;
        case EReferenceUseSiteKind::Widget:
            AddWidget(Record, BlueprintAlias);
            break;
        case EReferenceUseSiteKind::Blueprint:
            AddMatchedComments(Record);
            break;
        }
    }

    TSharedPtr<FJsonObject> BuildResult(const TArray<TSharedPtr<FJsonObject>>& Diagnostics = {}) const
    {
        return Builder.BuildResult(Diagnostics);
    }

    void AddComment(const FString& Comment)
    {
        Builder.AddComment(Comment);
    }

private:
    FString EnsureAsset(const FReferenceRecord& Record)
    {
        if (const FString* Existing = AssetAliases.Find(Record.ContainerPath))
        {
            return *Existing;
        }
        const FString Alias = Builder.UniqueAlias(Record.ContainerName + TEXT("Asset"));
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("path"), Record.ContainerPath);
        if (!Record.ContainerType.IsEmpty())
        {
            Args->SetStringField(TEXT("type"), Record.ContainerType);
        }
        Builder.AddLocalBinding(Alias, Call(TEXT("asset"), Args));
        AssetAliases.Add(Record.ContainerPath, Alias);
        return Alias;
    }

    FString EnsureBlueprint(const FReferenceRecord& Record)
    {
        const FString Key = Record.ContainerPath + TEXT("|") + Record.BlueprintPath;
        if (const FString* Existing = BlueprintAliases.Find(Key))
        {
            return *Existing;
        }
        const FString AssetAlias = EnsureAsset(Record);
        const FString Alias = Builder.UniqueAlias(Record.BlueprintName);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetField(TEXT("asset"), Value::Local(AssetAlias));
        Args->SetStringField(TEXT("id"), Record.BlueprintId);
        Builder.AddLocalBinding(Alias, Call(TEXT("blueprint"), Args));
        BlueprintAliases.Add(Key, Alias);
        return Alias;
    }

    FString EnsureGraph(const FReferenceRecord& Record, const FString& BlueprintAlias)
    {
        const FString Key = Record.ContainerPath + TEXT("|") + Record.BlueprintPath + TEXT("|") + Record.GraphId;
        if (const FString* Existing = GraphAliases.Find(Key))
        {
            return *Existing;
        }
        const FString Alias = Builder.UniqueAlias(Record.GraphName);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetField(TEXT("asset"), Value::Local(BlueprintAlias));
        Args->SetStringField(TEXT("id"), Record.GraphId);
        if (!Record.GraphName.IsEmpty())
        {
            Args->SetStringField(TEXT("name"), Record.GraphName);
        }
        Builder.AddLocalBinding(Alias, Call(TEXT("graph"), Args));
        GraphAliases.Add(Key, Alias);
        return Alias;
    }

    void AddNode(const FReferenceRecord& Record, const FString& GraphAlias)
    {
        const FString Preferred = Record.NodeTitle.IsEmpty() ? Record.NodeName : Record.NodeTitle;
        const FString Alias = Builder.UniqueAlias(Preferred);
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetField(TEXT("graph"), Value::Local(GraphAlias));
        Args->SetStringField(TEXT("id"), Record.NodeId);
        Args->SetStringField(TEXT("type"), Record.NodeType);
        Builder.AddLocalBinding(Alias, Call(TEXT("node"), Args));
        if (!Record.NodeTitle.IsEmpty())
        {
            Builder.AddComment(Record.NodeTitle);
        }
        for (const FString& Health : Record.NodeHealth)
        {
            Builder.AddComment(Health);
        }
        const FString BlueprintKey = Record.ContainerPath + TEXT("|") + Record.BlueprintPath;
        if (!Record.StaleBlueprintStatus.IsEmpty() && !StaleWarnings.Contains(BlueprintKey))
        {
            Builder.AddComment(FString::Printf(
                TEXT("UE compiler annotations may be stale: owner Blueprint status is %s"),
                *Record.StaleBlueprintStatus));
            StaleWarnings.Add(BlueprintKey);
        }
        AddMatchedComments(Record);
    }

    void AddVariable(const FReferenceRecord& Record, const FString& BlueprintAlias)
    {
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("id"), Record.VariableId);
        if (!Record.VariableType.IsEmpty())
        {
            Args->SetStringField(TEXT("type"), Record.VariableType);
        }
        if (FSalObjectBuilder::IsIdentifier(Record.VariableName))
        {
            Builder.AddMemberBinding(BlueprintAlias, {Record.VariableName}, Call(TEXT("variable"), Args));
        }
        else
        {
            Builder.AddLocalBinding(Builder.UniqueAlias(Record.VariableName), Call(TEXT("variable"), Args));
            Builder.AddComment(FString::Printf(
                TEXT("owner: %s\nmember path: unavailable in SAL identifier syntax\nnative name: %s"),
                *BlueprintAlias,
                *Record.VariableName));
        }
        AddMatchedComments(Record);
    }

    void AddWidget(const FReferenceRecord& Record, const FString& BlueprintAlias)
    {
        TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
        Args->SetStringField(TEXT("id"), Record.WidgetId);
        Args->SetStringField(TEXT("type"), Record.WidgetType);
        if (!Record.WidgetDisplayLabel.IsEmpty() && Record.WidgetDisplayLabel != Record.WidgetName)
        {
            Args->SetStringField(TEXT("DisplayLabel"), Record.WidgetDisplayLabel);
        }
        Builder.AddLocalBinding(Builder.UniqueAlias(Record.WidgetName), Call(TEXT("widget"), Args));
        Builder.AddComment(TEXT("owner: ") + BlueprintAlias);
        AddMatchedComments(Record);
    }

    void AddMatchedComments(const FReferenceRecord& Record)
    {
        if (!Record.bCompound && Record.MatchedPaths.Num() <= 1)
        {
            return;
        }
        for (const FString& Path : Record.MatchedPaths)
        {
            if (!Path.IsEmpty())
            {
                Builder.AddComment(TEXT("matched through ") + Path);
            }
        }
    }

    FSalObjectBuilder Builder;
    TMap<FString, FString> AssetAliases;
    TMap<FString, FString> BlueprintAliases;
    TMap<FString, FString> GraphAliases;
    TSet<FString> StaleWarnings;
};

TSharedPtr<FJsonObject> BuildPageResult(
    FReferenceSession& Session,
    const TArray<FReferenceRecord>& Records,
    const bool bHasNext,
    const bool bPending)
{
    FReferenceObjectEncoder Encoder;
    for (const FReferenceRecord& Record : Records)
    {
        Encoder.Add(Record);
    }

    TArray<TSharedPtr<FJsonObject>> Diagnostics;
    if (bPending && Records.IsEmpty())
    {
        Diagnostics.Add(
            FSalDiagnostics::Info(
                TEXT("validation.reference_scan_pending"),
                Session.bAwaitingRegistry
                    ? TEXT("Asset Registry discovery is still in progress; pass page.next back unchanged.")
                    : TEXT("The incremental project reference scan has more containers; pass page.next back unchanged."))
                .Operation(TEXT("references"))
                .Build());
    }
    if (!bHasNext && Records.IsEmpty())
    {
        Encoder.AddComment(Session.TotalEmitted == 0 ? TEXT("no matches") : TEXT("reference scan complete"));
    }

    TSharedPtr<FJsonObject> Result = Encoder.BuildResult(Diagnostics);
    if (bHasNext)
    {
        ++Session.Revision;
        TSharedPtr<FJsonObject> Page = MakeShared<FJsonObject>();
        Page->SetStringField(TEXT("next"), EncodeCursor(Session));
        Result->SetObjectField(TEXT("page"), Page);
    }
    return Result;
}

bool CurrentProjectEnvironmentMatches(const FReferenceSession& Session)
{
    return Session.RootsHash == StringArrayHash(ProjectRoots())
        && Session.HandlerHash == HandlerHash(HandlerClassPaths());
}
}

void FSalReferenceInterface::Startup()
{
    using namespace ReferenceInterfacePrivate;

    if (bReferenceStarted)
    {
        return;
    }
    bReferenceStarted = true;
    GReferenceSessions.Reset();
    GReferenceGeneration.Store(1);
    GObjectModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddStatic(&OnObjectModified);

    FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = Module.Get();
    GAssetAddedHandle = Registry.OnAssetAdded().AddStatic(&OnAssetChanged);
    GAssetRemovedHandle = Registry.OnAssetRemoved().AddStatic(&OnAssetChanged);
    GAssetRenamedHandle = Registry.OnAssetRenamed().AddStatic(&OnAssetRenamed);
    GAssetUpdatedOnDiskHandle = Registry.OnAssetUpdatedOnDisk().AddStatic(&OnAssetChanged);
    GInMemoryAssetCreatedHandle = Registry.OnInMemoryAssetCreated().AddStatic(&OnInMemoryAssetChanged);
    GInMemoryAssetDeletedHandle = Registry.OnInMemoryAssetDeleted().AddStatic(&OnInMemoryAssetChanged);
    GFilesLoadedHandle = Registry.OnFilesLoaded().AddStatic(&OnFilesLoaded);
}

void FSalReferenceInterface::Shutdown()
{
    using namespace ReferenceInterfacePrivate;

    if (!bReferenceStarted)
    {
        return;
    }
    bReferenceStarted = false;
    GReferenceSessions.Reset();
    FCoreUObjectDelegates::OnObjectModified.Remove(GObjectModifiedHandle);
    GObjectModifiedHandle.Reset();

    if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
    {
        IAssetRegistry& Registry = Module->Get();
        Registry.OnAssetAdded().Remove(GAssetAddedHandle);
        Registry.OnAssetRemoved().Remove(GAssetRemovedHandle);
        Registry.OnAssetRenamed().Remove(GAssetRenamedHandle);
        Registry.OnAssetUpdatedOnDisk().Remove(GAssetUpdatedOnDiskHandle);
        Registry.OnInMemoryAssetCreated().Remove(GInMemoryAssetCreatedHandle);
        Registry.OnInMemoryAssetDeleted().Remove(GInMemoryAssetDeletedHandle);
        Registry.OnFilesLoaded().Remove(GFilesLoadedHandle);
    }
    GAssetAddedHandle.Reset();
    GAssetRemovedHandle.Reset();
    GAssetRenamedHandle.Reset();
    GAssetUpdatedOnDiskHandle.Reset();
    GInMemoryAssetCreatedHandle.Reset();
    GInMemoryAssetDeletedHandle.Reset();
    GFilesLoadedHandle.Reset();
}

TSharedPtr<FJsonObject> FSalReferenceInterface::Query(
    const FSalQuery& Query,
    const FSalResolvedTarget& Target)
{
    using namespace ReferenceInterfacePrivate;

    if (!IsInGameThread())
    {
        return ReferenceDiagnostic(
            TEXT("capability.reference_unavailable"),
            TEXT("Reference queries require the Unreal game thread."));
    }
    if (Query.Where.IsValid() || !Query.OrderBy.IsEmpty() || !Query.With.IsEmpty())
    {
        return FSalDiagnostics::Result(
            FSalDiagnostics::Error(
                TEXT("capability.clause_unavailable"),
                TEXT("references accepts only cursor page clauses; where, order by, and with are unavailable."))
                .Operation(TEXT("references"))
                .Supported({TEXT("page")})
                .Build());
    }
    if ((Target.Kind != ESalTargetKind::Blueprint && Target.Kind != ESalTargetKind::Graph)
        || Target.Blueprint == nullptr)
    {
        return ReferenceDiagnostic(
            TEXT("capability.reference_unavailable"),
            TEXT("The bound target does not expose a Blueprint or Graph reference scope."));
    }

    const TSharedPtr<FJsonObject>* OperationTarget = nullptr;
    if (!Query.Operation.IsValid()
        || !Query.Operation->TryGetObjectField(TEXT("target"), OperationTarget)
        || OperationTarget == nullptr
        || !(*OperationTarget).IsValid())
    {
        return ReferenceDiagnostic(
            TEXT("resolution.reference_not_found"),
            TEXT("The normalized references operation has no exact target."));
    }

    FString Scope;
    Query.Operation->TryGetStringField(TEXT("scope"), Scope);
    const bool bProject = Scope == TEXT("project");
    const int32 Limit = FMath::Clamp(Query.PageLimit > 0 ? Query.PageLimit : DefaultPageLimit, 1, MaxPageLimit);

    CleanupSessions();
    TSharedPtr<FReferenceSession> Session;
    FString CursorRequestHash;
    int32 CursorRevision = 0;
    uint64 CursorGeneration = 0;
    FString CursorSnapshotHash;
    if (!Query.PageAfter.IsEmpty())
    {
        FString SessionId;
        if (!DecodeCursor(
                Query.PageAfter,
                SessionId,
                CursorRequestHash,
                CursorRevision,
                CursorGeneration,
                CursorSnapshotHash))
        {
            return ReferenceDiagnostic(
                TEXT("validation.invalid_cursor"),
                TEXT("Reference cursor is malformed. Re-run the first page."));
        }
        Session = GReferenceSessions.FindRef(SessionId);
        if (!Session.IsValid()
            || Session->Revision != CursorRevision
            || Session->Generation != CursorGeneration
            || Session->SnapshotHash != CursorSnapshotHash
            || Session->bProject != bProject)
        {
            return ReferenceDiagnostic(
                TEXT("validation.invalid_cursor"),
                TEXT("Reference cursor is expired or no longer belongs to the active scan. Re-run the first page."));
        }
        if (!Session->bAwaitingRegistry && Session->Generation != GReferenceGeneration.Load())
        {
            GReferenceSessions.Remove(SessionId);
            return ReferenceDiagnostic(
                TEXT("validation.invalid_cursor"),
                TEXT("Authored project state changed during reference pagination. Re-run the first page."));
        }
        if (bProject && !CurrentProjectEnvironmentMatches(*Session))
        {
            GReferenceSessions.Remove(SessionId);
            return ReferenceDiagnostic(
                TEXT("validation.invalid_cursor"),
                TEXT("Project roots or Blueprint asset handlers changed during reference pagination. Re-run the first page."));
        }
        Session->LastAccess = FPlatformTime::Seconds();
    }

    const FReferenceSubjectResolution Resolution = FSalReferenceFacts::ResolveSubject(Target, *OperationTarget);
    if (!Resolution.IsResolved())
    {
        return SubjectError(Resolution);
    }
    const FString ResolvedRequestHash = RequestHash(Query, Target, Resolution.Subject.Identity, bProject, Limit);
    if (Session.IsValid())
    {
        if (Session->RequestHash != ResolvedRequestHash
            || CursorRequestHash != ResolvedRequestHash
            || Session->IdentityKey != Resolution.Subject.Identity.StableKey())
        {
            return ReferenceDiagnostic(
                TEXT("validation.invalid_cursor"),
                TEXT("Reference cursor belongs to a different bound target, declaration, scope, or page limit."),
                Resolution.Subject.QueryRef);
        }
    }
    else
    {
        ReserveSessionSlot();
        Session = MakeShared<FReferenceSession>();
        Session->Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Session->RequestHash = ResolvedRequestHash;
        Session->IdentityKey = Resolution.Subject.Identity.StableKey();
        Session->bProject = bProject;
        Session->LastAccess = FPlatformTime::Seconds();

        FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
        IAssetRegistry* Registry = Module != nullptr ? &Module->Get() : nullptr;
        if (bProject)
        {
            if (Registry == nullptr)
            {
                return ReferenceDiagnostic(
                    TEXT("capability.reference_unavailable"),
                    TEXT("Asset Registry is unavailable for a project reference query."));
            }
            Session->RootsHash = StringArrayHash(ProjectRoots());
            Session->HandlerHash = HandlerHash(HandlerClassPaths());
            Session->SnapshotHash = TEXT("pending");
            Session->bAwaitingRegistry = true;
            FString SnapshotError;
            const ESnapshotResult Snapshot = BuildProjectSnapshot(*Session, *Registry, SnapshotError);
            if (Snapshot == ESnapshotResult::Error)
            {
                return ReferenceDiagnostic(
                    TEXT("validation.reference_scan_incomplete"),
                    SnapshotError,
                    Resolution.Subject.QueryRef);
            }
        }
        else
        {
            Session->Generation = GReferenceGeneration.Load();
            Session->bScanComplete = true;
            FString ScanError;
            UObject* Container = FindTopLevelContainer(Target.Blueprint, Registry);
            const FReferenceScanResult Scan = FSalReferenceFacts::ScanBlueprint(
                Target.Blueprint,
                Target.Kind == ESalTargetKind::Graph ? Target.Graph : nullptr,
                Resolution.Subject.Identity);
            if (!AppendScanResult(*Session, Scan, Container, Registry, ScanError))
            {
                return ReferenceDiagnostic(
                    TEXT("validation.reference_scan_incomplete"),
                    ScanError,
                    Resolution.Subject.QueryRef);
            }
        }
        GReferenceSessions.Add(Session->Id, Session);
    }

    FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry* Registry = Module != nullptr ? &Module->Get() : nullptr;
    if (Session->bProject && Registry == nullptr)
    {
        GReferenceSessions.Remove(Session->Id);
        return ReferenceDiagnostic(
            TEXT("capability.reference_unavailable"),
            TEXT("Asset Registry became unavailable during the project reference scan."));
    }

    if (Session->bAwaitingRegistry)
    {
        FString SnapshotError;
        const ESnapshotResult Snapshot = BuildProjectSnapshot(*Session, *Registry, SnapshotError);
        if (Snapshot == ESnapshotResult::Error)
        {
            GReferenceSessions.Remove(Session->Id);
            return ReferenceDiagnostic(
                TEXT("validation.reference_scan_incomplete"),
                SnapshotError,
                Resolution.Subject.QueryRef);
        }
        if (Snapshot == ESnapshotResult::Pending)
        {
            return BuildPageResult(*Session, {}, true, true);
        }
    }

    TArray<FReferenceRecord> PageRecords;
    PageRecords.Reserve(Limit);
    int32 ContainersProcessed = 0;
    const double StartedAt = FPlatformTime::Seconds();
    while (PageRecords.Num() < Limit)
    {
        while (Session->NextBuffered < Session->Buffered.Num() && PageRecords.Num() < Limit)
        {
            PageRecords.Add(MoveTemp(Session->Buffered[Session->NextBuffered++]));
        }
        if (Session->NextBuffered >= Session->Buffered.Num())
        {
            Session->Buffered.Reset();
            Session->NextBuffered = 0;
        }
        if (PageRecords.Num() >= Limit || Session->bScanComplete || !Session->bProject)
        {
            break;
        }
        if (ContainersProcessed >= MaxContainersPerCall
            || (ContainersProcessed > 0 && FPlatformTime::Seconds() - StartedAt >= MaxScanSecondsPerCall))
        {
            break;
        }
        if (Session->NextContainer >= Session->Containers.Num())
        {
            Session->bScanComplete = true;
            break;
        }

        const FAssetData Data = Session->Containers[Session->NextContainer++];
        ++ContainersProcessed;
        FString ScanError;
        const EContainerProcessResult ProcessResult = ProcessContainer(
            *Session,
            Data,
            *Registry,
            Resolution.Subject.Identity,
            ScanError);
        if (ProcessResult != EContainerProcessResult::Success)
        {
            GReferenceSessions.Remove(Session->Id);
            return ReferenceDiagnostic(
                ProcessResult == EContainerProcessResult::ScanIncomplete
                    ? TEXT("validation.reference_scan_incomplete")
                    : TEXT("validation.reference_container_failed"),
                ScanError,
                Resolution.Subject.QueryRef);
        }
        Session->bScanComplete = Session->NextContainer >= Session->Containers.Num();
    }

    if (Session->Generation != GReferenceGeneration.Load())
    {
        GReferenceSessions.Remove(Session->Id);
        return ReferenceDiagnostic(
            TEXT("validation.invalid_cursor"),
            TEXT("Authored project state changed while the reference page was being produced. Re-run the first page."),
            Resolution.Subject.QueryRef);
    }

    Session->TotalEmitted += PageRecords.Num();
    const bool bHasNext = Session->NextBuffered < Session->Buffered.Num() || !Session->bScanComplete;
    TSharedPtr<FJsonObject> Result = BuildPageResult(
        *Session,
        PageRecords,
        bHasNext,
        bHasNext && PageRecords.IsEmpty());
    if (!bHasNext)
    {
        GReferenceSessions.Remove(Session->Id);
    }
    return Result;
}
}
