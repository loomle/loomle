#include "mcp_core/McpCoreTools.h"

#include "Containers/Array.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* LoomleMcpProtocolVersion = TEXT("2025-11-25");
constexpr const TCHAR* LoomleMcpServerVersion = TEXT("0.4.0-dev");
constexpr const TCHAR* JsonSchemaDraft2020 = TEXT("https://json-schema.org/draft/2020-12/schema");

TSharedPtr<FJsonObject> MakeObjectSchema(bool bAdditionalProperties)
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("$schema"), JsonSchemaDraft2020);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
    Schema->SetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
    return Schema;
}

TSharedPtr<FJsonObject> MakeArraySchema(const TSharedPtr<FJsonObject>& Items)
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("type"), TEXT("array"));
    Schema->SetObjectField(TEXT("items"), Items);
    return Schema;
}

TSharedPtr<FJsonObject> MakeStringSchema(const FString& Description = FString(), int32 MinLength = 0)
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("type"), TEXT("string"));
    if (MinLength > 0)
    {
        Schema->SetNumberField(TEXT("minLength"), MinLength);
    }
    if (!Description.IsEmpty())
    {
        Schema->SetStringField(TEXT("description"), Description);
    }
    return Schema;
}

TSharedPtr<FJsonObject> MakeBooleanSchema(bool bDefaultValue, bool bIncludeDefault = true, const FString& Description = FString())
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("type"), TEXT("boolean"));
    if (bIncludeDefault)
    {
        Schema->SetBoolField(TEXT("default"), bDefaultValue);
    }
    if (!Description.IsEmpty())
    {
        Schema->SetStringField(TEXT("description"), Description);
    }
    return Schema;
}

TSharedPtr<FJsonObject> MakeIntegerSchema(
    const TOptional<int32>& MinValue = TOptional<int32>(),
    const TOptional<int32>& MaxValue = TOptional<int32>(),
    const TOptional<int32>& DefaultValue = TOptional<int32>(),
    const FString& Description = FString())
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("type"), TEXT("integer"));
    if (MinValue.IsSet())
    {
        Schema->SetNumberField(TEXT("minimum"), MinValue.GetValue());
    }
    if (MaxValue.IsSet())
    {
        Schema->SetNumberField(TEXT("maximum"), MaxValue.GetValue());
    }
    if (DefaultValue.IsSet())
    {
        Schema->SetNumberField(TEXT("default"), DefaultValue.GetValue());
    }
    if (!Description.IsEmpty())
    {
        Schema->SetStringField(TEXT("description"), Description);
    }
    return Schema;
}

TSharedPtr<FJsonObject> MakeEnumStringSchema(
    const TArray<FString>& Values,
    const FString& DefaultValue = FString(),
    const FString& Description = FString())
{
    TSharedPtr<FJsonObject> Schema = MakeStringSchema(Description);
    TArray<TSharedPtr<FJsonValue>> EnumValues;
    for (const FString& Value : Values)
    {
        EnumValues.Add(MakeShared<FJsonValueString>(Value));
    }
    Schema->SetArrayField(TEXT("enum"), EnumValues);
    if (!DefaultValue.IsEmpty())
    {
        Schema->SetStringField(TEXT("default"), DefaultValue);
    }
    return Schema;
}

TSharedPtr<FJsonObject> MakeOpenOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("$schema"), JsonSchemaDraft2020);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    return Schema;
}

void AddRequiredFields(TSharedPtr<FJsonObject>& Schema, std::initializer_list<const TCHAR*> Names)
{
    TArray<TSharedPtr<FJsonValue>> Required;
    for (const TCHAR* Name : Names)
    {
        Required.Add(MakeShared<FJsonValueString>(Name));
    }
    Schema->SetArrayField(TEXT("required"), Required);
}

TSharedPtr<FJsonObject> MakeExecutionSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("mode"), MakeEnumStringSchema({TEXT("sync"), TEXT("job")}, TEXT("sync")));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("label"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("waitMs"), MakeIntegerSchema(1));
    Properties->SetObjectField(TEXT("resultTtlMs"), MakeIntegerSchema(1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphTypeSchema()
{
    return MakeEnumStringSchema({TEXT("blueprint"), TEXT("material"), TEXT("pcg")}, TEXT("blueprint"), TEXT("Graph domain."));
}

TSharedPtr<FJsonObject> MakeGraphRefSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    Schema->SetStringField(TEXT("description"), TEXT("Self-contained subgraph locator emitted by graph.list and graph.query. Pass back verbatim — do not construct manually."));
    AddRequiredFields(Schema, {TEXT("kind")});

    TSharedPtr<FJsonObject> InlineVariant = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> InlineProperties = InlineVariant->GetObjectField(TEXT("properties"));
    InlineProperties->SetObjectField(TEXT("kind"), MakeEnumStringSchema({TEXT("inline")}));
    InlineProperties->SetObjectField(TEXT("nodeGuid"), MakeStringSchema(TEXT("FGuid of the composite/subgraph node that owns this subgraph."), 1));
    InlineProperties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Asset that contains the node (embedded for self-containment)."), 1));
    AddRequiredFields(InlineVariant, {TEXT("kind"), TEXT("nodeGuid"), TEXT("assetPath")});

    TSharedPtr<FJsonObject> AssetVariant = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> AssetProperties = AssetVariant->GetObjectField(TEXT("properties"));
    AssetProperties->SetObjectField(TEXT("kind"), MakeEnumStringSchema({TEXT("asset")}));
    AssetProperties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path of the graph asset."), 1));
    AssetProperties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Graph name within the asset. Required for multi-graph assets such as Blueprint; omit for single-graph assets (Material, PCG)."), 1));
    AddRequiredFields(AssetVariant, {TEXT("kind"), TEXT("assetPath")});

    TArray<TSharedPtr<FJsonValue>> Variants;
    Variants.Add(MakeShared<FJsonValueObject>(InlineVariant));
    Variants.Add(MakeShared<FJsonValueObject>(AssetVariant));
    Schema->SetArrayField(TEXT("oneOf"), Variants);
    return Schema;
}

TSharedPtr<FJsonObject> MakeLoomleInputSchema()
{
    return MakeObjectSchema(false);
}

TSharedPtr<FJsonObject> MakeContextInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("resolveIds"), MakeArraySchema(MakeStringSchema()));
    Properties->SetObjectField(TEXT("resolveFields"), MakeArraySchema(MakeStringSchema()));
    return Schema;
}

TSharedPtr<FJsonObject> MakeExecuteInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("code")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("language"), MakeStringSchema());
    Properties->GetObjectField(TEXT("language"))->SetStringField(TEXT("default"), TEXT("python"));
    Properties->SetObjectField(TEXT("mode"), MakeEnumStringSchema({TEXT("exec"), TEXT("eval")}, TEXT("exec")));
    Properties->SetObjectField(TEXT("code"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("execution"), MakeExecutionSchema());
    Properties->SetObjectField(
        TEXT("timeoutMs"),
        MakeIntegerSchema(
            1,
            TOptional<int32>(),
            120000,
            TEXT("Optional end-to-end timeout budget in milliseconds for long-running editor work. Defaults to 120000.")));
    return Schema;
}

TSharedPtr<FJsonObject> MakeJobsInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("action")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("action"), MakeEnumStringSchema({TEXT("status"), TEXT("result"), TEXT("logs"), TEXT("list")}));
    Properties->SetObjectField(TEXT("jobId"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("cursor"), MakeStringSchema());
    Properties->SetObjectField(TEXT("limit"), MakeIntegerSchema(1, 1000));
    Properties->SetObjectField(TEXT("status"), MakeEnumStringSchema({TEXT("queued"), TEXT("running"), TEXT("succeeded"), TEXT("failed")}));
    Properties->SetObjectField(TEXT("tool"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("sessionId"), MakeStringSchema(FString(), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeProfilingInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("action")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("action"), MakeEnumStringSchema({TEXT("unit"), TEXT("game"), TEXT("gpu"), TEXT("ticks"), TEXT("memory"), TEXT("capture")}));
    Properties->SetObjectField(TEXT("world"), MakeEnumStringSchema({TEXT("active"), TEXT("editor"), TEXT("pie")}, TEXT("active")));
    Properties->SetObjectField(TEXT("gpuIndex"), MakeIntegerSchema(0, TOptional<int32>(), 0));
    Properties->SetObjectField(TEXT("includeRaw"), MakeBooleanSchema(true));
    Properties->SetObjectField(TEXT("includeGpuUtilization"), MakeBooleanSchema(true));
    Properties->SetObjectField(TEXT("includeHistory"), MakeBooleanSchema(false, true, TEXT("Expose official non-shipping FStatUnitData history arrays when available.")));
    Properties->SetObjectField(TEXT("group"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("displayMode"), MakeEnumStringSchema({TEXT("flat"), TEXT("hierarchical"), TEXT("both")}));
    Properties->SetObjectField(TEXT("includeThreadBreakdown"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("sortBy"), MakeEnumStringSchema({TEXT("sum"), TEXT("call_count"), TEXT("name")}));
    Properties->SetObjectField(TEXT("maxDepth"), MakeIntegerSchema(1, 32));
    Properties->SetObjectField(TEXT("mode"), MakeEnumStringSchema({TEXT("all"), TEXT("grouped"), TEXT("enabled"), TEXT("disabled")}));
    Properties->SetObjectField(TEXT("profile"), MakeStringSchema());
    Properties->SetObjectField(TEXT("log"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("csv"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("kind"), MakeStringSchema());
    Properties->SetObjectField(TEXT("execution"), MakeExecutionSchema());
    return Schema;
}

TSharedPtr<FJsonObject> MakeEditorOpenInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    Schema->GetObjectField(TEXT("properties"))->SetObjectField(
        TEXT("assetPath"),
        MakeStringSchema(TEXT("Unreal asset path, for example /Game/MyFolder/MyAsset."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeEditorFocusInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("panel")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path whose editor should be focused."), 1));
    Properties->SetObjectField(
        TEXT("panel"),
        MakeEnumStringSchema(
            {TEXT("graph"), TEXT("viewport"), TEXT("details"), TEXT("palette"), TEXT("find"), TEXT("preview"), TEXT("log"), TEXT("profiling"), TEXT("constructionScript"), TEXT("myBlueprint")},
            FString(),
            TEXT("Semantic editor panel name. Supported values vary by editor type.")));
    return Schema;
}

TSharedPtr<FJsonObject> MakeEditorScreenshotInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(
        TEXT("target"),
        MakeEnumStringSchema(
            {TEXT("activeWindow")},
            TEXT("activeWindow"),
            TEXT("Screenshot target. The first release supports only the active top-level editor window.")));
    Properties->SetObjectField(
        TEXT("path"),
        MakeStringSchema(TEXT("Optional output path. Relative paths resolve from the Unreal project root; .png is appended when omitted.")));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphDescriptorInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    Schema->GetObjectField(TEXT("properties"))->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphListInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path, for example /Game/MyFolder/MyAsset."), 1));
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("includeSubgraphs"), MakeBooleanSchema(false, true, TEXT("When true, recursively enumerate subgraphs owned by composite/subgraph nodes.")));
    Properties->SetObjectField(TEXT("maxDepth"), MakeIntegerSchema(0, 8, 1, TEXT("Maximum recursion depth when includeSubgraphs is true. 0 disables recursion; 1 returns direct children only.")));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphQueryInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path (Mode A). Required when graphName is used; omit when graphRef is provided."), 1));
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Graph name within the asset (Mode A), for example EventGraph. Mutually exclusive with graphRef."), 1));
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("layoutDetail"), MakeEnumStringSchema({TEXT("basic"), TEXT("measured")}, TEXT("basic"), TEXT("Requested layout detail level. `basic` returns lightweight geometry; `measured` asks the runtime to provide richer layout data when supported.")));

    TSharedPtr<FJsonObject> FilterSchema = MakeObjectSchema(false);
    FilterSchema->SetStringField(TEXT("description"), TEXT("Optional filters to narrow returned nodes."));
    TSharedPtr<FJsonObject> FilterProperties = FilterSchema->GetObjectField(TEXT("properties"));
    FilterProperties->SetObjectField(TEXT("nodeClasses"), MakeArraySchema(MakeStringSchema()));
    FilterProperties->SetObjectField(TEXT("nodeIds"), MakeArraySchema(MakeStringSchema()));
    FilterProperties->SetObjectField(TEXT("text"), MakeStringSchema(TEXT("Fuzzy text search across node titles and comments.")));
    Properties->SetObjectField(TEXT("filter"), FilterSchema);
    Properties->SetObjectField(TEXT("limit"), MakeIntegerSchema(1, 1000, TOptional<int32>(), TEXT("Maximum number of nodes/edges to return when truncation is supported.")));
    Properties->SetObjectField(TEXT("cursor"), MakeStringSchema(TEXT("Opaque pagination cursor returned by a prior graph.query response. Supply it together with the same graph address and filters to continue a truncated read.")));
    TSharedPtr<FJsonObject> PathSchema = MakeArraySchema(MakeStringSchema(FString(), 1));
    PathSchema->SetNumberField(TEXT("minItems"), 1);
    PathSchema->SetNumberField(TEXT("maxItems"), 8);
    PathSchema->SetStringField(TEXT("description"), TEXT("Blueprint only. Ordered list of composite node GUIDs to traverse into before querying. Each entry must be a K2Node_Composite nodeId. The server resolves the subgraph of the final GUID in a single round-trip, avoiding multiple graph.query calls for deeply nested composites. Mutually exclusive with graphRef.kind=inline at the same level — supply path instead."));
    Properties->SetObjectField(TEXT("path"), PathSchema);
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphResolveInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("path"), MakeStringSchema(TEXT("Generic Unreal object path, including values emitted by context.selection.items[*].path."), 1));
    Properties->SetObjectField(TEXT("objectPath"), MakeStringSchema(TEXT("Explicit Unreal object path."), 1));
    Properties->SetObjectField(TEXT("actorPath"), MakeStringSchema(TEXT("Actor object path."), 1));
    Properties->SetObjectField(TEXT("componentPath"), MakeStringSchema(TEXT("Actor component object path."), 1));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path."), 1));
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());

    TArray<TSharedPtr<FJsonValue>> AnyOfValues;
    for (const FString& Field : {TEXT("path"), TEXT("objectPath"), TEXT("actorPath"), TEXT("componentPath"), TEXT("assetPath")})
    {
        TSharedPtr<FJsonObject> Requirement = MakeShared<FJsonObject>();
        Requirement->SetArrayField(TEXT("required"), {MakeShared<FJsonValueString>(Field)});
        AnyOfValues.Add(MakeShared<FJsonValueObject>(Requirement));
    }
    Schema->SetArrayField(TEXT("anyOf"), AnyOfValues);
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphMutateInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("ops")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path (Mode A). Required when graphName is used; omit when graphRef is provided."), 1));
    TSharedPtr<FJsonObject> GraphName = MakeStringSchema(TEXT("Target graph name (Mode A). Defaults to EventGraph when omitted. Mutually exclusive with graphRef."), 1);
    GraphName->SetStringField(TEXT("default"), TEXT("EventGraph"));
    Properties->SetObjectField(TEXT("graphName"), GraphName);
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("expectedRevision"), MakeStringSchema(TEXT("Optional optimistic concurrency token from a prior graph read.")));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(TEXT("Optional client-supplied idempotency token.")));
    Properties->SetObjectField(TEXT("dryRun"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("continueOnError"), MakeBooleanSchema(false));

    TSharedPtr<FJsonObject> ExecutionPolicy = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> ExecutionPolicyProperties = ExecutionPolicy->GetObjectField(TEXT("properties"));
    ExecutionPolicyProperties->SetObjectField(TEXT("stopOnError"), MakeBooleanSchema(true));
    ExecutionPolicyProperties->SetObjectField(TEXT("maxOps"), MakeIntegerSchema(1, 200));
    Properties->SetObjectField(TEXT("executionPolicy"), ExecutionPolicy);

    TSharedPtr<FJsonObject> OpSchema = MakeObjectSchema(false);
    AddRequiredFields(OpSchema, {TEXT("op"), TEXT("args")});
    TSharedPtr<FJsonObject> OpProperties = OpSchema->GetObjectField(TEXT("properties"));
    OpProperties->SetObjectField(TEXT("op"), MakeStringSchema(TEXT("Graph mutate op id. Discover the graphType-specific supported ops from the graph tool response; runScript is currently blueprint-only.")));
    OpProperties->SetObjectField(TEXT("clientRef"), MakeStringSchema(TEXT("Optional client-side reference name for later ops in the same request.")));
    OpProperties->SetObjectField(TEXT("targetGraphName"), MakeStringSchema(TEXT("Optional per-op graph override by graph name. Mutually exclusive with targetGraphRef (or args.graphRef)."), 1));
    OpProperties->SetObjectField(TEXT("targetGraphRef"), MakeGraphRefSchema());
    TSharedPtr<FJsonObject> ArgsSchema = MakeObjectSchema(true);
    ArgsSchema->SetStringField(TEXT("description"), TEXT("Operation-specific arguments. Required keys depend on op. For mutate graph-addressing, args.graphRef is also accepted as an alias of targetGraphRef."));
    OpProperties->SetObjectField(TEXT("args"), ArgsSchema);

    TSharedPtr<FJsonObject> OpsSchema = MakeArraySchema(OpSchema);
    OpsSchema->SetNumberField(TEXT("minItems"), 1);
    OpsSchema->SetNumberField(TEXT("maxItems"), 200);
    Properties->SetObjectField(TEXT("ops"), OpsSchema);
    return Schema;
}

TSharedPtr<FJsonObject> MakeDiagTailInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("fromSeq"), MakeIntegerSchema(0, TOptional<int32>(), TOptional<int32>(), TEXT("Return events with seq > fromSeq. Defaults to 0.")));
    Properties->SetObjectField(TEXT("limit"), MakeIntegerSchema(1, 1000, 200, TEXT("Maximum number of events to return.")));

    TSharedPtr<FJsonObject> FiltersSchema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> FiltersProperties = FiltersSchema->GetObjectField(TEXT("properties"));
    FiltersProperties->SetObjectField(TEXT("severity"), MakeEnumStringSchema({TEXT("error"), TEXT("warning"), TEXT("info")}));
    FiltersProperties->SetObjectField(TEXT("category"), MakeStringSchema(FString(), 1));
    FiltersProperties->SetObjectField(TEXT("source"), MakeStringSchema(FString(), 1));
    FiltersProperties->SetObjectField(TEXT("assetPathPrefix"), MakeStringSchema(FString(), 1));
    Properties->SetObjectField(TEXT("filters"), FiltersSchema);
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphVerifyInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("graphType")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("graphType"), MakeEnumStringSchema({TEXT("blueprint"), TEXT("material"), TEXT("pcg")}, FString(), TEXT("Graph domain for final graph-level verification.")));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Unreal asset path for graph-level verification."), 1));
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Graph name within the asset when verifying a specific Blueprint graph."), 1));
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphListOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("graphType"), TEXT("assetPath"), TEXT("graphs"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphs"), MakeArraySchema(MakeObjectSchema(true)));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphQueryOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("graphType"), TEXT("assetPath"), TEXT("graphName"), TEXT("graphRef"), TEXT("semanticSnapshot"), TEXT("meta"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    Properties->SetObjectField(TEXT("revision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("semanticSnapshot"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("nextCursor"), MakeStringSchema());
    Properties->SetObjectField(TEXT("meta"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphMutateOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("applied"), TEXT("partialApplied"), TEXT("graphType"), TEXT("assetPath"), TEXT("graphName"), TEXT("graphRef"), TEXT("opResults"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("applied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("partialApplied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    Properties->SetObjectField(TEXT("previousRevision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("newRevision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("code"), MakeStringSchema());
    Properties->SetObjectField(TEXT("message"), MakeStringSchema());

    TSharedPtr<FJsonObject> OpResultSchema = MakeObjectSchema(true);
    AddRequiredFields(OpResultSchema, {TEXT("index"), TEXT("op"), TEXT("ok"), TEXT("changed"), TEXT("errorCode"), TEXT("errorMessage")});
    TSharedPtr<FJsonObject> OpResultProperties = OpResultSchema->GetObjectField(TEXT("properties"));
    OpResultProperties->SetObjectField(TEXT("index"), MakeIntegerSchema());
    OpResultProperties->SetObjectField(TEXT("op"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("ok"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("skipped"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("changed"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("nodeId"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("error"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("errorCode"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("errorMessage"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("skipReason"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("details"), MakeObjectSchema(true));
    OpResultProperties->SetObjectField(TEXT("movedNodeIds"), MakeArraySchema(MakeStringSchema()));
    OpResultProperties->SetObjectField(TEXT("scriptResult"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("opResults"), MakeArraySchema(OpResultSchema));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeDiagTailOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("items"), TEXT("nextSeq"), TEXT("hasMore"), TEXT("highWatermark")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("items"), MakeArraySchema(MakeObjectSchema(true)));
    Properties->SetObjectField(TEXT("nextSeq"), MakeIntegerSchema(0));
    Properties->SetObjectField(TEXT("hasMore"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("highWatermark"), MakeIntegerSchema(0));
    return Schema;
}

TSharedPtr<FJsonObject> MakeGraphVerifyOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("status"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("status"), MakeEnumStringSchema({TEXT("ok"), TEXT("warn"), TEXT("error")}));
    Properties->SetObjectField(TEXT("summary"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphType"), MakeGraphTypeSchema());
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphRef"), MakeGraphRefSchema());
    Properties->SetObjectField(TEXT("previousRevision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("newRevision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("queryReport"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("compileReport"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

struct FToolDescriptorDefinition
{
    const TCHAR* Name;
    const TCHAR* Title;
    const TCHAR* Description;
    TSharedPtr<FJsonObject> (*BuildInputSchema)();
    TSharedPtr<FJsonObject> (*BuildOutputSchema)();
};

const TArray<FToolDescriptorDefinition>& GetToolDefinitions()
{
    static const TArray<FToolDescriptorDefinition> Definitions{
        {TEXT("loomle"), TEXT("Loomle Status"), TEXT("Bridge health and runtime status."), &MakeLoomleInputSchema, &MakeOpenOutputSchema},
        {TEXT("context"), TEXT("Editor Context"), TEXT("Read active editor context and selection."), &MakeContextInputSchema, &MakeOpenOutputSchema},
        {TEXT("execute"), TEXT("Execute Script"), TEXT("Execute Unreal-side Python inside the editor process."), &MakeExecuteInputSchema, &MakeOpenOutputSchema},
        {TEXT("jobs"), TEXT("Jobs"), TEXT("Inspect or retrieve long-running job state, results, and logs."), &MakeJobsInputSchema, &MakeOpenOutputSchema},
        {TEXT("profiling"), TEXT("Profiling"), TEXT("Bridge official Unreal profiling data families such as stat unit, stat groups, ticks, memory reports, and capture workflows."), &MakeProfilingInputSchema, &MakeOpenOutputSchema},
        {TEXT("editor.open"), TEXT("Open Asset Editor"), TEXT("Open or focus the editor for a specific Unreal asset path."), &MakeEditorOpenInputSchema, &MakeOpenOutputSchema},
        {TEXT("editor.focus"), TEXT("Focus Editor Panel"), TEXT("Focus a semantic panel inside an asset editor, such as graph, viewport, details, palette, or find."), &MakeEditorFocusInputSchema, &MakeOpenOutputSchema},
        {TEXT("editor.screenshot"), TEXT("Editor Screenshot"), TEXT("Capture a PNG of the active editor window and return the written file path."), &MakeEditorScreenshotInputSchema, &MakeOpenOutputSchema},
        {TEXT("graph.verify"), TEXT("Graph Verify"), TEXT("Run final graph verification for Blueprint, Material, or PCG graphs."), &MakeGraphVerifyInputSchema, &MakeGraphVerifyOutputSchema},
        {TEXT("graph"), TEXT("Graph Descriptor"), TEXT("Read graph capability descriptor and runtime status."), &MakeGraphDescriptorInputSchema, &MakeOpenOutputSchema},
        {TEXT("graph.list"), TEXT("Graph List"), TEXT("List readable graphs in an asset."), &MakeGraphListInputSchema, &MakeGraphListOutputSchema},
        {TEXT("graph.resolve"), TEXT("Graph Resolve"), TEXT("Resolve an Unreal object or asset reference into queryable graph refs."), &MakeGraphResolveInputSchema, &MakeOpenOutputSchema},
        {TEXT("graph.query"), TEXT("Graph Query"), TEXT("Query semantic graph snapshot."), &MakeGraphQueryInputSchema, &MakeGraphQueryOutputSchema},
        {TEXT("graph.mutate"), TEXT("Graph Mutate"), TEXT("Apply graph write operations in order."), &MakeGraphMutateInputSchema, &MakeGraphMutateOutputSchema},
        {TEXT("diag.tail"), TEXT("Diagnostics Tail"), TEXT("Read persisted diagnostics incrementally by sequence cursor."), &MakeDiagTailInputSchema, &MakeDiagTailOutputSchema},
    };
    return Definitions;
}

TSharedPtr<FJsonObject> MakeToolDescriptorObject(const FToolDescriptorDefinition& Definition)
{
    TSharedPtr<FJsonObject> Descriptor = MakeShared<FJsonObject>();
    Descriptor->SetStringField(TEXT("name"), Definition.Name);
    Descriptor->SetStringField(TEXT("title"), Definition.Title);
    Descriptor->SetStringField(TEXT("description"), Definition.Description);
    Descriptor->SetObjectField(TEXT("inputSchema"), Definition.BuildInputSchema());
    Descriptor->SetObjectField(TEXT("outputSchema"), Definition.BuildOutputSchema());
    return Descriptor;
}
}

namespace Loomle::McpCore
{
TSharedPtr<FJsonObject> BuildInitializeResult(const TSharedPtr<FJsonObject>& Params)
{
    FString ProtocolVersion = LoomleMcpProtocolVersion;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("protocolVersion"), ProtocolVersion);
    }

    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());

    TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), TEXT("LOOMLE"));
    ServerInfo->SetStringField(TEXT("version"), LoomleMcpServerVersion);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), ProtocolVersion);
    Result->SetObjectField(TEXT("capabilities"), Capabilities);
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
    Result->SetStringField(TEXT("instructions"), TEXT("Use tools/list for runtime schema discovery. Use tools/call for all LOOMLE runtime tools."));
    return Result;
}

TSharedPtr<FJsonObject> BuildToolsListResult()
{
    TArray<TSharedPtr<FJsonValue>> Tools;
    for (const FToolDescriptorDefinition& Definition : GetToolDefinitions())
    {
        Tools.Add(MakeShared<FJsonValueObject>(MakeToolDescriptorObject(Definition)));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tools"), Tools);
    return Result;
}

bool IsKnownTool(const FString& Name)
{
    for (const FToolDescriptorDefinition& Definition : GetToolDefinitions())
    {
        if (Name.Equals(Definition.Name, ESearchCase::CaseSensitive))
        {
            return true;
        }
    }
    return false;
}
}
