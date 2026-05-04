# Spec: Graph Domain Split — blueprint.* / material.* / pcg.*

Version: draft-2026-04-20
Status: pending approval

---

## 1. Overview

Remove the unified `graph.*` tool family and replace with three independent, domain-specific namespaces. Remove `runScript` entirely. Each domain gets five tools: `list`, `query`, `mutate`, `verify`, `describe`.

Old tools removed:
- `graph.list`, `graph.resolve`, `graph.query`, `graph.mutate`, `graph.verify`

New tools added (15 total):
- `blueprint.list`, `blueprint.query`, `blueprint.mutate`, `blueprint.verify`, `blueprint.describe`
- `material.list`, `material.query`, `material.mutate`, `material.verify`, `material.describe`
- `pcg.list`, `pcg.query`, `pcg.mutate`, `pcg.verify`, `pcg.describe`

`rpc.capabilities` `graphTypes` array removed. `UNSUPPORTED_GRAPH_TYPE` error code removed.

---

## 2. Common Conventions (all three domains)

### 2.1 Asset addressing
All tools accept `assetPath` (package path, e.g. `/Game/BP/MyBlueprint`).

### 2.2 Node addressing
All mutate ops use the same node token resolution order:
1. `nodeId` — GUID string
2. `nodeRef` — intra-request clientRef alias (mutate only)
3. `nodePath` — graph-qualified path (domain specific)
4. `nodeName` — display name (last resort, ambiguity error if multiple)

### 2.3 Optimistic concurrency
`expectedRevision` / `continueOnError` / `idempotencyKey` / `dryRun` — same semantics as current `graph.mutate`.

### 2.4 Position
`x`, `y` — absolute canvas position (integers).
`dx`, `dy` — relative delta for `movenodeby`.

### 2.5 clientRef
Op-level `clientRef` string creates intra-request alias for the created node GUID. Subsequent ops in the same request can reference it via `nodeRef`.

---

## 3. Blueprint Domain

### 3.1 blueprint.list
List all graphs inside a Blueprint asset (event graph, function graphs, macro graphs, collapsed composite subgraphs).

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "includeCompositeSubgraphs": "boolean (default false)"
}
```

**Response:**
```json
{
  "graphs": [
    { "name": "string", "kind": "EventGraph|Function|Macro|CompositeSubgraph", "nodeCount": 0 }
  ]
}
```

### 3.2 blueprint.query
Read node and pin data from a Blueprint graph.

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "graphName": "string (default EventGraph)",
  "nodeIds": ["string"] ,
  "nodeClasses": ["string"],
  "includeComments": "boolean",
  "includePinDefaults": "boolean",
  "includeConnections": "boolean"
}
```

**Response:** same structure as current `graph.query` for Blueprint, but without `graphType` discriminator.

**Game-thread exempt** (like current `graph.query`).

### 3.3 blueprint.mutate
Apply a batch of ops to a Blueprint graph.

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "graphName": "string (default EventGraph)",
  "expectedRevision": "string (optional)",
  "idempotencyKey": "string (optional)",
  "dryRun": "boolean (default false)",
  "continueOnError": "boolean (default false)",
  "ops": [ { "op": "string", ...op fields } ]
}
```

**Op inventory:**

| op | fields | adapter call |
|----|--------|-------------|
| `addNode.byClass` | `nodeClass`, `x`, `y`, `payload`, `clientRef` | `AddNodeByClass()` |
| `addNode.byFunction` | `functionClass`, `functionName`, `x`, `y`, `clientRef` | `AddCallFunctionNode()` |
| `addNode.byEvent` | `eventName`, `eventClass` (opt), `x`, `y`, `clientRef` | `AddEventNode()` |
| `addNode.byVariable` | `variableName`, `variableClass` (opt), `mode` (get/set), `x`, `y`, `clientRef` | `AddVariableGetNode()` / `AddVariableSetNode()` |
| `addNode.byMacro` | `macroLibrary`, `macroName`, `x`, `y`, `clientRef` | `AddMacroNode()` |
| `addNode.branch` | `x`, `y`, `clientRef` | `AddBranchNode()` |
| `addNode.sequence` | `x`, `y`, `clientRef` | `AddExecutionSequenceNode()` |
| `addNode.cast` | `targetClass`, `x`, `y`, `clientRef` | `AddCastNode()` |
| `addNode.comment` | `text`, `x`, `y`, `width`, `height`, `clientRef` | `AddCommentNode()` |
| `addNode.knot` | `x`, `y`, `clientRef` | `AddKnotNode()` |
| `duplicateNode` | `nodeId`/`nodeRef`, `dx`, `dy`, `clientRef` | `DuplicateNode()` (NEW) |
| `removeNode` | `nodeId`/`nodeRef` | `RemoveNode()` |
| `moveNode` | `nodeId`/`nodeRef`, `x`, `y` | `MoveNode()` |
| `moveNodeBy` | `nodeId`/`nodeRef`, `dx`, `dy` | `MoveNode()` with delta |
| `moveNodes` | `nodes: [{nodeId, dx, dy}]` | `MoveNode()` batch |
| `connectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | `ConnectPins()` |
| `disconnectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | `DisconnectPins()` |
| `breakPinLinks` | `nodeId`/`nodeRef`, `pinName` | `BreakPinLinks()` |
| `setPinDefault` | `nodeId`/`nodeRef`, `pinName`, `value` | `SetPinDefaultValue()` |
| `setNodeComment` | `nodeId`/`nodeRef`, `comment` | `SetNodeComment()` (NEW) |
| `setNodeEnabled` | `nodeId`/`nodeRef`, `enabled` (bool) | `SetNodeEnabled()` (NEW) |
| `addGraph` | `graphName`, `kind` (Function/Macro), `clientRef` | `AddGraph()` (NEW) |
| `renameGraph` | `graphName`, `newName` | `RenameGraph()` (NEW) |
| `deleteGraph` | `graphName` | `DeleteGraph()` (NEW) |
| `layoutGraph` | `algorithm` (default: "default") | auto-layout |
| `compile` | — | `CompileBlueprint()` |

### 3.4 blueprint.verify
Read-only structural validation of a Blueprint graph (node counts, pin connection integrity, compile status).

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "graphName": "string (optional)"
}
```

**Response:** same structure as current `graph.verify` filtered to Blueprint.

### 3.5 blueprint.describe
Two modes: class mode and instance mode.

**Class mode** (`assetPath` only, no `graphName`+`nodeId`):
Reflect the Blueprint class — enumerate variables, functions, event signatures, component list.

**Instance mode** (`assetPath` + `graphName` + `nodeId`):
Resolve a specific node; enumerate its pins (name, type, direction, default, connected). Analogous to `widget.describe` instance mode.

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "graphName": "string (optional — triggers instance mode when combined with nodeId)",
  "nodeId": "string (optional — triggers instance mode)"
}
```

---

## 4. Material Domain

### 4.1 material.list
List material expressions in a material asset.

**Parameters:**
```json
{
  "assetPath": "string (required)"
}
```

**Response:**
```json
{
  "expressions": [
    { "nodeId": "string", "class": "string", "x": 0, "y": 0, "comment": "string" }
  ],
  "outputCount": 0
}
```

### 4.2 material.query
Read expression nodes and pin data from a material.

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "nodeIds": ["string"],
  "nodeClasses": ["string"],
  "includeConnections": "boolean"
}
```

**Game-thread exempt.**

### 4.3 material.mutate
Apply batch ops to a material asset.

**Parameters:** same envelope as `blueprint.mutate` (no `graphName`).

**Op inventory:**

| op | fields | UE API |
|----|--------|--------|
| `addNode.byClass` | `nodeClass`, `x`, `y`, `clientRef` | `UMaterialEditingLibrary::CreateMaterialExpression()` |
| `removeNode` | `nodeId`/`nodeRef` | `UMaterialEditingLibrary::DeleteMaterialExpression()` |
| `moveNode` | `nodeId`/`nodeRef`, `x`, `y` | `Expr->MaterialExpressionEditorX/Y` |
| `moveNodeBy` | `nodeId`/`nodeRef`, `dx`, `dy` | same + delta |
| `moveNodes` | `nodes: [{nodeId, dx, dy}]` | batch |
| `connectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | expression input wiring |
| `disconnectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | clear expression input |
| `setProperty` | `nodeId`/`nodeRef`, `property`, `value` | `TFieldIterator` reflection write |

### 4.4 material.verify
Read-only: compile material, check for errors.

**Parameters:**
```json
{
  "assetPath": "string (required)"
}
```

### 4.5 material.describe
Two modes.

**Class mode** (`nodeClass` only):
Reflect a `UMaterialExpression` subclass — list input pins, output pins, editable properties.

**Instance mode** (`assetPath` + `nodeId`):
Resolve a specific expression node in an asset; enumerate its current property values and pin connections.

**Parameters:**
```json
{
  "assetPath": "string (optional — triggers instance mode when combined with nodeId)",
  "nodeId": "string (optional — instance mode)",
  "nodeClass": "string (optional — class mode)"
}
```

---

## 5. PCG Domain

### 5.1 pcg.list
List nodes in a PCG graph asset.

**Parameters:**
```json
{
  "assetPath": "string (required)"
}
```

**Response:**
```json
{
  "nodes": [
    { "nodeId": "string", "class": "string", "x": 0, "y": 0, "label": "string" }
  ]
}
```

### 5.2 pcg.query
Read PCG node and pin data.

**Parameters:**
```json
{
  "assetPath": "string (required)",
  "nodeIds": ["string"],
  "nodeClasses": ["string"],
  "includeConnections": "boolean"
}
```

**Game-thread exempt.**

### 5.3 pcg.mutate
Apply batch ops to a PCG graph.

**Parameters:** same envelope as `blueprint.mutate` (no `graphName`).

**Op inventory:**

| op | fields | UE API |
|----|--------|--------|
| `addNode.byClass` | `nodeClass`, `x`, `y`, `clientRef` | `UPCGGraph::AddNodeOfType()` |
| `removeNode` | `nodeId`/`nodeRef` | `UPCGGraph::RemoveNode()` |
| `moveNode` | `nodeId`/`nodeRef`, `x`, `y` | `UPCGNode::SetNodePosition()` |
| `moveNodeBy` | `nodeId`/`nodeRef`, `dx`, `dy` | same + delta |
| `moveNodes` | `nodes: [{nodeId, dx, dy}]` | batch |
| `connectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | `UPCGGraph::AddEdge()` |
| `disconnectPins` | `fromNodeId`/`fromNodeRef`, `fromPin`, `toNodeId`/`toNodeRef`, `toPin` | `UPCGGraph::RemoveEdge()` |
| `setProperty` | `nodeId`/`nodeRef`, `property`, `value` | `TFieldIterator` reflection write |

### 5.4 pcg.verify
Read-only: check PCG graph for errors (missing connections, unknown node classes).

**Parameters:**
```json
{
  "assetPath": "string (required)"
}
```

### 5.5 pcg.describe
Two modes.

**Class mode** (`nodeClass` only):
Reflect a `UPCGSettings` subclass — list input/output pins and editable properties.

**Instance mode** (`assetPath` + `nodeId`):
Resolve a specific PCG node in an asset; enumerate its current property values and pin connections.

**Parameters:**
```json
{
  "assetPath": "string (optional — triggers instance mode when combined with nodeId)",
  "nodeId": "string (optional — instance mode)",
  "nodeClass": "string (optional — class mode)"
}
```

---

## 6. File-by-File Change List

### Block 1 — New Blueprint Adapter Methods
**File:** `engine/LoomleBridge/Source/LoomleBridge/Public/LoomleBlueprintAdapter.h`
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBlueprintAdapter.cpp`

Add these static methods (all new):
```cpp
static bool DuplicateNode(const FString& AssetPath, const FString& GraphName,
    const FString& NodeGuid, int32 DeltaX, int32 DeltaY,
    FString& OutNewNodeGuid, FString& OutError);

static bool SetNodeComment(const FString& AssetPath, const FString& GraphName,
    const FString& NodeGuid, const FString& Comment, FString& OutError);

static bool SetNodeEnabled(const FString& AssetPath, const FString& GraphName,
    const FString& NodeGuid, bool bEnabled, FString& OutError);

static bool AddFunctionGraph(const FString& AssetPath, const FString& GraphName,
    FString& OutError);

static bool AddMacroGraph(const FString& AssetPath, const FString& GraphName,
    FString& OutError);

static bool RenameGraph(const FString& AssetPath, const FString& OldGraphName,
    const FString& NewGraphName, FString& OutError);

static bool DeleteGraph(const FString& AssetPath, const FString& GraphName,
    FString& OutError);
```

### Block 2 — McpCoreTools.cpp
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/mcp_core/McpCoreTools.cpp`

Remove: all `graph.*` schema builders and registrations (5 tools).
Add: 15 new tool schema builders.

Schema pattern for mutate tools: object with `assetPath` (required), optional `graphName`, `expectedRevision`, `idempotencyKey`, `dryRun`, `continueOnError`, `ops` array.

### Block 3 — LoomleBridgeModule.h
**File:** `engine/LoomleBridge/Source/LoomleBridge/Public/LoomleBridgeModule.h`

Remove declarations:
- `BuildGraphListToolResult`
- `BuildGraphResolveToolResult`
- `BuildGraphQueryToolResult`
- `BuildGraphQueryBaseResult`
- `BuildShapedGraphQueryResult`
- `BuildGraphMutateToolResult`
- `BuildGraphVerifyToolResult`
- `BuildGraphDescriptorToolResult` (if exists)

Add declarations (15 functions):
```cpp
// Blueprint
TSharedPtr<FJsonObject> BuildBlueprintListToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildBlueprintQueryToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildBlueprintMutateToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildBlueprintVerifyToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildBlueprintDescribeToolResult(const TSharedPtr<FJsonObject>& Args);

// Material
TSharedPtr<FJsonObject> BuildMaterialListToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildMaterialQueryToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildMaterialMutateToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildMaterialVerifyToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildMaterialDescribeToolResult(const TSharedPtr<FJsonObject>& Args);

// PCG
TSharedPtr<FJsonObject> BuildPcgListToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildPcgQueryToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildPcgMutateToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildPcgVerifyToolResult(const TSharedPtr<FJsonObject>& Args);
TSharedPtr<FJsonObject> BuildPcgDescribeToolResult(const TSharedPtr<FJsonObject>& Args);
```

### Block 4 — LoomleBridgeBlueprint.inl (new file)
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeBlueprint.inl`

Contains:
- `BuildBlueprintListToolResult` — wraps `FLoomleBlueprintAdapter::ListBlueprintGraphs()` and `ListEventGraphNodes()`
- `BuildBlueprintQueryToolResult` — wraps `FLoomleBlueprintAdapter::ListGraphNodes()`, `GetNodeDetails()`
- `BuildBlueprintMutateToolResult` — op dispatch loop using `ExecuteBlueprintMutateOp()` (extracted and cleaned from existing `LoomleBridgeGraph.inl`)
- `BuildBlueprintVerifyToolResult` — wraps compile status check
- `BuildBlueprintDescribeToolResult` — class mode: `TFieldIterator` over Blueprint CDO; instance mode: `FLoomleBlueprintAdapter::GetNodeDetails()` + pin enumeration

Implementation source: extracted from `LoomleBridgeGraph.inl` Blueprint sections with `runScript` removed and new ops added.

### Block 5 — LoomleBridgeMaterial.inl (new file)
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeMaterial.inl`

Contains:
- `BuildMaterialListToolResult` — enumerate all `UMaterialExpression` nodes
- `BuildMaterialQueryToolResult` — read expression nodes and pins
- `BuildMaterialMutateToolResult` — op dispatch: `ExecuteMaterialMutateOp()` (extracted from `LoomleBridgeGraph.inl`)
- `BuildMaterialVerifyToolResult` — trigger material compile, return error list
- `BuildMaterialDescribeToolResult` — class mode: reflect `UMaterialExpression` subclass; instance mode: resolve asset+nodeId, enumerate properties

### Block 6 — LoomleBridgePcg.inl (new file)
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgePcg.inl`

Contains:
- `BuildPcgListToolResult` — `UPCGGraph` node enumeration
- `BuildPcgQueryToolResult` — PCG node/pin read
- `BuildPcgMutateToolResult` — `ExecutePcgMutateOp()` (extracted from `LoomleBridgeGraph.inl`)
- `BuildPcgVerifyToolResult` — PCG graph structural validation
- `BuildPcgDescribeToolResult` — class mode: reflect `UPCGSettings` subclass; instance mode: resolve asset+nodeId

### Block 7 — LoomleBridgeRpc.inl
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRpc.inl`

Changes:
1. `BuildRpcCapabilitiesResult()`: replace `graph.*` tool names with 15 new names; remove `graphTypes` array.
2. `DispatchTool()` game-thread exemption at line 155: replace `GraphQueryToolName` check with `blueprint.query`, `material.query`, `pcg.query`.
3. `DispatchTool()` dispatch chain: remove all `graph.*` branches; add 15 new branches calling `Build*ToolResult()`.
4. `MapToolErrorCode()`: remove `UNSUPPORTED_GRAPH_TYPE`→1003.

### Block 8 — LoomleBridgeModule.cpp
**File:** `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeModule.cpp`

Changes:
1. Remove `#include "LoomleBridgeGraph.inl"`.
2. Add:
   ```cpp
   #include "LoomleBridgeBlueprint.inl"
   #include "LoomleBridgeMaterial.inl"
   #include "LoomleBridgePcg.inl"
   ```
3. Remove any `GraphQueryToolName`, `GraphMutateToolName`, etc. constant references no longer needed.

### Block 9 — Delete LoomleBridgeGraph.inl
After all three new `.inl` files compile successfully and tests pass, delete:
- `engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeGraph.inl`

### Block 10 — Version Bump
- `engine/LoomleBridge/LoomleBridge.uplugin`: `VersionName` **0.5.0**, `Version` 46
- `client/Cargo.toml`: `version` **0.5.0**
- Any other version strings found by `check_release_versions.py`

### Block 11 — Tests
Update regression tests in `tests/e2e/`:
- `test_bridge_regression.py`: replace `graph.*` call sites with domain-specific calls; verify removal of `graphType` param
- Add new test cases for new ops: `duplicateNode`, `setNodeComment`, `setNodeEnabled`, `addGraph`, `renameGraph`, `deleteGraph`
- Add test cases for `*.describe` class mode and instance mode for all three domains

---

## 7. Error Code Changes

### Removed
- `UNSUPPORTED_GRAPH_TYPE` → 1003 (removed; each domain tool is type-specific)

### Added (for new Blueprint graph management ops)
- `GRAPH_ALREADY_EXISTS` → 1026
- `GRAPH_DELETE_DENIED` → 1027 (e.g. attempt to delete EventGraph)
- `NODE_DUPLICATE_FAILED` → 1028

---

## 8. Constants Changes

In `LoomleBridgeModule.h` or a constants header, remove:
- `GraphQueryToolName`, `GraphMutateToolName`, etc.

Add:
- `BlueprintQueryToolName = TEXT("blueprint.query")`
- `MaterialQueryToolName = TEXT("material.query")`
- `PcgQueryToolName = TEXT("pcg.query")`
(Only query tools need constants since they're referenced in the game-thread exemption check.)

---

## 9. Execution Order

1. **Block 1** — New adapter methods (header + .cpp) — compiles independently
2. **Block 2** — McpCoreTools.cpp schemas — compiles independently
3. **Block 3** — LoomleBridgeModule.h declarations — depends on nothing
4. **Block 4** — LoomleBridgeBlueprint.inl — depends on Blocks 1, 3
5. **Block 5** — LoomleBridgeMaterial.inl — depends on Block 3
6. **Block 6** — LoomleBridgePcg.inl — depends on Block 3
7. **Block 7** — LoomleBridgeRpc.inl — depends on Blocks 4, 5, 6
8. **Block 8** — LoomleBridgeModule.cpp includes — depends on Block 7
9. **Block 9** — Delete LoomleBridgeGraph.inl — after full compile passes
10. **Block 10** — Version bump
11. **Block 11** — Tests
