# Blueprint Interface Design

## Status

Draft. This document fixes the top-level Blueprint interface shape for domains 1-8 only.

Included:

- asset
- inheritance and interfaces
- variables
- functions
- macros
- dispatchers
- components
- graphs

Excluded for now:

- compile and validation workflow details
- transaction model

## Design Principles

- Blueprint is an asset first, not a graph first.
- Top-level tools are organized by object boundary, not by engine implementation detail.
- Asset-level and member-level operations are separated from graph editing.
- Graph is treated as a child resource inside a Blueprint asset.
- Public tool names should expose user intent, not low-level mutate ops.
- Low-frequency asset and member operations should be aggregated into a small public tool surface.

## Object Hierarchy

1. Blueprint Asset
2. Blueprint Member
3. Blueprint Graph
4. Blueprint Graph Element

This document defines public interfaces for levels 1-3 and for member domains under level 2.

## Public Surface Strategy

This design distinguishes between:

- conceptual domains
- public MCP tools

Conceptual domains remain explicit so the model stays clear.

Public MCP tools should still be compressed where usage is low-frequency and schema cost would otherwise be too high.

Recommended compressed public surface for non-graph Blueprint operations:

- `blueprint.inspect`
- `blueprint.edit`
- `blueprint.member.inspect`
- `blueprint.member.edit`

Recommended public graph surface:

- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.refactor`
- `blueprint.graph.generate`
- `blueprint.graph.recipe.list`
- `blueprint.graph.recipe.inspect`
- `blueprint.graph.recipe.validate`
- `blueprint.compile`
- `blueprint.validate`

## Domain 1: Asset

Asset is a conceptual domain. Public MCP tools should expose asset operations through `blueprint.inspect` and `blueprint.edit`.

### Tools

- `blueprint.inspect`
- `blueprint.edit`

### Asset Edit Operations

Recommended `blueprint.edit` operation set:

- `create`
- `duplicate`
- `rename`
- `delete`
- `reparent`
- `setMetadata`
- `getDefaults`
- `setDefaults`

### Scope

- asset identity
- asset class
- parent class
- implemented interfaces
- top-level metadata
- class defaults
- asset summary

### Notes

- `inspect` is the canonical asset overview entrypoint.
- `getDefaults` and `setDefaults` operate on Blueprint-level defaults, not graph-local values.
- `reparent` belongs to asset management, not inheritance inspection.

## Domain 2: Inheritance / Interfaces

Inheritance and interfaces remain a conceptual asset subdomain. Public MCP tools should expose these operations through `blueprint.inspect` and `blueprint.edit`.

### Tools

- `blueprint.inspect`
- `blueprint.edit`

### Asset Edit Operations

Recommended asset-level operations for this domain:

- `setParent`
- `listInterfaces`
- `addInterface`
- `removeInterface`

### Scope

- current parent class
- inherited contract summary
- implemented interfaces
- interface add/remove lifecycle

### Notes

- `inheritance.inspect` is read-oriented and summarizes the Blueprint class lineage.
- interface management stays separate from generic asset metadata.

## Domain 3: Variable

Variable is a conceptual member domain. Public MCP tools should expose variable operations through `blueprint.member.inspect` and `blueprint.member.edit`.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `variable.list`
- `variable.inspect`
- `variable.create`
- `variable.update`
- `variable.rename`
- `variable.delete`
- `variable.reorder`
- `variable.setDefault`

### Scope

- variable identity
- name
- type
- container shape
- editability flags
- replication and exposure flags
- category and metadata
- default value

### Notes

- `setDefault` is variable-level default management, distinct from pin default editing.
- `update` covers properties such as type, flags, metadata, and editor-facing settings.

## Domain 4: Function

Function is a conceptual member domain. Public MCP tools should expose function operations through `blueprint.member.inspect` and `blueprint.member.edit`.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `function.list`
- `function.inspect`
- `function.create`
- `function.updateSignature`
- `function.rename`
- `function.delete`
- `function.setFlags`

### Scope

- function inventory
- function declaration
- inputs and outputs
- pure/const/callable flags
- access and metadata flags

### Notes

- Function declaration management is separate from function graph editing.
- `updateSignature` changes parameters and return definitions, not node bodies.
- `setFlags` is reserved for declaration-level flags only.

## Domain 5: Macro

Macro is a conceptual member domain. Public MCP tools should expose macro operations through `blueprint.member.inspect` and `blueprint.member.edit`.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `macro.list`
- `macro.inspect`
- `macro.create`
- `macro.updateSignature`
- `macro.rename`
- `macro.delete`

### Scope

- macro inventory
- macro declaration
- macro input/output contract

### Notes

- Macro declaration management is separate from macro graph editing.

## Domain 6: Dispatcher

Dispatcher is a conceptual member domain. Public MCP tools should expose dispatcher operations through `blueprint.member.inspect` and `blueprint.member.edit`.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `dispatcher.list`
- `dispatcher.inspect`
- `dispatcher.create`
- `dispatcher.updateSignature`
- `dispatcher.rename`
- `dispatcher.delete`

### Scope

- dispatcher inventory
- dispatcher declaration
- dispatcher parameter contract

### Notes

- Dispatcher signature editing is modeled like function signature editing, but remains a separate domain.

## Domain 7: Custom Event

Custom Event is a conceptual member domain. Public MCP tools should expose custom event operations through `blueprint.member.inspect` and `blueprint.member.edit`; graph edit may create the visible event node, but deeper signature and RPC metadata belongs to the member surface.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `event.create`
- `event.updateSignature`
- `event.addInput`
- `event.setFlags`
- `event.rename`
- `event.delete`

### Scope

- event identity
- event parameter contract
- RPC replication flags
- reliable/unreliable flags
- graph node pin refresh and inspectability

### Notes

- `updateSignature` replaces the Custom Event parameter list.
- `addInput` adds one parameter without replacing existing inputs.
- Business-level Custom Event inputs appear as output pins on the `UK2Node_CustomEvent` node; callers should connect from the event parameter pin to downstream consumers.
- Failed input edits should return structured diagnostics with `reason`, `eventName`, `graphName`, `requestedInput`, `actualInputs`, `actualPins`, `stage`, and `suggestion`.

## Domain 8: Component

Component is a conceptual member domain. Public MCP tools should expose component operations through `blueprint.member.inspect` and `blueprint.member.edit`.

### Tools

- `blueprint.member.inspect`
- `blueprint.member.edit`

### Member Edit Operations

Recommended member-level operations for this domain:

- `component.list`
- `component.inspect`
- `component.create`
- `component.update`
- `component.rename`
- `component.delete`
- `component.reparent`
- `component.reorder`

### Scope

- component inventory
- component class
- attachment hierarchy
- component properties
- component ordering

### Notes

- `reparent` here means changing attachment parent within the component tree.
- Component management is explicitly separated from graph logic.

## Domain 9: Graph

Graph is a conceptual domain. Public MCP tools should keep high-frequency graph inspection and graph mutation explicit, while lower-frequency graph management can remain folded into broader edit surfaces if schema pressure requires it.

### Tools

- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.refactor`
- `blueprint.graph.generate`
- `blueprint.graph.recipe.list`
- `blueprint.graph.recipe.inspect`
- `blueprint.graph.recipe.validate`

### Graph Management Operations

Conceptually required graph-level operations:

- `create`
- `rename`
- `delete`
- `setEntry`
- `layout`

### Scope

- graph inventory
- graph identity
- graph kind
- graph summary
- entry routing metadata
- graph-level layout action
- graph recipe discovery and management

### Notes

- Graphs are child resources inside the Blueprint asset.
- `graph.inspect` is graph-level structure read, not low-level edit.
- graph management operations are lower-frequency than graph inspection and graph mutation.
- `graph.recipe.*` is the recipe discovery and management surface for graph generation.

## Graph Object Model

Graph editing and inspection should be built on an explicit graph object model.

Required object types:

- `BlueprintGraph`
- `GraphNode`
- `GraphPin`
- `GraphLink`
- `NodeType`

Rules:

- `BlueprintGraph` is a graph resource that belongs to a Blueprint asset.
- `GraphNode`, `GraphPin`, and `GraphLink` are graph instance objects.
- `NodeType` describes a creatable node type, not a node instance.
- `node` is a first-class concept in the object model, but not a top-level Blueprint tool domain.
- Public top-level tools should remain asset-first. `node`, `pin`, and `link` stay under the graph model.

### BlueprintGraph

Represents one graph resource inside a Blueprint asset.

Recommended fields:

- `id`
- `name`
- `displayName`
- `kind`
- `role`
- `entryNodeIds`
- `readOnly`
- `metadata`
- `summary`

Recommended `kind` values:

- `event_graph`
- `function_graph`
- `macro_graph`
- `construction_script`
- `animation_graph`
- `delegate_signature_graph`
- `unknown`

Recommended `role` values:

- `event`
- `function_body`
- `macro_body`
- `signature`

### GraphNode

Represents one node instance inside a graph.

Recommended fields:

- `id`
- `graphId`
- `nodeTypeId`
- `title`
- `subtitle`
- `category`
- `kind`
- `position`
- `size`
- `enabled`
- `comment`
- `selected`
- `pins`
- `metadata`

Recommended `kind` values:

- `event`
- `call`
- `pure_call`
- `macro`
- `variable_get`
- `variable_set`
- `branch`
- `sequence`
- `cast`
- `knot`
- `comment`
- `make_struct`
- `break_struct`
- `operator`
- `literal`
- `timeline`
- `custom`

Notes:

- `nodeTypeId` points to the corresponding `NodeType`.
- `pins` should be embedded so graph inspection can return a fully usable node snapshot.

### GraphPin

Represents one pin instance on a node.

Recommended fields:

- `id`
- `nodeId`
- `name`
- `displayName`
- `direction`
- `kind`
- `type`
- `defaultValue`
- `linkedTo`
- `optional`
- `hidden`
- `advanced`
- `orphaned`

Recommended `direction` values:

- `input`
- `output`

Recommended `kind` values:

- `exec`
- `data`

Notes:

- `type` should be structured, not a single display string.
- `linkedTo` should expose direct targets for fast graph traversal.

### PinType

Represents the normalized data type of a graph pin.

Recommended fields:

- `category`
- `subCategory`
- `objectPath`
- `container`
- `reference`
- `const`
- `weak`
- `nullable`

Recommended `category` values:

- `exec`
- `bool`
- `int`
- `int64`
- `float`
- `double`
- `name`
- `string`
- `text`
- `object`
- `class`
- `struct`
- `enum`
- `delegate`
- `wildcard`

Recommended `container` values:

- `single`
- `array`
- `set`
- `map`

### GraphLink

Represents one pin-to-pin connection.

Recommended fields:

- `id`
- `graphId`
- `from`
- `to`
- `kind`

Recommended `kind` values:

- `exec`
- `data`

Notes:

- `from` and `to` should each contain `nodeId` and `pinId`.
- Links should be returned explicitly even if they can be derived from pin linkage.

### NodeType

Represents a creatable node type, not a graph instance.

Recommended fields:

- `id`
- `kind`
- `title`
- `category`
- `searchText`
- `description`
- `source`
- `spawnMode`
- `signature`
- `tags`
- `metadata`

Recommended `source` values:

- `engine_class`
- `ufunction`
- `macro_library`
- `blueprint_member`
- `event`
- `schema_builtin`

Recommended `spawnMode` values:

- `byClass`
- `byFunction`
- `byEvent`
- `byVariable`
- `byMacro`
- `specialized`

### NodeSignature

Represents the signature of a creatable node type.

Recommended fields:

- `inputs`
- `outputs`
- `latent`
- `pure`
- `expandable`
- `supportsDefaults`

Notes:

- `inputs` and `outputs` should use pin-definition objects aligned with `GraphPin`, but without instance-only fields.

## Graph Reference Model

Graph-facing interfaces should use explicit reference types.

Reference objects:

- `GraphRef`
- `NodeRef`
- `PinRef`
- `LinkRef`

Recommended forms:

- `GraphRef`
  - `{ "id": "..." }`
  - `{ "name": "EventGraph" }`
- `NodeRef`
  - `{ "id": "..." }`
  - `{ "alias": "branch1" }`
- `PinRef`
  - `{ "node": { ... }, "pin": "Condition" }`
  - `{ "pinId": "..." }`
- `LinkRef`
  - `{ "id": "..." }`
  - `{ "from": ..., "to": ... }`

Rules:

- inspect returns stable ids
- edit should prefer `id` and request-local `alias`
- fuzzy selectors should not be part of low-level edit
- fuzzy selectors may be allowed later in higher-level refactor or generate interfaces

## Non-Goals For Top-Level Domains

The following should not become top-level Blueprint tool families:

- `blueprint.node.*`
- `blueprint.pin.*`
- `blueprint.link.*`

Reason:

- they are graph-internal objects
- promoting them to top-level domains weakens the asset-first hierarchy

## Graph Editing Domains

Graph-facing mutation should be split into three distinct public interfaces:

- `blueprint.graph.edit`
- `blueprint.graph.refactor`
- `blueprint.graph.generate`

These three interfaces must not be treated as aliases for one shared low-level op list.

### Design Intent

- `edit` is for deterministic, local, low-ambiguity graph changes.
- `refactor` is for structural transformation of existing graph structure.
- `generate` is for creating new local graph structure from controlled recipes.

This split is the core agent-facing editing model for Blueprint graphs.

## blueprint.graph.edit

`blueprint.graph.edit` is the low-level public editing interface.

It should be used when:

- the target graph is known
- the target nodes or pins are known
- the desired changes are local and precise
- the caller does not need structural intent inference

### Required Properties

- local
- deterministic
- low ambiguity
- batch-capable
- id-driven

### Edit Command Vocabulary

Recommended public command set:

- `addNode`
- `addNode.byMacro`
- `removeNode`
- `duplicateNode`
- `moveNode`
- `connect`
- `disconnect`
- `breakLinks`
- `setPinDefault`
- `setNodeComment`
- `setNodeEnabled`
- `addReroute`
- `addCommentBox`

### Rules

- commands should accept stable ids and request-local aliases
- commands should not rely on fuzzy selectors
- each command should be narrow and predictable
- batch editing is allowed, but each command remains explicit

### Top-Level Request Shape

Recommended request fields:

- `assetPath`
- `graph`
- `commands`
- `dryRun`
- `returnDiff`
- `returnDiagnostics`
- `expectedRevision`

Recommended shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graph": {
    "id": "graph-1"
  },
  "commands": [],
  "dryRun": false,
  "returnDiff": true,
  "returnDiagnostics": true,
  "expectedRevision": "rev-42"
}
```

Rules:

- `assetPath` is required
- `graph` is required
- `commands` is required and ordered
- `dryRun`, `returnDiff`, `returnDiagnostics`, and `expectedRevision` follow the unified mutation contract

### Graph Reference Rules

Supported graph references:

- `{ "id": "graph-1" }`
- `{ "name": "EventGraph" }`

Rules:

- write requests should prefer graph `id`
- graph `name` is allowed as a compatibility form
- fuzzy graph selection is not supported in `graph.edit`

### Command Envelope

Each command must include:

- `kind`

Optional shared command fields:

- `alias`

Rules:

- `kind` selects the command schema
- `alias` is only valid for commands that create a new graph object
- first version should stay minimal and avoid per-command execution policy fields

### Reference Types

#### NodeRef

Supported forms:

- `{ "id": "node-1" }`
- `{ "alias": "print1" }`

Rules:

- `id` references an existing node in the graph
- `alias` references a node created earlier in the same request
- fuzzy node selectors are not supported in `graph.edit`

#### PinRef

Supported forms:

- `{ "node": { "id": "node-1" }, "pin": "Then" }`
- `{ "node": { "alias": "print1" }, "pin": "execute" }`

Rules:

- `pin` is the pin name within the resolved node
- `PinRef` in `graph.edit` should remain node-qualified
- direct fuzzy pin lookup is not supported

### Command Schemas

#### addNode

Creates one node instance.

Required fields:

- `kind`
- `nodeType`
- `position`

Optional fields:

- `alias`
- `defaults`

Recommended shape:

```json
{
  "kind": "addNode",
  "nodeType": {
    "id": "ufunction:/Script/Engine.KismetSystemLibrary:PrintString"
  },
  "alias": "print1",
  "position": { "x": 100, "y": 200 },
  "defaults": [
    {
      "pin": "InString",
      "value": "Hello"
    }
  ]
}
```

Rules:

- `alias` is strongly recommended
- `defaults` only initializes pin defaults on the new node
- `addNode` does not implicitly attach the node to existing structure
- Self references use the normal by-class path: `nodeType.id = "class:/Script/BlueprintGraph.K2Node_Self"`
- `K2Node_Self` exposes an output pin named `self`, which may be connected with the standard `connect` command to compatible UObject/Actor input pins
- UE macro instance nodes use the normal by-class path with explicit macro context: `nodeType.id = "class:/Script/BlueprintGraph.K2Node_MacroInstance"`, `macroLibraryAssetPath`, and `macroGraphName`

#### addNode.byMacro

Creates one `UK2Node_MacroInstance` from a Blueprint macro library graph.

Required fields:

- `kind`
- `macroLibraryAssetPath`
- `macroGraphName`
- `position`

Optional fields:

- `alias`

Recommended shape:

```json
{
  "kind": "addNode.byMacro",
  "macroLibraryAssetPath": "/Engine/EditorBlueprintResources/StandardMacros",
  "macroGraphName": "Gate",
  "alias": "authority",
  "position": { "x": 100, "y": 200 }
}
```

Rules:

- `addNode.byMacro` is a direct UE macro instance creation path, not a semantic wrapper around specific macros
- standard macros such as `Gate`, `DoOnce`, `ForLoop`, `ForLoopWithBreak`, and related Blueprint macro library graphs should all use this shape
- `macroGraphName` must match the graph name in the macro library exactly
- when `macroGraphName` is wrong, the tool should return `MACRO_GRAPH_NOT_FOUND` with `availableMacroGraphs` so callers can correct the request without guessing blindly
- `blueprint.graph.inspect` should expose macro identity through the node's macro extension fields

#### removeNode

Removes one node instance.

Required fields:

- `kind`
- `node`

Recommended shape:

```json
{
  "kind": "removeNode",
  "node": { "id": "node-1" }
}
```

Rules:

- removing a node removes its links
- `removeNode` does not auto-heal surrounding structure

#### duplicateNode

Duplicates one node instance.

Required fields:

- `kind`
- `node`

Optional fields:

- `alias`
- `offset`

Recommended shape:

```json
{
  "kind": "duplicateNode",
  "node": { "id": "node-1" },
  "alias": "nodeCopy1",
  "offset": { "x": 48, "y": 32 }
}
```

Rules:

- duplicated nodes copy node-local state and defaults
- duplicated nodes do not copy external links

#### moveNode

Moves one node instance.

Required fields:

- `kind`
- `node`

One of:

- `position`
- `delta`

Recommended shapes:

```json
{
  "kind": "moveNode",
  "node": { "id": "node-1" },
  "position": { "x": 400, "y": 240 }
}
```

```json
{
  "kind": "moveNode",
  "node": { "id": "node-1" },
  "delta": { "x": 120, "y": 0 }
}
```

Rules:

- `position` and `delta` are mutually exclusive
- one `moveNode` command operates on exactly one node

#### connect

Creates one explicit link.

Required fields:

- `kind`
- `from`
- `to`

Recommended shape:

```json
{
  "kind": "connect",
  "from": {
    "node": { "id": "branch1" },
    "pin": "then"
  },
  "to": {
    "node": { "alias": "print1" },
    "pin": "execute"
  }
}
```

Rules:

- `from` must resolve to an output pin
- `to` must resolve to an input pin
- pin type compatibility is enforced
- `connect` does not implicitly insert conversion or cast nodes

#### disconnect

Removes one explicit link.

Required fields:

- `kind`
- `from`
- `to`

Recommended shape:

```json
{
  "kind": "disconnect",
  "from": {
    "node": { "id": "branch1" },
    "pin": "then"
  },
  "to": {
    "node": { "id": "print1" },
    "pin": "execute"
  }
}
```

Rules:

- `disconnect` only removes the specified link
- if the link is absent, the command should be treated as no-op with optional warning

#### breakLinks

Breaks all links for one target pin.

Required fields:

- `kind`
- `target`

Recommended shape:

```json
{
  "kind": "breakLinks",
  "target": {
    "node": { "id": "print1" },
    "pin": "InString"
  }
}
```

Rules:

- `breakLinks` removes all links from the target pin
- `breakLinks` does not modify the pin default value

#### reconstructNode

Reconstructs one node so UE refreshes its pins and node-owned metadata.

Required fields:

- `kind`
- `node`

Optional fields:

- `preserveLinks`

Recommended shape:

```json
{
  "kind": "reconstructNode",
  "node": { "id": "node-call" },
  "preserveLinks": true
}
```

Rules:

- `reconstructNode` returns `pinsBefore`, `pinsAfter`, `linksPreserved`, and `linksDropped`
- `preserveLinks: true` attempts same-name pin relinking after reconstruction
- links that cannot be restored must be reported with a reason

#### setPinDefault

Sets the default value of one pin.

Required fields:

- `kind`
- `target`
- `value`

Recommended shape:

```json
{
  "kind": "setPinDefault",
  "target": {
    "node": { "id": "print1" },
    "pin": "InString"
  },
  "value": "Hello"
}
```

Resource/class pin defaults may use a structured object default instead of stringifying the
object reference:

```json
{
  "kind": "setPinDefault",
  "target": {
    "node": { "id": "spawn1" },
    "pin": "Class"
  },
  "value": {
    "object": "/Game/BP_Coin.BP_Coin_C"
  }
}
```

Rules:

- only valid on pins that support editable defaults
- the target pin must be unlinked
- if the target pin is linked, `setPinDefault` should fail with a constructive error
- `setPinDefault` must not implicitly break links
- object defaults must resolve to an Unreal object/class compatible with the target pin

#### Secondary Surface Hints

Some nodes are only the visible entry point for deeper Blueprint-owned state.
When creating one of these nodes succeeds, `opResults[*].secondarySurface` may point to the
follow-up editing surface and the result diagnostics may include an informational hint.

Examples:

- `UK2Node_Timeline` -> `blueprint.member.edit` with `memberKind="timeline"`
- `UK2Node_CustomEvent` -> `blueprint.member.edit` with `memberKind="event"`
- `UK2Node_AddComponent` -> `blueprint.member.edit` with `memberKind="component"`

#### setNodeComment

Sets or replaces the node comment text.

Required fields:

- `kind`
- `node`
- `comment`

Recommended shape:

```json
{
  "kind": "setNodeComment",
  "node": { "id": "print1" },
  "comment": "Debug output"
}
```

#### setNodeEnabled

Sets enabled state for one node.

Required fields:

- `kind`
- `node`
- `enabled`

Recommended shape:

```json
{
  "kind": "setNodeEnabled",
  "node": { "id": "print1" },
  "enabled": false
}
```

#### addReroute

Creates one reroute node.

Required fields:

- `kind`
- `position`

Optional fields:

- `alias`

Recommended shape:

```json
{
  "kind": "addReroute",
  "alias": "reroute1",
  "position": { "x": 320, "y": 180 }
}
```

Rules:

- `addReroute` only creates the reroute node
- insertion into an existing link belongs to refactor, not edit

#### addCommentBox

Creates one comment box node.

Required fields:

- `kind`
- `bounds`
- `text`

Optional fields:

- `alias`

Recommended shape:

```json
{
  "kind": "addCommentBox",
  "alias": "comment1",
  "bounds": { "x": 80, "y": 120, "w": 500, "h": 260 },
  "text": "Init flow"
}
```

### Execution Rules

The following rules are fixed for the first version of `blueprint.graph.edit`:

- `moveNode` does not support multi-node movement in a single command
- `setPinDefault` on a linked pin fails with a constructive error
- `disconnect` on a missing link is a no-op and may emit a warning
- `duplicateNode` does not copy external links
- `connect` does not auto-insert conversion nodes

### Explicitly Excluded From Edit

The following do not belong in `blueprint.graph.edit`:

- graph layout
- compile
- validate
- replace-node semantics
- insert-between semantics
- wrap-with semantics
- execution fanout semantics
- recipe expansion

### Constructive Error Requirement

`blueprint.graph.edit` errors must be constructive.

Error reporting should help an agent understand:

- what failed
- which rule blocked the request
- what concrete next step can resolve the issue

Graph edit must not rely on opaque failures such as:

- `INVALID_OPERATION`
- `EDIT_FAILED`
- generic freeform failure text without rule context

Recommended diagnostic fields for edit failures:

- `level`
- `code`
- `message`
- `reason`
- `suggestion`
- `context`
- `nextActions`

Recommended examples of rule-oriented error codes:

- `PIN_DEFAULT_REQUIRES_UNLINKED_PIN`
- `CONNECT_REQUIRES_OUTPUT_TO_INPUT`
- `CONNECT_PIN_TYPE_MISMATCH`
- `NODE_REF_NOT_FOUND`
- `ALIAS_NOT_DEFINED`
- `GRAPH_REVISION_MISMATCH`

Recommended properties:

- errors should describe the violated rule, not just the failed implementation path
- errors should include actionable recovery guidance
- errors should include enough structured context for an agent to retry correctly
- `nextActions` should be advisory, not implicit auto-repair

## blueprint.graph.refactor

`blueprint.graph.refactor` is the public structural transformation interface.

It should be used when:

- the graph already contains relevant structure
- the caller wants to transform structure instead of performing raw edits
- the system should handle reconnection or structural preservation logic

### Required Properties

- structure-aware
- transformation-oriented
- diff-friendly
- higher-level than edit

### Refactor Transform Vocabulary

Recommended public transform set:

- `insertBetween`
- `replaceNode`
- `wrapWith`
- `fanoutExec`
- `collapseSelection`
- `expandNode`
- `cleanupReroutes`
- `normalizeLayout`
- `rebindPins`

### First-Priority Refactors

The most valuable initial refactors are:

- `insertBetween`
- `replaceNode`
- `wrapWith`
- `fanoutExec`
- `cleanupReroutes`

### Rules

- transforms should operate on existing graph structure
- transforms may create, delete, and reconnect nodes and links as one semantic action
- transforms should return a normalized change summary
- limited selector support may be allowed later, but deterministic resolution must be returned

### Top-Level Request Shape

Recommended request fields:

- `assetPath`
- `graph`
- `transforms`
- `dryRun`
- `returnDiff`
- `returnDiagnostics`
- `expectedRevision`

Recommended shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graph": {
    "id": "graph-1"
  },
  "transforms": [],
  "dryRun": false,
  "returnDiff": true,
  "returnDiagnostics": true,
  "expectedRevision": "rev-42"
}
```

Rules:

- `assetPath` is required
- `graph` is required
- `transforms` is required and ordered
- top-level control fields follow the unified mutation contract

### Graph Reference Rules

Supported graph references:

- `{ "id": "graph-1" }`
- `{ "name": "EventGraph" }`

Rules:

- write requests should prefer graph `id`
- graph `name` is allowed as a compatibility form

### Transform Envelope

Each transform must include:

- `kind`

Optional shared transform fields:

- `alias`

Rules:

- `kind` selects the transform schema
- `alias` is only valid when the transform introduces one or more new graph objects that should be referenced later

### Core Refactor Schemas

#### insertBetween

Inserts a node or snippet between two linked endpoints.

Required fields:

- `kind`
- `link`
- one of `nodeType` or `recipeSource`

Optional fields:

- `alias`
- `positionHint`
- `inputs`

Recommended shape:

```json
{
  "kind": "insertBetween",
  "link": {
    "from": {
      "node": { "id": "node-a" },
      "pin": "Then"
    },
    "to": {
      "node": { "id": "node-b" },
      "pin": "execute"
    }
  },
  "nodeType": {
    "id": "ufunction:/Script/Engine.KismetSystemLibrary:Delay"
  },
  "alias": "delay1"
}
```

Rules:

- `insertBetween` requires an existing link or resolvable endpoint pair
- `insertBetween` may create a single node or a generated snippet
- reconnection is part of the transform contract

#### replaceNode

Replaces one node with a different node type while attempting compatible rebinding.

Required fields:

- `kind`
- `target`
- `replacement`

Optional fields:

- `alias`
- `rebindPolicy`

Recommended shape:

```json
{
  "kind": "replaceNode",
  "target": {
    "id": "node-old"
  },
  "replacement": {
    "id": "ufunction:/Script/Engine.KismetMathLibrary:Lerp"
  },
  "alias": "lerp1",
  "rebindPolicy": "matchingPins",
  "removeOriginal": true
}
```

Rules:

- `replaceNode` removes the target node and creates a replacement node
- compatible pin rebinding is attempted according to `rebindPolicy`
- unmapped pins must produce diagnostics
- legacy `node` / `nodeType` transform fields are not supported

Recommended `rebindPolicy` values:

- `none`
- `matchingPins`

#### wrapWith

Wraps existing structure with a control or utility node pattern.

Required fields:

- `kind`
- `target`
- `wrapper`

Optional fields:

- `alias`
- `entryPin`
- `targetEntryPin`
- `wrapperExitPin`

Recommended shape:

```json
{
  "kind": "wrapWith",
  "target": {
    "id": "node-call"
  },
  "wrapper": {
    "kind": "branch"
  },
  "alias": "branch1",
  "entryPin": "execute",
  "targetEntryPin": "execute",
  "wrapperExitPin": "then"
}
```

Rules:

- `wrapWith` is structure-preserving rather than pure insertion
- `wrapWith` preserves the target node and inserts the wrapper before the target execution entry pin
- existing upstream execution links into `targetEntryPin` are moved to `entryPin`
- `wrapperExitPin` is connected back to the target `targetEntryPin`
- if the target has no upstream execution links, the wrapper-to-target link is still created and the result reports `upstreamLinksMoved: 0`
- the first version only rewrites execution pins; data inputs should be wired with explicit graph.edit commands

#### fanoutExec

Expands one execution path into a sequence-style fanout.

Required fields:

- `kind`
- `source`
- `targets`

Optional fields:

- `alias`

Recommended shape:

```json
{
  "kind": "fanoutExec",
  "source": {
    "node": { "id": "node-entry" },
    "pin": "Then"
  },
  "targets": [
    {
      "node": { "id": "node-a" },
      "pin": "execute"
    },
    {
      "node": { "id": "node-b" },
      "pin": "execute"
    }
  ],
  "alias": "sequence1"
}
```

Rules:

- `fanoutExec` creates sequence-like flow expansion
- target count must be positive
- output routing must be explicit in the transform result

#### cleanupReroutes

Normalizes or removes unnecessary reroute structure.

Required fields:

- `kind`

Optional fields:

- `scope`
- `mode`

Recommended shape:

```json
{
  "kind": "cleanupReroutes",
  "scope": {
    "graph": { "id": "graph-1" }
  },
  "mode": "redundantOnly"
}
```

Rules:

- `cleanupReroutes` is graph-structural cleanup, not general layout
- the transform must report which reroutes were removed or preserved
- current implementation returns `NOT_IMPLEMENTED`; it must not silently no-op

Recommended `mode` values:

- `redundantOnly`
- `normalizeChains`

### Secondary Refactors

These refactors remain part of the public design, but are lower priority than the core set:

- `collapseSelection`
- `expandNode`
- `normalizeLayout`
- `rebindPins`

### Execution Rules

The following rules are fixed for the first version of `blueprint.graph.refactor`:

- refactors may create, remove, and reconnect nodes and links within one semantic action
- refactors must return normalized diff information
- refactors must produce diagnostics for partial rebinds or compatibility loss
- refactors must not silently drop incompatible connections without diagnostics
- selector-based targeting may be added later, but first version should prefer stable ids

### Explicitly Excluded From Refactor

The following do not belong in `blueprint.graph.refactor`:

- freeform graph generation
- compile
- validate
- generic low-level mutation bundling without structural semantics

## blueprint.graph.generate

`blueprint.graph.generate` is the public local graph generation interface.

It should be used when:

- the caller wants a new graph structure that does not yet exist
- the desired structure matches a controlled pattern
- the system should create a connected snippet rather than require raw node assembly

### Required Properties

- built-in-recipe-driven
- file-recipe-driven
- inline-recipe-driven
- controlled
- non-open-ended

### Unified Recipe Model

Graph generation should use one unified concept: `recipe`.

In this model:

- built-in recipes are system-provided recipes
- user-authored reusable snippets are file recipes
- request-local snippets are inline recipes

Public protocol should prefer `recipeSource` over separate legacy template/recipe modes.

### Relationship To Recipe Management

`blueprint.graph.generate` executes a recipe.

`blueprint.graph.recipe.*` is the discovery and management surface for reusable recipes.

Recommended boundary:

- `graph.generate` executes built-in, file, or inline recipes
- `graph.recipe.list` and `graph.recipe.inspect` provide MCP-native self-discovery for built-in and file recipes
- `graph.recipe.validate` validates reusable recipe definitions
- `graph.recipe.create`, `graph.recipe.update`, and `graph.recipe.delete` manage persistent file recipes
- inline recipes are execution-scoped and are not managed as persistent recipe resources

### Top-Level Request Shape

Recommended request fields:

- `assetPath`
- `graph`
- `recipeSource`
- `inputs`
- `attach`
- `outputBindings`
- `dryRun`
- `returnDiff`
- `returnDiagnostics`
- `expectedRevision`

Recommended shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graph": {
    "id": "graph-1"
  },
  "recipeSource": {
    "kind": "builtIn",
    "id": "branch_then_call"
  },
  "inputs": {},
  "attach": {},
  "outputBindings": {},
  "dryRun": false,
  "returnDiff": true,
  "returnDiagnostics": true,
  "expectedRevision": "rev-42"
}
```

Rules:

- `assetPath` is required
- `graph` is required
- `recipeSource` is required
- `inputs`, `attach`, and `outputBindings` are optional but recommended where applicable
- top-level control fields follow the unified mutation contract

### Recipe Source Kinds

Recommended `recipeSource.kind` values:

- `builtIn`
- `file`
- `inline`

### recipeSource Contract

#### builtIn

Recommended shape:

```json
{
  "kind": "builtIn",
  "id": "branch_then_call"
}
```

Required fields:

- `kind`
- `id`

Rules:

- `id` must resolve to a discoverable built-in recipe
- built-in recipes are immutable

#### file

Recommended shape:

```json
{
  "kind": "file",
  "id": "project.guard_then_print"
}
```

Alternative shape:

```json
{
  "kind": "file",
  "path": "BlueprintRecipes/guard_then_print.json"
}
```

Required fields:

- `kind`
- one of `id` or `path`

Rules:

- file recipes must resolve to a managed persistent recipe resource
- `id` is preferred over raw path once the recipe is registered and discoverable

#### inline

Recommended shape:

```json
{
  "kind": "inline",
  "recipe": {
    "title": "Guard Then Print",
    "inputs": [],
    "attach": {
      "modes": ["append_exec"]
    },
    "outputs": [],
    "steps": []
  }
}
```

Required fields:

- `kind`
- `recipe`

Rules:

- inline recipes use the same conceptual model as inspectable recipes where possible
- inline recipes are request-local and not persisted

### inputs

`inputs` supplies concrete values for the recipe input contract.

Recommended shape:

```json
{
  "message": "Hello",
  "duration": 0.2
}
```

Rules:

- input names must match the recipe input contract
- missing required inputs produce validation errors
- unknown inputs should produce warnings or errors according to strictness policy

### attach

`attach` describes how the generated snippet should connect to existing graph structure.

Recommended shape:

```json
{
  "mode": "append_exec",
  "from": {
    "node": { "id": "node-entry" },
    "pin": "Then"
  }
}
```

Alternative insert-between shape:

```json
{
  "mode": "insert_between_exec",
  "link": {
    "from": {
      "node": { "id": "node-a" },
      "pin": "Then"
    },
    "to": {
      "node": { "id": "node-b" },
      "pin": "execute"
    }
  }
}
```

Recommended attach modes:

- `none`
- `append_exec`
- `insert_between_exec`
- `attach_data_input`
- `attach_data_output`

Rules:

- `attach.mode` must be compatible with the recipe attach contract
- attach resolution must be reported in the result
- missing required attach information produces validation errors

### outputBindings

`outputBindings` requests stable names for created handles that the caller wants to reference later.

Recommended shape:

```json
{
  "primaryNode": "guard1",
  "callNode": "print1"
}
```

Rules:

- output bindings must refer to declared recipe outputs
- output bindings are advisory names for result mapping

### Generation Result Expectations

`blueprint.graph.generate` should return the unified mutation result shape and additionally emphasize:

- created nodes and links
- resolved recipe source
- resolved attach target
- resolved output bindings
- generation diagnostics

Recommended additional result fields:

- `recipe`
- `attachResult`
- `outputBindings`

#### recipe

Normalized description of the executed recipe source.

Recommended fields:

- `kind`
- `id`
- `title`
- `version`

#### attachResult

Normalized description of how the generated snippet was attached.

Recommended fields:

- `mode`
- `resolvedTargets`
- `createdLinks`
- `removedLinks`

#### outputBindings

Resolved named handles requested by the caller.

Recommended fields:

- output name to resolved object mapping

### Execution Rules

The following rules are fixed for the first version of `blueprint.graph.generate`:

- generation must be constrained by a recipe contract
- generation must not accept unconstrained natural-language planning
- generation may create one node or a multi-node snippet
- generation must report created objects and resolved output bindings
- generation must validate recipe source, inputs, and attach contract before apply
- generation must not silently degrade an incompatible attach request

### builtIn Recipes

Built-in recipes are system-provided and stable.

Recommended initial built-in recipes:

- `branch_then_call`
- `delay_then_call`
- `sequence_fanout`
- `set_variable_then_call`
- `guard_clause`
- `cast_then_call`

Rules:

- built-in recipes must be discoverable through MCP
- built-in recipes must be inspectable through MCP
- built-in recipes are immutable

### file Recipes

File recipes are project-defined, persisted, and reusable.

They should be:

- text-based
- diff-friendly
- agent-editable
- repository-managed

Rules:

- file recipes must be discoverable through MCP
- file recipes must be inspectable through MCP
- file recipes must be validatable through MCP
- file recipes are mutable managed resources

### inline Recipes

Inline recipes are request-local recipe definitions.

They are intended for:

- one-off graph generation
- temporary composition
- immediate agent-authored generation flows

Rules:

- inline recipes are request-local
- inline recipes are not persistent managed resources
- inline recipes may be validated during generate or explicit validation flows

### Why All Three Are Needed

- `builtIn` provides standardization
- `file` provides persistence and reuse
- `inline` provides flexibility and low-friction generation

### Rules

- generation should not accept unconstrained natural-language graph planning
- generation should be parameterized, not freeform
- generation should accept a unified recipe source
- generation may support attach points or insertion anchors
- generation should return created nodes, resolved aliases, and attachment results
- built-in and file recipes should share one conceptual model
- inline recipes should use the same recipe model as file recipes where possible

### Explicitly Excluded From Generate

The following do not belong in `blueprint.graph.generate`:

- arbitrary planner-driven graph synthesis
- compile
- validate
- generic low-level mutation replay

## blueprint.graph.recipe

`blueprint.graph.recipe.*` is the public recipe discovery and management domain.

It exists so MCP itself provides recipe self-discovery without depending on repository files or external documentation.

### Recipe Kinds

Discoverable and managed recipe kinds:

- `builtIn`
- `file`

Execution-only recipe kind:

- `inline`

### Tools

- `blueprint.graph.recipe.list`
- `blueprint.graph.recipe.inspect`
- `blueprint.graph.recipe.validate`
- `blueprint.graph.recipe.create`
- `blueprint.graph.recipe.update`
- `blueprint.graph.recipe.delete`

### blueprint.graph.recipe.list

Lists available reusable recipes.

Recommended scope:

- list built-in recipes
- list file recipes
- filter by recipe kind
- filter by tags or capability

Recommended result fields:

- `kind`
- `id`
- `title`
- `summary`
- `inputs`
- `attachModes`
- `outputs`
- `tags`
- `mutable`

### blueprint.graph.recipe.inspect

Returns the full contract of one reusable recipe.

Recommended scope:

- recipe identity
- recipe kind
- title and description
- input contract
- attach contract
- output contract
- structure summary
- constraints
- example usage
- mutability

Rules:

- built-in recipes must be inspectable
- file recipes must be inspectable

Recommended result fields:

- `kind`
- `id`
- `title`
- `summary`
- `description`
- `version`
- `inputs`
- `attach`
- `outputs`
- `structureSummary`
- `steps`
- `constraints`
- `example`
- `tags`
- `mutable`

### Recipe Inspect Contract

#### kind

Recipe kind.

Expected values:

- `builtIn`
- `file`

#### id

Stable recipe identifier.

Rules:

- built-in recipe ids should be stable across versions where possible
- file recipe ids should be stable within the project

#### title

Short display title for the recipe.

#### summary

One-line summary for quick selection.

#### description

Longer description of intended behavior and usage.

#### version

Recipe version identifier or revision marker.

#### inputs

Normalized input contract for the recipe.

Each input definition should include:

- `name`
- `type`
- `required`
- `default`
- `description`

#### attach

Attach contract describing how the recipe can connect into an existing graph.

Recommended fields:

- `modes`
- `entryPoints`
- `exitPoints`
- `constraints`

Recommended attach modes:

- `none`
- `append_exec`
- `insert_between_exec`
- `attach_data_input`
- `attach_data_output`

#### outputs

Named outputs exposed by the recipe after generation.

Each output definition should include:

- `name`
- `kind`
- `description`

Recommended output kinds:

- `node`
- `pin`
- `link`
- `value`

#### structureSummary

Compact structural summary of the recipe body.

Recommended fields:

- `nodeCount`
- `linkCount`
- `containsExecFlow`
- `containsLatentFlow`
- `primaryNodeKinds`

#### steps

Normalized high-level recipe steps intended for learning and debugging.

Rules:

- `steps` should describe semantic structure, not engine-internal implementation noise
- `steps` are for inspection and learning, not necessarily for direct replay

#### constraints

Structural or typing constraints required by the recipe.

Examples:

- required attach mode
- required input type compatibility
- required graph kind

#### example

One representative usage example.

Recommended fields:

- `inputs`
- `attach`
- `expectedOutputs`

#### mutable

Whether the recipe is mutable through MCP.

Rules:

- built-in recipes return `false`
- file recipes return `true`

### blueprint.graph.recipe.validate

Validates a reusable recipe definition.

Recommended scope:

- schema validation
- generation contract validation
- attach compatibility validation
- structural normalization validation

Rules:

- validate should support file recipes
- validate may also support inline recipe payloads for preflight checking

### blueprint.graph.recipe.create

Creates a persistent file recipe.

Rules:

- create applies only to file recipes
- built-in recipes cannot be created through this interface

### blueprint.graph.recipe.update

Updates a persistent file recipe.

Rules:

- update applies only to file recipes
- built-in recipes are immutable

### blueprint.graph.recipe.delete

Deletes a persistent file recipe.

Rules:

- delete applies only to file recipes
- built-in recipes are immutable

## Graph Mutation Boundary Summary

Use `blueprint.graph.edit` when the caller wants explicit, local graph changes.

Use `blueprint.graph.refactor` when the caller wants existing structure transformed semantically.

Use `blueprint.graph.generate` when the caller wants a new local snippet built from a controlled pattern.

## Unified Mutation Contract

All Blueprint write interfaces should share one common mutation contract.

This applies to:

- asset write operations
- inheritance and interface write operations
- variable write operations
- function write operations
- macro write operations
- dispatcher write operations
- component write operations
- graph edit
- graph refactor
- graph generate

### Common Fields

All write interfaces should support:

- `dryRun`
- `returnDiff`
- `returnDiagnostics`
- `expectedRevision`

### dryRun

`dryRun` means:

- parse the request
- resolve references
- validate intent
- produce a normalized execution plan
- do not apply changes

This is a planning and preview mode, not a fake success mode.

### returnDiff

`returnDiff` requests a structured description of the planned or applied change set.

Typical uses:

- agent follow-up editing
- review and audit
- deterministic change inspection

### returnDiagnostics

`returnDiagnostics` requests detailed errors, warnings, and compatibility notes.

Typical uses:

- pin type mismatch reporting
- recipe attachment issues
- transform compatibility issues
- partial mapping warnings

### expectedRevision

`expectedRevision` is the optimistic concurrency guard.

It should be used to ensure that the write request is being applied against the Blueprint state the caller expects.

Typical uses:

- prevent stale writes
- avoid overwriting intervening edits
- support safe multi-step agent workflows

## Unified Mutation Result Model

All write interfaces should use one consistent result shape.

Recommended result fields:

- `applied`
- `dryRun`
- `valid`
- `revision`
- `resolvedRefs`
- `created`
- `updated`
- `deleted`
- `diff`
- `diagnostics`

### Result Field Intent

- `applied`
  - whether the Blueprint was actually modified
- `dryRun`
  - whether the request was evaluated in preview mode
- `valid`
  - whether the request was executable
- `revision`
  - resulting revision, or current revision in dry-run contexts
- `resolvedRefs`
  - normalized graph, node, pin, and recipe reference resolution
- `created`
  - newly created objects
- `updated`
  - modified objects
- `deleted`
  - removed objects
- `diff`
  - structured change summary
- `diagnostics`
  - errors, warnings, and informational messages

## Diagnostic Quality Rule

Diagnostics for Blueprint write interfaces should be agent-usable by default.

This means diagnostics should:

- prefer stable rule codes over generic failure labels
- explain why the request was rejected
- identify the relevant graph, node, pin, command, or transform
- suggest the next likely valid action

Where useful, diagnostics may include structured `nextActions` to guide retry behavior.

## Contract Consistency Rule

These fields should not be selectively invented per domain.

The meaning of:

- `dryRun`
- `returnDiff`
- `returnDiagnostics`
- `expectedRevision`

should remain consistent across all Blueprint write interfaces.

## Recommended Public Surface For Domains 1-8

The current recommended public Blueprint surface for these domains is:

- `blueprint.inspect`
- `blueprint.edit`
- `blueprint.member.inspect`
- `blueprint.member.edit`
- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.refactor`
- `blueprint.graph.generate`
- `blueprint.graph.recipe.list`
- `blueprint.graph.recipe.inspect`
- `blueprint.graph.recipe.validate`
- `blueprint.compile`
- `blueprint.validate`

## Deferred Topics

The following topics are intentionally deferred to a separate design pass:

- compile and validate payload details
- final recipe file format
- final asset/member edit command schemas
- compile and validation contract
- preview and transaction semantics
- unified identifier model for graph elements
