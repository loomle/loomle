#include "mcp_core/McpCoreTools.h"

#include "Containers/Array.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
constexpr const TCHAR* LoomleMcpProtocolVersion = TEXT("2025-11-25");
constexpr const TCHAR* LoomleMcpServerVersion = TEXT("0.5.1");
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

TSharedPtr<FJsonObject> MakeAssetPathOnlySchema(const FString& Description)
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(Description, 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeNodeIdArraySchema()
{
    return MakeArraySchema(MakeStringSchema(FString(), 1));
}

TSharedPtr<FJsonObject> MakeNodeClassArraySchema()
{
    return MakeArraySchema(MakeStringSchema());
}

TSharedPtr<FJsonObject> MakeBlueprintListInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Blueprint asset path, for example /Game/BP/MyBlueprint."), 1));
    Properties->SetObjectField(TEXT("includeCompositeSubgraphs"), MakeBooleanSchema(false));
    return Schema;
}

TSharedPtr<FJsonObject> MakeBlueprintQueryInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Blueprint asset path, for example /Game/BP/MyBlueprint."), 1));
    TSharedPtr<FJsonObject> GraphName = MakeStringSchema(TEXT("Blueprint graph name. Defaults to EventGraph."), 1);
    GraphName->SetStringField(TEXT("default"), TEXT("EventGraph"));
    Properties->SetObjectField(TEXT("graphName"), GraphName);
    Properties->SetObjectField(TEXT("nodeIds"), MakeNodeIdArraySchema());
    Properties->SetObjectField(TEXT("nodeClasses"), MakeNodeClassArraySchema());
    Properties->SetObjectField(TEXT("includeComments"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("includePinDefaults"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("includeConnections"), MakeBooleanSchema(false));
    return Schema;
}

TSharedPtr<FJsonObject> MakeMutateOpSchema(const TArray<FString>& OpNames, bool bIncludeGraphTargets)
{
    TSharedPtr<FJsonObject> OpSchema = MakeObjectSchema(false);
    AddRequiredFields(OpSchema, {TEXT("op")});
    TSharedPtr<FJsonObject> OpProperties = OpSchema->GetObjectField(TEXT("properties"));
    OpProperties->SetObjectField(TEXT("op"), MakeEnumStringSchema(OpNames));
    OpProperties->SetObjectField(TEXT("clientRef"), MakeStringSchema(TEXT("Optional intra-request alias for a created node."), 1));
    if (bIncludeGraphTargets)
    {
        OpProperties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Optional graph target for graph management operations."), 1));
        OpProperties->SetObjectField(TEXT("newName"), MakeStringSchema(TEXT("Optional new graph name for rename operations."), 1));
    }
    OpProperties->SetObjectField(TEXT("nodeId"), MakeStringSchema(TEXT("Node GUID."), 1));
    OpProperties->SetObjectField(TEXT("nodeRef"), MakeStringSchema(TEXT("clientRef alias from an earlier op in the same request."), 1));
    OpProperties->SetObjectField(TEXT("nodePath"), MakeStringSchema(TEXT("Domain-specific graph-qualified node path."), 1));
    OpProperties->SetObjectField(TEXT("nodeName"), MakeStringSchema(TEXT("Display name fallback for node resolution."), 1));
    OpProperties->SetObjectField(TEXT("nodeClass"), MakeStringSchema(TEXT("Node class path."), 1));
    OpProperties->SetObjectField(TEXT("functionClass"), MakeStringSchema(TEXT("Owning class path for function call creation."), 1));
    OpProperties->SetObjectField(TEXT("functionName"), MakeStringSchema(TEXT("Function name for call-function node creation."), 1));
    OpProperties->SetObjectField(TEXT("eventName"), MakeStringSchema(TEXT("Event name for event-node creation."), 1));
    OpProperties->SetObjectField(TEXT("eventClass"), MakeStringSchema(TEXT("Optional event class path."), 1));
    OpProperties->SetObjectField(TEXT("variableName"), MakeStringSchema(TEXT("Variable name for variable node creation."), 1));
    OpProperties->SetObjectField(TEXT("variableClass"), MakeStringSchema(TEXT("Optional variable owner class path."), 1));
    OpProperties->SetObjectField(TEXT("mode"), MakeEnumStringSchema({TEXT("get"), TEXT("set"), TEXT("exec"), TEXT("eval"), TEXT("sync"), TEXT("job")}));
    OpProperties->SetObjectField(TEXT("macroLibrary"), MakeStringSchema(TEXT("Macro library asset path."), 1));
    OpProperties->SetObjectField(TEXT("macroName"), MakeStringSchema(TEXT("Macro graph name."), 1));
    OpProperties->SetObjectField(TEXT("targetClass"), MakeStringSchema(TEXT("Target class path."), 1));
    OpProperties->SetObjectField(TEXT("text"), MakeStringSchema(TEXT("Comment text."), 1));
    OpProperties->SetObjectField(TEXT("comment"), MakeStringSchema(TEXT("Node comment text."), 1));
    OpProperties->SetObjectField(TEXT("enabled"), MakeBooleanSchema(true, false));
    OpProperties->SetObjectField(TEXT("width"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("height"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("x"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("y"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("dx"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("dy"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("pinName"), MakeStringSchema(TEXT("Pin name."), 1));
    OpProperties->SetObjectField(TEXT("fromPin"), MakeStringSchema(TEXT("Source pin name."), 1));
    OpProperties->SetObjectField(TEXT("toPin"), MakeStringSchema(TEXT("Target pin name."), 1));
    OpProperties->SetObjectField(TEXT("fromNodeId"), MakeStringSchema(FString(), 1));
    OpProperties->SetObjectField(TEXT("fromNodeRef"), MakeStringSchema(FString(), 1));
    OpProperties->SetObjectField(TEXT("toNodeId"), MakeStringSchema(FString(), 1));
    OpProperties->SetObjectField(TEXT("toNodeRef"), MakeStringSchema(FString(), 1));
    OpProperties->SetObjectField(TEXT("value"), MakeStringSchema(TEXT("Serialized value payload."), 1));
    OpProperties->SetObjectField(TEXT("property"), MakeStringSchema(TEXT("Editable property name."), 1));
    OpProperties->SetObjectField(TEXT("algorithm"), MakeStringSchema(TEXT("Layout algorithm."), 1));

    TSharedPtr<FJsonObject> MoveNodesItemSchema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> MoveNodesItemProps = MoveNodesItemSchema->GetObjectField(TEXT("properties"));
    MoveNodesItemProps->SetObjectField(TEXT("nodeId"), MakeStringSchema(FString(), 1));
    MoveNodesItemProps->SetObjectField(TEXT("dx"), MakeIntegerSchema());
    MoveNodesItemProps->SetObjectField(TEXT("dy"), MakeIntegerSchema());
    OpProperties->SetObjectField(TEXT("nodes"), MakeArraySchema(MoveNodesItemSchema));

    TSharedPtr<FJsonObject> OpsSchema = MakeArraySchema(OpSchema);
    OpsSchema->SetNumberField(TEXT("minItems"), 1);
    OpsSchema->SetNumberField(TEXT("maxItems"), 200);
    return OpsSchema;
}

TSharedPtr<FJsonObject> MakeBlueprintMutateInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("ops")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Blueprint asset path, for example /Game/BP/MyBlueprint."), 1));
    TSharedPtr<FJsonObject> GraphName = MakeStringSchema(TEXT("Blueprint graph name. Defaults to EventGraph."), 1);
    GraphName->SetStringField(TEXT("default"), TEXT("EventGraph"));
    Properties->SetObjectField(TEXT("graphName"), GraphName);
    Properties->SetObjectField(TEXT("expectedRevision"), MakeStringSchema(TEXT("Optional optimistic concurrency token."), 1));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(TEXT("Optional idempotency token."), 1));
    Properties->SetObjectField(TEXT("dryRun"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("continueOnError"), MakeBooleanSchema(false));
    Properties->SetObjectField(
        TEXT("ops"),
        MakeMutateOpSchema(
            {
                TEXT("addNode.byClass"),
                TEXT("addNode.byFunction"),
                TEXT("addNode.byEvent"),
                TEXT("addNode.byVariable"),
                TEXT("addNode.byMacro"),
                TEXT("addNode.branch"),
                TEXT("addNode.sequence"),
                TEXT("addNode.cast"),
                TEXT("addNode.comment"),
                TEXT("addNode.knot"),
                TEXT("duplicateNode"),
                TEXT("removeNode"),
                TEXT("moveNode"),
                TEXT("moveNodeBy"),
                TEXT("moveNodes"),
                TEXT("connectPins"),
                TEXT("disconnectPins"),
                TEXT("breakPinLinks"),
                TEXT("setPinDefault"),
                TEXT("setNodeComment"),
                TEXT("setNodeEnabled"),
                TEXT("addGraph"),
                TEXT("renameGraph"),
                TEXT("deleteGraph"),
                TEXT("layoutGraph"),
                TEXT("compile")
            },
            true));
    return Schema;
}

TSharedPtr<FJsonObject> MakeBlueprintVerifyInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Blueprint asset path."), 1));
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Optional Blueprint graph name."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeBlueprintDescribeInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Blueprint asset path."), 1));
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema(TEXT("Provide with nodeId to enter instance mode."), 1));
    Properties->SetObjectField(TEXT("nodeId"), MakeStringSchema(TEXT("Node GUID for instance mode."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeMaterialListInputSchema()
{
    return MakeAssetPathOnlySchema(TEXT("Material asset path, for example /Game/M_Material."));
}

TSharedPtr<FJsonObject> MakeMaterialQueryInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Material asset path."), 1));
    Properties->SetObjectField(TEXT("nodeIds"), MakeNodeIdArraySchema());
    Properties->SetObjectField(TEXT("nodeClasses"), MakeNodeClassArraySchema());
    Properties->SetObjectField(TEXT("includeConnections"), MakeBooleanSchema(false));
    return Schema;
}

TSharedPtr<FJsonObject> MakeMaterialMutateInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("ops")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Material asset path."), 1));
    Properties->SetObjectField(TEXT("expectedRevision"), MakeStringSchema(TEXT("Optional optimistic concurrency token."), 1));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(TEXT("Optional idempotency token."), 1));
    Properties->SetObjectField(TEXT("dryRun"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("continueOnError"), MakeBooleanSchema(false));
    Properties->SetObjectField(
        TEXT("ops"),
        MakeMutateOpSchema(
            {
                TEXT("addNode.byClass"),
                TEXT("removeNode"),
                TEXT("moveNode"),
                TEXT("moveNodeBy"),
                TEXT("moveNodes"),
                TEXT("connectPins"),
                TEXT("disconnectPins"),
                TEXT("setProperty")
            },
            false));
    return Schema;
}

TSharedPtr<FJsonObject> MakeMaterialVerifyInputSchema()
{
    return MakeAssetPathOnlySchema(TEXT("Material asset path."));
}

TSharedPtr<FJsonObject> MakeMaterialDescribeInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("Material asset path for instance mode."), 1));
    Properties->SetObjectField(TEXT("nodeId"), MakeStringSchema(TEXT("Node GUID for instance mode."), 1));
    Properties->SetObjectField(TEXT("nodeClass"), MakeStringSchema(TEXT("Material expression class for class mode."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakePcgListInputSchema()
{
    return MakeAssetPathOnlySchema(TEXT("PCG graph asset path, for example /Game/PCG/MyGraph."));
}

TSharedPtr<FJsonObject> MakePcgQueryInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("PCG graph asset path."), 1));
    Properties->SetObjectField(TEXT("nodeIds"), MakeNodeIdArraySchema());
    Properties->SetObjectField(TEXT("nodeClasses"), MakeNodeClassArraySchema());
    Properties->SetObjectField(TEXT("includeConnections"), MakeBooleanSchema(false));
    return Schema;
}

TSharedPtr<FJsonObject> MakePcgMutateInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("ops")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("PCG graph asset path."), 1));
    Properties->SetObjectField(TEXT("expectedRevision"), MakeStringSchema(TEXT("Optional optimistic concurrency token."), 1));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(TEXT("Optional idempotency token."), 1));
    Properties->SetObjectField(TEXT("dryRun"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("continueOnError"), MakeBooleanSchema(false));
    Properties->SetObjectField(
        TEXT("ops"),
        MakeMutateOpSchema(
            {
                TEXT("addNode.byClass"),
                TEXT("removeNode"),
                TEXT("moveNode"),
                TEXT("moveNodeBy"),
                TEXT("moveNodes"),
                TEXT("connectPins"),
                TEXT("disconnectPins"),
                TEXT("setProperty")
            },
            false));
    return Schema;
}

TSharedPtr<FJsonObject> MakePcgVerifyInputSchema()
{
    return MakeAssetPathOnlySchema(TEXT("PCG graph asset path."));
}

TSharedPtr<FJsonObject> MakePcgDescribeInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema(TEXT("PCG graph asset path for instance mode."), 1));
    Properties->SetObjectField(TEXT("nodeId"), MakeStringSchema(TEXT("Node GUID for instance mode."), 1));
    Properties->SetObjectField(TEXT("nodeClass"), MakeStringSchema(TEXT("PCG settings class for class mode."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeBlueprintListOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("graphs")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("graphs"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeMaterialListOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("expressions"), TEXT("outputCount")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("expressions"), MakeArraySchema(MakeObjectSchema(true)));
    Properties->SetObjectField(TEXT("outputCount"), MakeIntegerSchema());
    return Schema;
}

TSharedPtr<FJsonObject> MakePcgListOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("nodes")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("nodes"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeDomainQueryOutputSchema(bool bIncludeGraphName)
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("semanticSnapshot"), TEXT("meta"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    if (bIncludeGraphName)
    {
        Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    }
    Properties->SetObjectField(TEXT("revision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("semanticSnapshot"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("meta"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeDomainMutateOutputSchema(bool bIncludeGraphName)
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("applied"), TEXT("partialApplied"), TEXT("assetPath"), TEXT("opResults"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("applied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("partialApplied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    if (bIncludeGraphName)
    {
        Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    }
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
    OpResultProperties->SetObjectField(TEXT("changed"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("nodeId"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("errorCode"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("errorMessage"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("details"), MakeObjectSchema(true));
    OpResultProperties->SetObjectField(TEXT("movedNodeIds"), MakeArraySchema(MakeStringSchema()));
    Properties->SetObjectField(TEXT("opResults"), MakeArraySchema(OpResultSchema));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeDomainVerifyOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("status"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("status"), MakeEnumStringSchema({TEXT("ok"), TEXT("warn"), TEXT("error")}));
    Properties->SetObjectField(TEXT("summary"), MakeStringSchema());
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("graphName"), MakeStringSchema());
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeDomainDescribeOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
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

TSharedPtr<FJsonObject> MakeWidgetAssetInputBase()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    AddRequiredFields(Schema, {TEXT("assetPath")});
    Schema->GetObjectField(TEXT("properties"))->SetObjectField(
        TEXT("assetPath"),
        MakeStringSchema(TEXT("Unreal asset path of the WidgetBlueprint, for example /Game/UI/WBP_MyPanel."), 1));
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetQueryInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeWidgetAssetInputBase();
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(
        TEXT("includeSlotProperties"),
        MakeBooleanSchema(false, true, TEXT("When true, each widget node includes the full slot property map from its parent panel.")));

    TSharedPtr<FJsonObject> FilterSchema = MakeObjectSchema(false);
    FilterSchema->SetStringField(TEXT("description"), TEXT("Optional filters to narrow which widgets are returned."));
    TSharedPtr<FJsonObject> FilterProperties = FilterSchema->GetObjectField(TEXT("properties"));
    FilterProperties->SetObjectField(TEXT("widgetNames"), MakeArraySchema(MakeStringSchema()));
    FilterProperties->SetObjectField(TEXT("widgetClasses"), MakeArraySchema(MakeStringSchema()));
    Properties->SetObjectField(TEXT("filter"), FilterSchema);
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetMutateInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeWidgetAssetInputBase();
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("ops")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("expectedRevision"), MakeStringSchema(TEXT("Optional optimistic concurrency token from a prior widget.query response.")));
    Properties->SetObjectField(TEXT("idempotencyKey"), MakeStringSchema(TEXT("Optional client-supplied idempotency token.")));
    Properties->SetObjectField(TEXT("dryRun"), MakeBooleanSchema(false));
    Properties->SetObjectField(TEXT("continueOnError"), MakeBooleanSchema(false));

    TSharedPtr<FJsonObject> OpSchema = MakeObjectSchema(false);
    AddRequiredFields(OpSchema, {TEXT("op"), TEXT("args")});
    TSharedPtr<FJsonObject> OpProperties = OpSchema->GetObjectField(TEXT("properties"));
    OpProperties->SetObjectField(
        TEXT("op"),
        MakeEnumStringSchema(
            {TEXT("addWidget"), TEXT("removeWidget"), TEXT("setProperty"), TEXT("reparentWidget")},
            FString(),
            TEXT("Widget mutate op id.")));
    TSharedPtr<FJsonObject> ArgsSchema = MakeObjectSchema(true);
    ArgsSchema->SetStringField(TEXT("description"), TEXT("Operation-specific arguments. Required keys depend on op."));
    OpProperties->SetObjectField(TEXT("args"), ArgsSchema);

    TSharedPtr<FJsonObject> OpsSchema = MakeArraySchema(OpSchema);
    OpsSchema->SetNumberField(TEXT("minItems"), 1);
    OpsSchema->SetNumberField(TEXT("maxItems"), 200);
    Properties->SetObjectField(TEXT("ops"), OpsSchema);
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetVerifyInputSchema()
{
    return MakeWidgetAssetInputBase();
}

TSharedPtr<FJsonObject> MakeWidgetDescribeInputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(false);
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(
        TEXT("widgetClass"),
        MakeStringSchema(TEXT("Short class name (e.g. \"TextBlock\") or full path (e.g. \"/Script/UMG.TextBlock\"). Required if assetPath/widgetName not provided.")));
    Properties->SetObjectField(
        TEXT("assetPath"),
        MakeStringSchema(TEXT("Asset path to a WidgetBlueprint. Used together with widgetName to resolve class and read currentValues.")));
    Properties->SetObjectField(
        TEXT("widgetName"),
        MakeStringSchema(TEXT("Designer name of the widget instance inside the WidgetTree. Required when assetPath is provided.")));
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetDescribeOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("widgetClass"), TEXT("properties"), TEXT("slotProperties")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("widgetClass"), MakeStringSchema(TEXT("Full UClass path of the described widget type.")));

    TSharedPtr<FJsonObject> PropDescSchema = MakeObjectSchema(true);
    TSharedPtr<FJsonObject> PropDescProps = PropDescSchema->GetObjectField(TEXT("properties"));
    PropDescProps->SetObjectField(TEXT("name"), MakeStringSchema());
    PropDescProps->SetObjectField(TEXT("type"), MakeStringSchema());
    PropDescProps->SetObjectField(TEXT("category"), MakeStringSchema());
    PropDescProps->SetObjectField(TEXT("writable"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("properties"), MakeArraySchema(PropDescSchema));

    TSharedPtr<FJsonObject> SlotPropSchema = MakeObjectSchema(true);
    TSharedPtr<FJsonObject> SlotPropProps = SlotPropSchema->GetObjectField(TEXT("properties"));
    SlotPropProps->SetObjectField(TEXT("name"), MakeStringSchema());
    SlotPropProps->SetObjectField(TEXT("type"), MakeStringSchema());
    SlotPropProps->SetObjectField(TEXT("writable"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("slotProperties"), MakeArraySchema(SlotPropSchema));
    Properties->SetObjectField(TEXT("currentValues"), MakeObjectSchema(true));
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetQueryOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("assetPath"), TEXT("revision"), TEXT("rootWidget"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("revision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("rootWidget"), MakeObjectSchema(true));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetMutateOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("applied"), TEXT("partialApplied"), TEXT("assetPath"), TEXT("opResults"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("applied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("partialApplied"), MakeBooleanSchema(false, false));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
    Properties->SetObjectField(TEXT("previousRevision"), MakeStringSchema());
    Properties->SetObjectField(TEXT("newRevision"), MakeStringSchema());

    TSharedPtr<FJsonObject> OpResultSchema = MakeObjectSchema(true);
    AddRequiredFields(OpResultSchema, {TEXT("index"), TEXT("op"), TEXT("ok"), TEXT("changed"), TEXT("errorCode"), TEXT("errorMessage")});
    TSharedPtr<FJsonObject> OpResultProperties = OpResultSchema->GetObjectField(TEXT("properties"));
    OpResultProperties->SetObjectField(TEXT("index"), MakeIntegerSchema());
    OpResultProperties->SetObjectField(TEXT("op"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("ok"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("changed"), MakeBooleanSchema(false, false));
    OpResultProperties->SetObjectField(TEXT("errorCode"), MakeStringSchema());
    OpResultProperties->SetObjectField(TEXT("errorMessage"), MakeStringSchema());
    Properties->SetObjectField(TEXT("opResults"), MakeArraySchema(OpResultSchema));
    Properties->SetObjectField(TEXT("diagnostics"), MakeArraySchema(MakeObjectSchema(true)));
    return Schema;
}

TSharedPtr<FJsonObject> MakeWidgetVerifyOutputSchema()
{
    TSharedPtr<FJsonObject> Schema = MakeObjectSchema(true);
    AddRequiredFields(Schema, {TEXT("status"), TEXT("assetPath"), TEXT("diagnostics")});
    TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
    Properties->SetObjectField(TEXT("status"), MakeEnumStringSchema({TEXT("ok"), TEXT("error")}));
    Properties->SetObjectField(TEXT("assetPath"), MakeStringSchema());
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
        {TEXT("graph"), TEXT("Graph Descriptor"), TEXT("Read graph capability descriptor and runtime status."), &MakeGraphDescriptorInputSchema, &MakeOpenOutputSchema},
        {TEXT("blueprint.list"), TEXT("Blueprint List"), TEXT("List Blueprint graphs in an asset."), &MakeBlueprintListInputSchema, &MakeBlueprintListOutputSchema},
        {TEXT("blueprint.query"), TEXT("Blueprint Query"), TEXT("Read node and pin data from a Blueprint graph."), &MakeBlueprintQueryInputSchema, [](){ return MakeDomainQueryOutputSchema(true); }},
        {TEXT("blueprint.mutate"), TEXT("Blueprint Mutate"), TEXT("Apply a batch of write operations to a Blueprint graph."), &MakeBlueprintMutateInputSchema, [](){ return MakeDomainMutateOutputSchema(true); }},
        {TEXT("blueprint.verify"), TEXT("Blueprint Verify"), TEXT("Run read-only structural validation for a Blueprint graph."), &MakeBlueprintVerifyInputSchema, &MakeDomainVerifyOutputSchema},
        {TEXT("blueprint.describe"), TEXT("Blueprint Describe"), TEXT("Describe a Blueprint class or a specific Blueprint graph node."), &MakeBlueprintDescribeInputSchema, &MakeDomainDescribeOutputSchema},
        {TEXT("material.list"), TEXT("Material List"), TEXT("List material expressions in a material asset."), &MakeMaterialListInputSchema, &MakeMaterialListOutputSchema},
        {TEXT("material.query"), TEXT("Material Query"), TEXT("Read expression nodes and pin data from a material."), &MakeMaterialQueryInputSchema, [](){ return MakeDomainQueryOutputSchema(false); }},
        {TEXT("material.mutate"), TEXT("Material Mutate"), TEXT("Apply a batch of write operations to a material asset."), &MakeMaterialMutateInputSchema, [](){ return MakeDomainMutateOutputSchema(false); }},
        {TEXT("material.verify"), TEXT("Material Verify"), TEXT("Compile a material and return diagnostics."), &MakeMaterialVerifyInputSchema, &MakeDomainVerifyOutputSchema},
        {TEXT("material.describe"), TEXT("Material Describe"), TEXT("Describe a material expression class or instance."), &MakeMaterialDescribeInputSchema, &MakeDomainDescribeOutputSchema},
        {TEXT("pcg.list"), TEXT("PCG List"), TEXT("List nodes in a PCG graph asset."), &MakePcgListInputSchema, &MakePcgListOutputSchema},
        {TEXT("pcg.query"), TEXT("PCG Query"), TEXT("Read node and pin data from a PCG graph."), &MakePcgQueryInputSchema, [](){ return MakeDomainQueryOutputSchema(false); }},
        {TEXT("pcg.mutate"), TEXT("PCG Mutate"), TEXT("Apply a batch of write operations to a PCG graph."), &MakePcgMutateInputSchema, [](){ return MakeDomainMutateOutputSchema(false); }},
        {TEXT("pcg.verify"), TEXT("PCG Verify"), TEXT("Run read-only validation for a PCG graph."), &MakePcgVerifyInputSchema, &MakeDomainVerifyOutputSchema},
        {TEXT("pcg.describe"), TEXT("PCG Describe"), TEXT("Describe a PCG settings class or instance."), &MakePcgDescribeInputSchema, &MakeDomainDescribeOutputSchema},
        {TEXT("diag.tail"), TEXT("Diagnostics Tail"), TEXT("Read persisted diagnostics incrementally by sequence cursor."), &MakeDiagTailInputSchema, &MakeDiagTailOutputSchema},
        {TEXT("widget.query"), TEXT("Widget Tree Query"), TEXT("Read the UMG WidgetTree of a WidgetBlueprint asset."), &MakeWidgetQueryInputSchema, &MakeWidgetQueryOutputSchema},
        {TEXT("widget.mutate"), TEXT("Widget Tree Mutate"), TEXT("Apply structural write operations to the UMG WidgetTree of a WidgetBlueprint asset."), &MakeWidgetMutateInputSchema, &MakeWidgetMutateOutputSchema},
        {TEXT("widget.verify"), TEXT("Widget Blueprint Verify"), TEXT("Compile a WidgetBlueprint and return diagnostics."), &MakeWidgetVerifyInputSchema, &MakeWidgetVerifyOutputSchema},
        {TEXT("widget.describe"), TEXT("Widget Class Describe"), TEXT("Enumerate the editable properties of a UMG widget class, with optional current values from a live instance."), &MakeWidgetDescribeInputSchema, &MakeWidgetDescribeOutputSchema},
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
