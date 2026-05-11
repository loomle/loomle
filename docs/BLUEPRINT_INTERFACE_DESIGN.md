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
- `blueprint.graph.layout`
- `blueprint.palette`
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
- `blueprint.graph.layout`
- `blueprint.palette`

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

### Notes

- Graphs are child resources inside the Blueprint asset.
- `graph.inspect` is graph-level structure read, not low-level edit.
- graph management operations are lower-frequency than graph inspection and graph mutation.
- recipe discovery and graph generation are not part of the current public
  graph surface.

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

Graph-facing mutation should be split into explicit edit and visual layout
interfaces:

- `blueprint.graph.edit`
- `blueprint.graph.layout`

Structural replacement, wrapping, and snippet generation should be composed from
explicit `blueprint.graph.edit` commands after inspection rather than exposed as
separate public abstractions.

### Design Intent

- `edit` is for deterministic, local, low-ambiguity graph changes.
- `refactor` is for structural transformation of existing graph structure.
- `generate` is for creating new local graph structure from controlled recipes.
- `layout` is for visual formatting of an explicit graph region without
  changing Blueprint behavior.

This split is the core agent-facing editing model for Blueprint graphs.

## blueprint.graph.layout

`blueprint.graph.layout` is the visual formatting interface.

It should be used when:

- the graph already contains the nodes to organize
- the caller wants the region to become easier to read
- the operation should not change execution or data semantics
- the caller wants a dry-run movement plan before applying layout

### Required Properties

- visual-only by default
- deterministic
- dry-run capable
- region-scoped
- diff-friendly

### First-Version Operation

Recommended public operation set:

- `format`

The first version supports two explicit scope modes:

- `selection`
- `tree`

There is no shortcut syntax. Formatting one node still uses
`scope.mode="selection"` with one node in `scope.nodes`.

### Top-Level Request Shape

Recommended shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graphName": "EventGraph",
  "operation": "format",
  "scope": {
    "mode": "tree",
    "root": { "id": "branch1" }
  },
  "direction": "right",
  "style": "simple",
  "spacing": { "x": 360, "y": 180 },
  "origin": { "x": 400, "y": 200 },
  "dryRun": true
}
```

### Scope Rules

`selection`:

- requires `scope.nodes`
- moves only the listed nodes
- does not expand the selection automatically

`tree`:

- requires `scope.root`
- includes the root
- follows execution output pins downstream
- stays within the addressed graph
- skips already visited nodes
- does not follow data-flow-only links in the first version

### Formatting Rules

- first version supports `direction: right | down`
- first version supports `style: simple`
- first version accepts optional `spacing`
- if `origin` is omitted, `tree` keeps the root anchored at its current position
- if `origin` is omitted, `selection` uses the selection bounding box top-left
- first version must not insert, remove, or clean up reroute nodes
- first version must not create or fit comment boxes
- first version must not change links

### Result Shape

Dry run and execution should share the same shape:

```json
{
  "changed": true,
  "dryRun": true,
  "operation": "format",
  "scope": {
    "mode": "tree",
    "resolvedNodeCount": 5
  },
  "nodesMoved": [
    {
      "node": { "id": "nodeA" },
      "from": { "x": 100, "y": 200 },
      "to": { "x": 400, "y": 200 }
    }
  ],
  "warnings": []
}
```

### Explicitly Excluded From Layout MVP

- `organizeGraph`
- implicit single-node shortcut syntax
- whole-graph formatting
- automatic region discovery beyond `tree`
- reroute insertion
- reroute cleanup
- wire routing
- comment creation
- comment fitting
- data-flow-only traversal

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

## Retired Graph Refactor And Generate

The former public graph transformation and generation tools are retired:

- `blueprint.graph.refactor`
- `blueprint.graph.generate`
- `blueprint.graph.recipe.*`

They remain archived as design history in `docs/archive/legacy/BLUEPRINT_GRAPH_REFACTOR_GENERATE_RETIRED.md`. The public path is explicit `blueprint.graph.inspect`, `blueprint.palette`, `blueprint.graph.edit`, and `blueprint.graph.layout`.

## Graph Mutation Boundary Summary

Use `blueprint.graph.edit` when the caller wants explicit, local graph changes.

Use `blueprint.graph.layout` when the caller wants visual graph formatting
without semantic changes.

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
- `blueprint.graph.layout`
- `blueprint.palette`
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
