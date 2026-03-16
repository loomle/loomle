// Runtime tool handlers (context / selection / execute / diag).
namespace
{
constexpr int32 DefaultDiagTailLimit = 200;
constexpr int32 MaxDiagTailLimit = 1000;

bool TryReadJsonUInt64Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, uint64& Out)
{
    Out = 0;
    if (!Object.IsValid())
    {
        return false;
    }

    double Number = 0.0;
    if (Object->TryGetNumberField(FieldName, Number))
    {
        if (Number < 0.0)
        {
            return false;
        }
        Out = static_cast<uint64>(Number);
        return true;
    }

    FString Text;
    if (Object->TryGetStringField(FieldName, Text))
    {
        return LexTryParseString<uint64>(Out, *Text);
    }

    return false;
}

bool DiagEventMatchesFilters(const TSharedPtr<FJsonObject>& Event, const TSharedPtr<FJsonObject>& Filters)
{
    if (!Event.IsValid() || !Filters.IsValid())
    {
        return true;
    }

    auto MatchStringField = [&](const TCHAR* FilterField, const TCHAR* EventField) -> bool
    {
        FString FilterValue;
        if (!Filters->TryGetStringField(FilterField, FilterValue) || FilterValue.IsEmpty())
        {
            return true;
        }

        FString EventValue;
        if (!Event->TryGetStringField(EventField, EventValue))
        {
            return false;
        }
        return EventValue.Equals(FilterValue, ESearchCase::IgnoreCase);
    };

    if (!MatchStringField(TEXT("severity"), TEXT("severity")))
    {
        return false;
    }
    if (!MatchStringField(TEXT("category"), TEXT("category")))
    {
        return false;
    }
    if (!MatchStringField(TEXT("source"), TEXT("source")))
    {
        return false;
    }

    FString AssetPathPrefix;
    if (!Filters->TryGetStringField(TEXT("assetPathPrefix"), AssetPathPrefix) || AssetPathPrefix.IsEmpty())
    {
        return true;
    }

    FString AssetPath;
    if (!Event->TryGetStringField(TEXT("assetPath"), AssetPath))
    {
        const TSharedPtr<FJsonObject>* ContextObject = nullptr;
        if (Event->TryGetObjectField(TEXT("context"), ContextObject) && ContextObject && (*ContextObject).IsValid())
        {
            (*ContextObject)->TryGetStringField(TEXT("assetPath"), AssetPath);
        }
    }

    if (AssetPath.IsEmpty())
    {
        return false;
    }

    return AssetPath.StartsWith(AssetPathPrefix);
}
}

void FLoomleBridgeModule::InitializeDiagStore()
{
    FScopeLock Lock(&DiagStoreMutex);
    if (bDiagStoreInitialized)
    {
        return;
    }

    DiagStoreDirPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Loomle"), TEXT("runtime"), TEXT("diag"));
    DiagStoreFilePath = FPaths::Combine(DiagStoreDirPath, TEXT("diag.jsonl"));
    IFileManager::Get().MakeDirectory(*DiagStoreDirPath, true);

    NextDiagSeq = 1;
    TArray<FString> Lines;
    if (FPaths::FileExists(DiagStoreFilePath) && FFileHelper::LoadFileToStringArray(Lines, *DiagStoreFilePath))
    {
        for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
        {
            const FString& Line = Lines[Index];
            if (Line.TrimStartAndEnd().IsEmpty())
            {
                continue;
            }

            TSharedPtr<FJsonObject> Event;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
            if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
            {
                continue;
            }

            uint64 ParsedSeq = 0;
            if (!TryReadJsonUInt64Field(Event, TEXT("seq"), ParsedSeq))
            {
                continue;
            }

            NextDiagSeq = ParsedSeq + 1;
            break;
        }
    }

    bDiagStoreInitialized = true;
}

void FLoomleBridgeModule::AppendDiagEvent(
    const FString& Severity,
    const FString& Category,
    const FString& Source,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Context)
{
    if (!bDiagStoreInitialized)
    {
        InitializeDiagStore();
    }

    FScopeLock Lock(&DiagStoreMutex);
    if (DiagStoreFilePath.IsEmpty())
    {
        return;
    }

    const uint64 Seq = NextDiagSeq;
    TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
    Event->SetNumberField(TEXT("seq"), static_cast<double>(Seq));
    Event->SetStringField(TEXT("ts"), FDateTime::UtcNow().ToIso8601());
    Event->SetStringField(TEXT("severity"), Severity.ToLower());
    Event->SetStringField(TEXT("category"), Category.ToLower());
    Event->SetStringField(TEXT("source"), Source.ToLower());
    Event->SetStringField(TEXT("message"), Message);
    if (Context.IsValid())
    {
        Event->SetObjectField(TEXT("context"), Context);
        FString AssetPath;
        if (Context->TryGetStringField(TEXT("assetPath"), AssetPath) && !AssetPath.IsEmpty())
        {
            Event->SetStringField(TEXT("assetPath"), AssetPath);
        }
    }

    FString JsonLine;
    const TSharedRef<FCondensedJsonWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonLine);
    if (!FJsonSerializer::Serialize(Event.ToSharedRef(), Writer))
    {
        return;
    }

    JsonLine.AppendChar(TEXT('\n'));
    if (FFileHelper::SaveStringToFile(
            JsonLine,
            *DiagStoreFilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(),
            FILEWRITE_Append))
    {
        ++NextDiagSeq;
        return;
    }

    // Avoid recursive logging loops when persistence itself fails.
}

void FLoomleBridgeModule::HandleLogLine(const FString& Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
    if (Message.IsEmpty())
    {
        return;
    }

    const FString CategoryName = Category.ToString();
    if (CategoryName.Equals(TEXT("LogLoomleBridge"), ESearchCase::IgnoreCase))
    {
        return;
    }

    const FString Severity = (Verbosity == ELogVerbosity::Warning) ? TEXT("warning") : TEXT("error");
    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("categoryName"), CategoryName);
    AppendDiagEvent(Severity, TEXT("runtime"), TEXT("log"), Message, Context);
}

void FLoomleBridgeModule::HandleBlueprintCompiled()
{
    TSet<FString> CurrentErrorAssets;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Blueprint = *It;
        if (Blueprint == nullptr || Blueprint->HasAnyFlags(RF_Transient) || Blueprint->Status != BS_Error)
        {
            continue;
        }

        const FString AssetPath = Blueprint->GetPathName();
        if (AssetPath.IsEmpty())
        {
            continue;
        }

        CurrentErrorAssets.Add(AssetPath);
        if (BlueprintCompileErrorAssets.Contains(AssetPath))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
        Context->SetStringField(TEXT("assetPath"), AssetPath);
        Context->SetStringField(TEXT("assetName"), Blueprint->GetName());
        Context->SetStringField(TEXT("status"), TEXT("error"));
        AppendDiagEvent(
            TEXT("error"),
            TEXT("compile"),
            TEXT("blueprint"),
            FString::Printf(TEXT("Blueprint compile failed: %s"), *Blueprint->GetName()),
            Context);
    }

    BlueprintCompileErrorAssets = MoveTemp(CurrentErrorAssets);
}

namespace
{
TSharedPtr<FJsonObject> MakePcgRuntimeDiagnostic(
    const FString& Code,
    const FString& Severity,
    const FString& Message)
{
    TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
    Diagnostic->SetStringField(TEXT("code"), Code);
    Diagnostic->SetStringField(TEXT("severity"), Severity);
    Diagnostic->SetStringField(TEXT("message"), Message);
    return Diagnostic;
}

void AddPcgRuntimeDiagnostic(
    TArray<TSharedPtr<FJsonValue>>& OutDiagnostics,
    const FString& Code,
    const FString& Severity,
    const FString& Message)
{
    OutDiagnostics.Add(MakeShared<FJsonValueObject>(MakePcgRuntimeDiagnostic(Code, Severity, Message)));
}

TArray<TSharedPtr<FJsonValue>> MakeSortedStringCountArray(const TMap<FString, int32>& Counts)
{
    TArray<FString> Keys;
    Counts.GetKeys(Keys);
    Keys.Sort();

    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(Keys.Num());
    for (const FString& Key : Keys)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("key"), Key);
        Item->SetNumberField(TEXT("count"), Counts.FindRef(Key));
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Items;
}

TSharedPtr<FJsonObject> BuildPcgGeneratedGraphOutputSummary(const FPCGDataCollection& Collection)
{
    struct FPinSummary
    {
        int32 Count = 0;
        TMap<FString, int32> DataTypes;
        TSet<FString> Tags;
    };

    TMap<FString, int32> DataTypeCounts;
    TMap<FString, FPinSummary> PinSummaries;

    for (const FPCGTaggedData& TaggedData : Collection.TaggedData)
    {
        const FString PinName = TaggedData.Pin.IsNone() ? TEXT("") : TaggedData.Pin.ToString();
        const FString DataClassPath = TaggedData.Data.Get() ? TaggedData.Data->GetClass()->GetPathName() : TEXT("");

        ++DataTypeCounts.FindOrAdd(DataClassPath);

        FPinSummary& PinSummary = PinSummaries.FindOrAdd(PinName);
        ++PinSummary.Count;
        ++PinSummary.DataTypes.FindOrAdd(DataClassPath);
        for (const FString& Tag : TaggedData.Tags)
        {
            if (!Tag.IsEmpty())
            {
                PinSummary.Tags.Add(Tag);
            }
        }
    }

    TArray<FString> PinNames;
    PinSummaries.GetKeys(PinNames);
    PinNames.Sort();

    TArray<TSharedPtr<FJsonValue>> Pins;
    Pins.Reserve(PinNames.Num());
    for (const FString& PinName : PinNames)
    {
        const FPinSummary& PinSummary = PinSummaries[PinName];
        TSharedPtr<FJsonObject> PinObject = MakeShared<FJsonObject>();
        PinObject->SetStringField(TEXT("pin"), PinName);
        PinObject->SetNumberField(TEXT("itemCount"), PinSummary.Count);
        PinObject->SetArrayField(TEXT("dataTypes"), MakeSortedStringCountArray(PinSummary.DataTypes));

        TArray<FString> Tags = PinSummary.Tags.Array();
        Tags.Sort();
        TArray<TSharedPtr<FJsonValue>> TagValues;
        TagValues.Reserve(Tags.Num());
        for (const FString& Tag : Tags)
        {
            TagValues.Add(MakeShared<FJsonValueString>(Tag));
        }
        PinObject->SetArrayField(TEXT("tags"), TagValues);
        Pins.Add(MakeShared<FJsonValueObject>(PinObject));
    }

    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetNumberField(TEXT("taggedDataCount"), Collection.TaggedData.Num());
    Summary->SetArrayField(TEXT("dataTypes"), MakeSortedStringCountArray(DataTypeCounts));
    Summary->SetArrayField(TEXT("pins"), Pins);
    return Summary;
}

TSharedPtr<FJsonObject> BuildPcgManagedResourcesSummary(UPCGComponent* Component)
{
    TMap<FString, int32> ResourceTypeCounts;
    TMap<FString, int32> ComponentTypeCounts;
    TSet<FString> GeneratedActorPaths;
    TMap<FString, UActorComponent*> GeneratedComponentsByPath;
    int32 ResourceCount = 0;
    int32 TotalInstanceCount = 0;

    auto AddGeneratedComponent = [&](UActorComponent* GeneratedComponent)
    {
        if (GeneratedComponent == nullptr)
        {
            return;
        }

        const FString ComponentPath = GeneratedComponent->GetPathName();
        if (GeneratedComponentsByPath.Contains(ComponentPath))
        {
            return;
        }

        GeneratedComponentsByPath.Add(ComponentPath, GeneratedComponent);
        ++ComponentTypeCounts.FindOrAdd(GeneratedComponent->GetClass()->GetPathName());

        if (UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(GeneratedComponent))
        {
            TotalInstanceCount += InstancedComponent->GetInstanceCount();
        }
    };

    Component->ForEachConstManagedResource([&](const UPCGManagedResource* Resource)
    {
        if (Resource == nullptr)
        {
            return;
        }

        ++ResourceCount;
        ++ResourceTypeCounts.FindOrAdd(Resource->GetClass()->GetPathName());

        if (const UPCGManagedActors* ManagedActors = Cast<UPCGManagedActors>(Resource))
        {
            for (const TSoftObjectPtr<AActor>& GeneratedActor : ManagedActors->GetConstGeneratedActors())
            {
                if (AActor* Actor = GeneratedActor.Get())
                {
                    GeneratedActorPaths.Add(Actor->GetPathName());
                }
            }
        }
        else if (const UPCGManagedComponentList* ManagedComponentList = Cast<UPCGManagedComponentList>(Resource))
        {
            for (const TSoftObjectPtr<UActorComponent>& GeneratedComponent : ManagedComponentList->GeneratedComponents)
            {
                AddGeneratedComponent(GeneratedComponent.Get());
            }
        }
        else if (const UPCGManagedComponent* ManagedComponent = Cast<UPCGManagedComponent>(Resource))
        {
            AddGeneratedComponent(ManagedComponent->GeneratedComponent.Get());
        }
    });

    TArray<FString> ActorPaths = GeneratedActorPaths.Array();
    ActorPaths.Sort();

    TArray<TSharedPtr<FJsonValue>> Actors;
    Actors.Reserve(ActorPaths.Num());
    for (const FString& ActorPath : ActorPaths)
    {
        Actors.Add(MakeShared<FJsonValueString>(ActorPath));
    }

    TArray<FString> ComponentPaths;
    GeneratedComponentsByPath.GetKeys(ComponentPaths);
    ComponentPaths.Sort();

    TArray<TSharedPtr<FJsonValue>> Components;
    Components.Reserve(ComponentPaths.Num());
    for (const FString& ComponentPath : ComponentPaths)
    {
        UActorComponent* GeneratedComponent = GeneratedComponentsByPath[ComponentPath];
        TSharedPtr<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
        ComponentObject->SetStringField(TEXT("path"), ComponentPath);
        ComponentObject->SetStringField(TEXT("classPath"), GeneratedComponent->GetClass()->GetPathName());
        ComponentObject->SetStringField(
            TEXT("ownerPath"),
            GeneratedComponent->GetOwner() ? GeneratedComponent->GetOwner()->GetPathName() : TEXT(""));
        if (UInstancedStaticMeshComponent* InstancedComponent = Cast<UInstancedStaticMeshComponent>(GeneratedComponent))
        {
            ComponentObject->SetNumberField(TEXT("instanceCount"), InstancedComponent->GetInstanceCount());
        }
        Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
    }

    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetNumberField(TEXT("resourceCount"), ResourceCount);
    Summary->SetBoolField(TEXT("accessible"), Component->AreManagedResourcesAccessible());
    Summary->SetNumberField(TEXT("generatedActorCount"), ActorPaths.Num());
    Summary->SetNumberField(TEXT("generatedComponentCount"), ComponentPaths.Num());
    Summary->SetNumberField(TEXT("totalInstanceCount"), TotalInstanceCount);
    Summary->SetArrayField(TEXT("resourceTypes"), MakeSortedStringCountArray(ResourceTypeCounts));
    Summary->SetArrayField(TEXT("componentTypes"), MakeSortedStringCountArray(ComponentTypeCounts));
    Summary->SetArrayField(TEXT("actors"), Actors);
    Summary->SetArrayField(TEXT("components"), Components);
    return Summary;
}

TSharedPtr<FJsonObject> BuildPcgInspectionSummary(UPCGComponent* Component)
{
    TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();

#if WITH_EDITOR
    const FPCGGraphExecutionInspection& Inspection = Component->GetExecutionState().GetInspection();
    const TMap<TObjectKey<const UPCGNode>, TSet<FPCGGraphExecutionInspection::FNodeExecutedNotificationData>> ExecutedNodeStacks =
        Inspection.GetExecutedNodeStacks();

    TArray<TSharedPtr<FJsonValue>> Nodes;
    Nodes.Reserve(ExecutedNodeStacks.Num());
    int32 ProducedNodeCount = 0;

    for (const auto& Pair : ExecutedNodeStacks)
    {
        const UPCGNode* Node = Pair.Key.ResolveObjectPtr();
        if (Node == nullptr)
        {
            continue;
        }

        int32 ProducedStacks = 0;
        FString ExampleStackPath;
        for (const FPCGGraphExecutionInspection::FNodeExecutedNotificationData& Notification : Pair.Value)
        {
            if (Inspection.HasNodeProducedData(Node, Notification.Stack))
            {
                ++ProducedStacks;
            }

            if (ExampleStackPath.IsEmpty())
            {
                Notification.Stack.CreateStackFramePath(ExampleStackPath, Node);
            }
        }

        if (ProducedStacks > 0)
        {
            ++ProducedNodeCount;
        }

        TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
        NodeObject->SetStringField(TEXT("nodeId"), Node->GetPathName());
        NodeObject->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
        NodeObject->SetStringField(TEXT("classPath"), Node->GetClass()->GetPathName());
        NodeObject->SetNumberField(TEXT("executedStacks"), Pair.Value.Num());
        NodeObject->SetNumberField(TEXT("producedStacks"), ProducedStacks);
        NodeObject->SetStringField(TEXT("exampleStackPath"), ExampleStackPath);
        Nodes.Add(MakeShared<FJsonValueObject>(NodeObject));
    }

    Nodes.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
    {
        const TSharedPtr<FJsonObject>* AObject = nullptr;
        const TSharedPtr<FJsonObject>* BObject = nullptr;
        const FString APath = (A.IsValid() && A->TryGetObject(AObject) && AObject && (*AObject).IsValid())
            ? (*AObject)->GetStringField(TEXT("nodeId"))
            : FString();
        const FString BPath = (B.IsValid() && B->TryGetObject(BObject) && BObject && (*BObject).IsValid())
            ? (*BObject)->GetStringField(TEXT("nodeId"))
            : FString();
        return APath < BPath;
    });

    Summary->SetBoolField(TEXT("available"), !ExecutedNodeStacks.IsEmpty());
    Summary->SetNumberField(TEXT("executedNodeCount"), Nodes.Num());
    Summary->SetNumberField(TEXT("producedNodeCount"), ProducedNodeCount);
    Summary->SetArrayField(TEXT("nodes"), Nodes);
#else
    Summary->SetBoolField(TEXT("available"), false);
    Summary->SetNumberField(TEXT("executedNodeCount"), 0);
    Summary->SetNumberField(TEXT("producedNodeCount"), 0);
    Summary->SetArrayField(TEXT("nodes"), TArray<TSharedPtr<FJsonValue>>{});
#endif

    return Summary;
}
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGraphRuntimeToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString GraphType;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("graphType"), GraphType) || !GraphType.Equals(TEXT("pcg"), ESearchCase::IgnoreCase))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("UNSUPPORTED_GRAPH_TYPE"));
        Result->SetStringField(TEXT("message"), TEXT("graph.runtime currently supports graphType=\"pcg\" only."));
        return Result;
    }

    FString ResolvedBy;
    FString ErrorCode;
    FString ErrorMessage;
    UPCGComponent* PcgComponent = ResolvePcgComponentForRuntimeInspect(Arguments, ResolvedBy, ErrorCode, ErrorMessage);
    if (PcgComponent == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), ErrorCode.IsEmpty() ? TEXT("INVALID_ARGUMENT") : ErrorCode);
        Result->SetStringField(TEXT("message"), ErrorMessage.IsEmpty()
            ? TEXT("Failed to resolve a PCGComponent for runtime inspection.")
            : ErrorMessage);
        return Result;
    }

    FString GraphAssetPath;
    if (const UPCGGraph* Graph = PcgComponent->GetGraph())
    {
        TryGetAssetPathFromObject(Graph, GraphAssetPath);
    }

    const TSharedPtr<FJsonObject> GeneratedGraphOutputSummary = BuildPcgGeneratedGraphOutputSummary(PcgComponent->GetGeneratedGraphOutput());
    const TSharedPtr<FJsonObject> ManagedResourcesSummary = BuildPcgManagedResourcesSummary(PcgComponent);
    const TSharedPtr<FJsonObject> InspectionSummary = BuildPcgInspectionSummary(PcgComponent);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    if (!PcgComponent->bGenerated && !PcgComponent->IsGenerating())
    {
        AddPcgRuntimeDiagnostic(
            Diagnostics,
            TEXT("PCG_NOT_GENERATED"),
            TEXT("info"),
            TEXT("The PCG component is not currently generated. Run generation before trusting runtime output state."));
    }

    if (!PcgComponent->AreManagedResourcesAccessible())
    {
        AddPcgRuntimeDiagnostic(
            Diagnostics,
            TEXT("PCG_MANAGED_RESOURCES_INACCESSIBLE"),
            TEXT("warning"),
            TEXT("Managed resources are currently inaccessible, so generated component/actor summaries may be incomplete."));
    }

    const int32 TaggedDataCount = GeneratedGraphOutputSummary->GetIntegerField(TEXT("taggedDataCount"));
    const int32 GeneratedActorCount = ManagedResourcesSummary->GetIntegerField(TEXT("generatedActorCount"));
    const int32 GeneratedComponentCount = ManagedResourcesSummary->GetIntegerField(TEXT("generatedComponentCount"));
    if (PcgComponent->bGenerated && TaggedDataCount == 0 && (GeneratedActorCount > 0 || GeneratedComponentCount > 0))
    {
        AddPcgRuntimeDiagnostic(
            Diagnostics,
            TEXT("PCG_RUNTIME_OUTPUT_COMPONENT_MISMATCH"),
            TEXT("warning"),
            TEXT("Managed generated actors/components exist, but GetGeneratedGraphOutput() is empty. Prefer the managed resource summary for spawned-result validation."));
    }

    if (TaggedDataCount == 0)
    {
        AddPcgRuntimeDiagnostic(
            Diagnostics,
            TEXT("PCG_GENERATED_GRAPH_OUTPUT_EMPTY"),
            TEXT("info"),
            TEXT("GeneratedGraphOutput contains no tagged data. This can happen for common spawner-style PCG graphs even when visible generated results exist."));
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("resolvedBy"), ResolvedBy);
    Result->SetStringField(TEXT("componentPath"), PcgComponent->GetPathName());
    Result->SetStringField(TEXT("componentName"), PcgComponent->GetName());
    Result->SetStringField(TEXT("actorPath"), PcgComponent->GetOwner() ? PcgComponent->GetOwner()->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("graphAssetPath"), GraphAssetPath);
    Result->SetBoolField(TEXT("generated"), PcgComponent->bGenerated);
    Result->SetBoolField(TEXT("generating"), PcgComponent->IsGenerating());
    Result->SetBoolField(TEXT("managedResourcesAccessible"), PcgComponent->AreManagedResourcesAccessible());
    Result->SetObjectField(TEXT("generatedGraphOutput"), GeneratedGraphOutputSummary);
    Result->SetObjectField(TEXT("managedResources"), ManagedResourcesSummary);
    Result->SetObjectField(TEXT("inspection"), InspectionSummary);
    Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildDiagTailToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    if (!bDiagStoreInitialized)
    {
        InitializeDiagStore();
    }

    uint64 FromSeq = 0;
    if (Arguments.IsValid() && Arguments->HasField(TEXT("fromSeq")))
    {
        if (!TryReadJsonUInt64Field(Arguments, TEXT("fromSeq"), FromSeq))
        {
            TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
            Error->SetBoolField(TEXT("isError"), true);
            Error->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Error->SetStringField(TEXT("message"), TEXT("fromSeq must be a non-negative integer."));
            return Error;
        }
    }

    int32 Limit = DefaultDiagTailLimit;
    double LimitNumber = 0.0;
    if (Arguments.IsValid() && Arguments->TryGetNumberField(TEXT("limit"), LimitNumber))
    {
        if (LimitNumber < 1.0)
        {
            TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
            Error->SetBoolField(TEXT("isError"), true);
            Error->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
            Error->SetStringField(TEXT("message"), TEXT("limit must be >= 1."));
            return Error;
        }
        Limit = FMath::Clamp(static_cast<int32>(LimitNumber), 1, MaxDiagTailLimit);
    }

    TSharedPtr<FJsonObject> Filters;
    const TSharedPtr<FJsonObject>* FiltersPtr = nullptr;
    if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("filters"), FiltersPtr) && FiltersPtr && (*FiltersPtr).IsValid())
    {
        Filters = *FiltersPtr;
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    uint64 NextSeq = FromSeq;
    bool bHasMore = false;
    uint64 HighWatermark = 0;

    FScopeLock Lock(&DiagStoreMutex);
    HighWatermark = NextDiagSeq > 0 ? (NextDiagSeq - 1) : 0;

    TArray<FString> Lines;
    if (FPaths::FileExists(DiagStoreFilePath))
    {
        FFileHelper::LoadFileToStringArray(Lines, *DiagStoreFilePath);
    }

    for (const FString& Line : Lines)
    {
        if (Line.TrimStartAndEnd().IsEmpty())
        {
            continue;
        }

        TSharedPtr<FJsonObject> Event;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
        if (!FJsonSerializer::Deserialize(Reader, Event) || !Event.IsValid())
        {
            continue;
        }

        uint64 Seq = 0;
        if (!TryReadJsonUInt64Field(Event, TEXT("seq"), Seq) || Seq <= FromSeq)
        {
            continue;
        }

        if (!DiagEventMatchesFilters(Event, Filters))
        {
            continue;
        }

        if (Items.Num() >= Limit)
        {
            bHasMore = true;
            break;
        }

        Items.Add(MakeShared<FJsonValueObject>(Event));
        NextSeq = Seq;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("nextSeq"), static_cast<double>(NextSeq));
    Result->SetBoolField(TEXT("hasMore"), bHasMore);
    Result->SetNumberField(TEXT("highWatermark"), static_cast<double>(HighWatermark));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildGetContextToolResult(const TSharedPtr<FJsonObject>& Arguments) const
{
    (void)Arguments;

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);
    Result->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());

    TSharedPtr<FJsonObject> Runtime = MakeShared<FJsonObject>();
    Runtime->SetStringField(TEXT("projectName"), FApp::GetProjectName());
    Runtime->SetStringField(TEXT("projectFilePath"), FPaths::GetProjectFilePath());
    Runtime->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
    UWorld* EditorWorld = nullptr;
    if (GEditor)
    {
        EditorWorld = GEditor->GetEditorWorldContext().World();
        Runtime->SetBoolField(TEXT("isPIE"), GEditor->IsPlayingSessionInEditor());
    }
    else
    {
        Runtime->SetBoolField(TEXT("isPIE"), false);
    }
    Runtime->SetStringField(TEXT("editorWorld"), EditorWorld ? EditorWorld->GetName() : TEXT(""));
    Result->SetObjectField(TEXT("runtime"), Runtime);
    Result->SetObjectField(TEXT("activeWindow"), BuildActiveWindowJson());

    TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("source"), TEXT("unified"));
    TSharedPtr<FJsonObject> BlueprintContext;
    if (BuildBlueprintContextSnapshot(BlueprintContext) && BlueprintContext.IsValid())
    {
        Context = BlueprintContext;
        Context->SetStringField(TEXT("source"), TEXT("unified"));
    }
    else
    {
        TSharedPtr<FJsonObject> MaterialContext;
        if (BuildMaterialContextSnapshot(MaterialContext) && MaterialContext.IsValid())
        {
            Context = MaterialContext;
            Context->SetStringField(TEXT("source"), TEXT("unified"));
        }
        else
        {
            TSharedPtr<FJsonObject> PcgContext;
            if (BuildPcgContextSnapshot(PcgContext) && PcgContext.IsValid())
            {
                Context = PcgContext;
                Context->SetStringField(TEXT("source"), TEXT("unified"));
            }
        }
    }
    Result->SetObjectField(TEXT("context"), Context);

    TSharedPtr<FJsonObject> Selection = BuildSelectionTransformToolResult();
    Result->SetObjectField(TEXT("selection"), Selection);

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildSelectionTransformToolResult() const
{
    TSharedPtr<FJsonObject> BlueprintSelection;
    if (BuildBlueprintSelectionSnapshot(BlueprintSelection) && BlueprintSelection.IsValid())
    {
        BlueprintSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return BlueprintSelection;
    }

    TSharedPtr<FJsonObject> MaterialSelection;
    if (BuildMaterialSelectionSnapshot(MaterialSelection) && MaterialSelection.IsValid())
    {
        MaterialSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return MaterialSelection;
    }

    TSharedPtr<FJsonObject> PcgSelection;
    if (BuildPcgSelectionSnapshot(PcgSelection) && PcgSelection.IsValid())
    {
        PcgSelection->SetStringField(TEXT("source"), TEXT("unified"));
        return PcgSelection;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("isError"), false);

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> ResolvedGraphRefs;
    TSet<FString> SelectionSeenGraphRefs;
    FBox AggregateBox(EForceInit::ForceInit);
    TSharedPtr<FJsonObject> ActiveItem;

    if (GEditor)
    {
        USelection* Selection = GEditor->GetSelectedActors();
        if (Selection)
        {
            for (FSelectionIterator It(*Selection); It; ++It)
            {
                AActor* Actor = Cast<AActor>(*It);
                if (!Actor)
                {
                    continue;
                }

                const FVector Location = Actor->GetActorLocation();
                const FRotator Rotation = Actor->GetActorRotation();
                const FVector Scale = Actor->GetActorScale3D();

                FVector BoundsOrigin(ForceInitToZero);
                FVector BoundsExtent(ForceInitToZero);
                Actor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
                AggregateBox += (BoundsOrigin - BoundsExtent);
                AggregateBox += (BoundsOrigin + BoundsExtent);

                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("id"), Actor->GetPathName());
                Item->SetStringField(TEXT("name"), Actor->GetActorLabel());
                Item->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
                Item->SetStringField(TEXT("path"), Actor->GetPathName());

                TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
                LocationObj->SetNumberField(TEXT("x"), Location.X);
                LocationObj->SetNumberField(TEXT("y"), Location.Y);
                LocationObj->SetNumberField(TEXT("z"), Location.Z);
                Item->SetObjectField(TEXT("location"), LocationObj);

                TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
                RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
                RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
                RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
                Item->SetObjectField(TEXT("rotation"), RotationObj);

                TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
                ScaleObj->SetNumberField(TEXT("x"), Scale.X);
                ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
                ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
                Item->SetObjectField(TEXT("scale"), ScaleObj);

                TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
                TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
                OriginObj->SetNumberField(TEXT("x"), BoundsOrigin.X);
                OriginObj->SetNumberField(TEXT("y"), BoundsOrigin.Y);
                OriginObj->SetNumberField(TEXT("z"), BoundsOrigin.Z);
                BoundsObj->SetObjectField(TEXT("origin"), OriginObj);

                TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
                ExtentObj->SetNumberField(TEXT("x"), BoundsExtent.X);
                ExtentObj->SetNumberField(TEXT("y"), BoundsExtent.Y);
                ExtentObj->SetNumberField(TEXT("z"), BoundsExtent.Z);
                BoundsObj->SetObjectField(TEXT("boxExtent"), ExtentObj);

                BoundsObj->SetNumberField(TEXT("sphereRadius"), BoundsExtent.Size());
                Item->SetObjectField(TEXT("bounds"), BoundsObj);

                TArray<TSharedPtr<FJsonValue>> ItemResolvedGraphRefs;
                TSet<FString> ItemSeenGraphRefs;
                AppendActorResolvedGraphRefs(Actor, ItemResolvedGraphRefs, ItemSeenGraphRefs);
                SetResolvedGraphRefsFieldIfAny(Item, ItemResolvedGraphRefs);
                CopyResolvedGraphRefEntries(ItemResolvedGraphRefs, ResolvedGraphRefs, SelectionSeenGraphRefs);

                if (!ActiveItem.IsValid())
                {
                    ActiveItem = Item;
                }

                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
    }

    Result->SetArrayField(TEXT("items"), Items);
    Result->SetNumberField(TEXT("count"), Items.Num());
    SetResolvedGraphRefsFieldIfAny(Result, ResolvedGraphRefs);

    if (Items.Num() > 0)
    {
        TSharedPtr<FJsonObject> Aggregate = MakeShared<FJsonObject>();
        const FVector Center = AggregateBox.GetCenter();
        const FVector Extent = AggregateBox.GetExtent();

        TSharedPtr<FJsonObject> CenterObj = MakeShared<FJsonObject>();
        CenterObj->SetNumberField(TEXT("x"), Center.X);
        CenterObj->SetNumberField(TEXT("y"), Center.Y);
        CenterObj->SetNumberField(TEXT("z"), Center.Z);
        Aggregate->SetObjectField(TEXT("center"), CenterObj);

        TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
        ExtentObj->SetNumberField(TEXT("x"), Extent.X);
        ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
        ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
        Aggregate->SetObjectField(TEXT("boxExtent"), ExtentObj);

        Result->SetObjectField(TEXT("aggregate"), Aggregate);
    }

    if (ActiveItem.IsValid())
    {
        Result->SetObjectField(TEXT("active"), ActiveItem);
    }

    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorOpenToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath is required."));
        return Result;
    }

    if (!GEditor)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Editor is not available."));
        return Result;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("AssetEditorSubsystem is not available."));
        return Result;
    }

    UObject* Asset = LoadObjectByAssetPath(AssetPath);
    if (Asset == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Asset not found."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    FText OpenError;
    if (!AssetEditorSubsystem->CanOpenEditorForAsset(Asset, EAssetTypeActivationOpenedMethod::Edit, &OpenError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), OpenError.IsEmpty() ? TEXT("Asset cannot be opened in an editor.") : OpenError.ToString());
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    const bool bAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
    const bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);
    const bool bWindowFocused = AssetEditorSubsystem->FindEditorForAsset(Asset, true) != nullptr;
    const TSharedPtr<FJsonObject> ActiveWindow = BuildActiveWindowJson();

    if (!bOpened && !bAlreadyOpen)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to open asset editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("assetName"), Asset->GetName());
    Result->SetStringField(TEXT("assetClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
    Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
    Result->SetBoolField(TEXT("windowFocused"), bWindowFocused);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetStringField(TEXT("title")) : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorFocusToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString AssetPath;
    FString Panel;
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath)
        || AssetPath.TrimStartAndEnd().IsEmpty()
        || !Arguments->TryGetStringField(TEXT("panel"), Panel)
        || Panel.TrimStartAndEnd().IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.assetPath and arguments.panel are required."));
        return Result;
    }

    if (!GEditor)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Editor is not available."));
        return Result;
    }

    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (AssetEditorSubsystem == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("AssetEditorSubsystem is not available."));
        return Result;
    }

    UObject* Asset = LoadObjectByAssetPath(AssetPath);
    if (Asset == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("ASSET_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("Asset not found."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    FText OpenError;
    if (!AssetEditorSubsystem->CanOpenEditorForAsset(Asset, EAssetTypeActivationOpenedMethod::Edit, &OpenError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), OpenError.IsEmpty() ? TEXT("Asset cannot be opened in an editor.") : OpenError.ToString());
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    const bool bAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
    if (!bAlreadyOpen && !AssetEditorSubsystem->OpenEditorForAsset(Asset))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to open asset editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, true);
    if (EditorInstance == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("EDITOR_NOT_AVAILABLE"));
        Result->SetStringField(TEXT("message"), TEXT("Asset editor is not available."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        return Result;
    }

    Panel = NormalizeEditorPanel(Panel);
    FString EditorType;
    bool bPanelFocused = false;

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        EditorType = TEXT("blueprint");
        FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(EditorInstance);

        if (Panel.Equals(TEXT("graph")))
        {
            if (UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint))
            {
                bPanelFocused = BlueprintEditor->OpenGraphAndBringToFront(EventGraph, true).IsValid();
            }
        }
        else if (Panel.Equals(TEXT("constructionscript")))
        {
            if (UEdGraph* ConstructionGraph = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
            {
                bPanelFocused = BlueprintEditor->OpenGraphAndBringToFront(ConstructionGraph, true).IsValid();
            }
        }
        else if (Panel.Equals(TEXT("viewport")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::SCSViewportID));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::DetailsID));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("myblueprint")))
        {
            EditorInstance->InvokeTab(FTabId(FBlueprintEditorTabs::MyBlueprintID));
            bPanelFocused = true;
        }
    }
    else if (IsMaterialLikeAsset(Asset))
    {
        EditorType = TEXT("material");

        if (Panel.Equals(TEXT("graph")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorGraphTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPropertiesTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("palette")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPaletteTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("find")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorFindTabId));
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("preview")))
        {
            EditorInstance->InvokeTab(FTabId(MaterialEditorPreviewTabId));
            bPanelFocused = true;
        }
    }
    else if (ResolvePcgGraphFromAsset(Asset) != nullptr)
    {
        EditorType = TEXT("pcg");
        FPCGEditor* PCGEditor = static_cast<FPCGEditor*>(EditorInstance);

        if (Panel.Equals(TEXT("graph")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::GraphEditor);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("details")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::PropertyDetails1);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("palette")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::NodePalette);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("find")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Find);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("log")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Log);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("profiling")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Profiling);
            bPanelFocused = true;
        }
        else if (Panel.Equals(TEXT("viewport")))
        {
            PCGEditor->BringFocusToPanel(EPCGEditorPanel::Viewport1);
            bPanelFocused = true;
        }
    }

    if (EditorType.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("PANEL_UNSUPPORTED"));
        Result->SetStringField(TEXT("message"), TEXT("This asset editor is not supported by editor.focus."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("panel"), Panel);
        return Result;
    }

    if (!bPanelFocused)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("PANEL_UNSUPPORTED"));
        Result->SetStringField(TEXT("message"), TEXT("The requested panel is not supported for this editor."));
        Result->SetStringField(TEXT("assetPath"), AssetPath);
        Result->SetStringField(TEXT("editorType"), EditorType);
        Result->SetStringField(TEXT("panel"), Panel);
        return Result;
    }

    EditorInstance->FocusWindow(Asset);
    const TSharedPtr<FJsonObject> ActiveWindow = BuildActiveWindowJson();

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("assetPath"), AssetPath);
    Result->SetStringField(TEXT("assetName"), Asset->GetName());
    Result->SetStringField(TEXT("assetClassPath"), Asset->GetClass() ? Asset->GetClass()->GetPathName() : TEXT(""));
    Result->SetStringField(TEXT("editorType"), EditorType);
    Result->SetStringField(TEXT("panel"), Panel);
    Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
    Result->SetBoolField(TEXT("panelFocused"), true);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetStringField(TEXT("title")) : TEXT(""));
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildEditorScreenshotToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Target = TEXT("activeWindow");
    FString RequestedPath;
    if (Arguments.IsValid())
    {
        Arguments->TryGetStringField(TEXT("target"), Target);
        Arguments->TryGetStringField(TEXT("path"), RequestedPath);
    }

    Target = Target.TrimStartAndEnd();
    if (Target.IsEmpty())
    {
        Target = TEXT("activeWindow");
    }

    if (!Target.Equals(TEXT("activeWindow"), ESearchCase::IgnoreCase))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.target must be 'activeWindow'."));
        return Result;
    }

    TSharedPtr<SWindow> ActiveWindow = ResolveCaptureTopLevelWindow();

    const FString OutputPath = ResolveScreenshotOutputPath(RequestedPath);
    const FString OutputDir = FPaths::GetPath(OutputPath);
    if (!OutputDir.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*OutputDir, true);
    }

    TArray<FColor> ColorData;
    FIntVector ImageSize(0, 0, 0);

#if PLATFORM_WINDOWS
    if (UObject* ActiveMaterialAsset = FindEditedMaterialAsset())
    {
        RefreshMaterialEditorVisuals(ActiveMaterialAsset);
    }
    if (UObject* ActivePcgAsset = FindEditedPcgAsset())
    {
        RefreshPcgEditorVisuals(ActivePcgAsset);
    }
    if (ActiveWindow.IsValid())
    {
        RefreshSlateWindowForCapture(ActiveWindow.ToSharedRef());
    }

    const HWND CaptureWindowHandle = ResolveCaptureWindowHandle(ActiveWindow);
    if (CaptureWindowHandle == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("WINDOW_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("No native editor window was found for screenshot capture."));
        return Result;
    }

    ::RedrawWindow(CaptureWindowHandle, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    ::UpdateWindow(CaptureWindowHandle);

    FString CaptureError;
    if (!CaptureNativeWindowToColorData(CaptureWindowHandle, ColorData, ImageSize, CaptureError))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), CaptureError.IsEmpty()
            ? TEXT("Failed to capture the active editor window on Windows.")
            : CaptureError);
        return Result;
    }
#else
    if (!ActiveWindow.IsValid())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("WINDOW_NOT_FOUND"));
        Result->SetStringField(TEXT("message"), TEXT("No active top-level editor window was found."));
        return Result;
    }

    if (UObject* ActiveMaterialAsset = FindEditedMaterialAsset())
    {
        RefreshMaterialEditorVisuals(ActiveMaterialAsset);
    }
    if (UObject* ActivePcgAsset = FindEditedPcgAsset())
    {
        RefreshPcgEditorVisuals(ActivePcgAsset);
    }

    RefreshSlateWindowForCapture(ActiveWindow.ToSharedRef());
    if (!FSlateApplication::Get().TakeScreenshot(ActiveWindow.ToSharedRef(), ColorData, ImageSize))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to capture the active editor window."));
        return Result;
    }
#endif

    if (ImageSize.X <= 0 || ImageSize.Y <= 0 || ColorData.IsEmpty())
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("CAPTURE_FAILED"));
        Result->SetStringField(TEXT("message"), TEXT("Screenshot capture returned no pixels."));
        return Result;
    }

    TArray64<uint8> CompressedPng;
    FImageUtils::PNGCompressImageArray(
        ImageSize.X,
        ImageSize.Y,
        TArrayView64<const FColor>(ColorData.GetData(), ColorData.Num()),
        CompressedPng);

    if (CompressedPng.IsEmpty() || !FFileHelper::SaveArrayToFile(CompressedPng, *OutputPath))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("Failed to write screenshot PNG."));
        Result->SetStringField(TEXT("path"), OutputPath);
        return Result;
    }

    Result->SetBoolField(TEXT("isError"), false);
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetStringField(TEXT("target"), TEXT("activeWindow"));
    Result->SetStringField(TEXT("path"), OutputPath);
    Result->SetStringField(TEXT("windowTitle"), ActiveWindow.IsValid() ? ActiveWindow->GetTitle().ToString() : TEXT(""));
    Result->SetNumberField(TEXT("width"), ImageSize.X);
    Result->SetNumberField(TEXT("height"), ImageSize.Y);
    return Result;
}

TSharedPtr<FJsonObject> FLoomleBridgeModule::BuildExecutePythonToolResult(const TSharedPtr<FJsonObject>& Arguments)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString Code;
    if (!Arguments->TryGetStringField(TEXT("code"), Code))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.code is required."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.code is required."));
        return Result;
    }

    FString Mode = TEXT("exec");
    Arguments->TryGetStringField(TEXT("mode"), Mode);
    Mode = Mode.ToLower();
    if (!Mode.Equals(TEXT("exec")) && !Mode.Equals(TEXT("eval")))
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INVALID_ARGUMENT"));
        Result->SetStringField(TEXT("message"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("arguments.mode must be 'exec' or 'eval'."));
        return Result;
    }

    IPythonScriptPlugin* PythonScriptPlugin = IPythonScriptPlugin::Get();
    if (PythonScriptPlugin == nullptr)
    {
        Result->SetBoolField(TEXT("isError"), true);
        Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
        Result->SetStringField(TEXT("message"), TEXT("PythonScriptPlugin module is not loaded."));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("PythonScriptPlugin module is not loaded."));
        return Result;
    }

    if (!PythonScriptPlugin->IsPythonInitialized())
    {
        PythonScriptPlugin->ForceEnablePythonAtRuntime();
        if (!PythonScriptPlugin->IsPythonInitialized())
        {
            Result->SetBoolField(TEXT("isError"), true);
            Result->SetStringField(TEXT("code"), TEXT("INTERNAL_ERROR"));
            Result->SetStringField(TEXT("message"), TEXT("Python runtime is not initialized."));
            AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python runtime is not initialized."));
            return Result;
        }
    }

    static constexpr int32 ExecuteTimeoutSeconds = 30;

    FString WrappedCode;
    if (Mode.Equals(TEXT("exec")))
    {
        WrappedCode += TEXT("import signal, platform\n");
        WrappedCode += FString::Printf(TEXT("_LOOMLE_TIMEOUT = %d\n"), ExecuteTimeoutSeconds);
        WrappedCode += TEXT("def _loomle_timeout_handler(signum, frame):\n");
        WrappedCode += TEXT("    raise TimeoutError(f'execute exceeded {_LOOMLE_TIMEOUT}s timeout')\n");
        WrappedCode += TEXT("if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
        WrappedCode += TEXT("    signal.signal(signal.SIGALRM, _loomle_timeout_handler)\n");
        WrappedCode += TEXT("    signal.alarm(_LOOMLE_TIMEOUT)\n");
        WrappedCode += TEXT("try:\n");
        TArray<FString> CodeLines;
        Code.ParseIntoArrayLines(CodeLines, false);
        for (const FString& Line : CodeLines)
        {
            WrappedCode += TEXT("    ") + Line + TEXT("\n");
        }
        WrappedCode += TEXT("finally:\n");
        WrappedCode += TEXT("    if platform.system() != 'Windows' and hasattr(signal, 'SIGALRM'):\n");
        WrappedCode += TEXT("        signal.alarm(0)\n");
    }

    FPythonCommandEx PythonCommand;
    PythonCommand.ExecutionMode = Mode.Equals(TEXT("eval"))
        ? EPythonCommandExecutionMode::EvaluateStatement
        : EPythonCommandExecutionMode::ExecuteFile;
    PythonCommand.Command = Mode.Equals(TEXT("exec")) ? WrappedCode : Code;

    const bool bSuccess = PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);
    Result->SetBoolField(TEXT("isError"), !bSuccess);
    Result->SetBoolField(TEXT("success"), bSuccess);
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

    TArray<TSharedPtr<FJsonValue>> Logs;
    for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
    {
        TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
        LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
        LogEntry->SetStringField(TEXT("output"), Entry.Output);
        Logs.Add(MakeShared<FJsonValueObject>(LogEntry));
    }
    Result->SetArrayField(TEXT("logs"), Logs);

    if (!bSuccess)
    {
        TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
        Context->SetStringField(TEXT("mode"), Mode);
        Context->SetNumberField(TEXT("logCount"), static_cast<double>(Logs.Num()));
        AppendDiagEvent(TEXT("error"), TEXT("python"), TEXT("execute"), TEXT("Python execute failed."), Context);
    }

    return Result;
}
