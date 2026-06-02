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

- `asset.create`
- `asset.inspect`
- `asset.edit`
- `blueprint.inspect`
- `blueprint.class.inspect`
- `blueprint.class.edit`
- `blueprint.member.inspect`
- `blueprint.member.edit`

Recommended public graph surface:

- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.layout`
- `blueprint.graph.palette`
- `blueprint.compile`

## Domain 1: Asset

Asset is a conceptual domain. Blueprint-specific asset operations are being
split away from class contract operations. Generic asset lifecycle operations
belong on the `asset.*` surface.

### Tools

- `asset.create`
- `asset.inspect`
- `asset.edit`
- `blueprint.inspect`

### Asset Edit Operations

Current `asset.*` coverage:

- `asset.create` creates Blueprint, enum, UserDefinedStruct, Material,
  MaterialFunction, PCG graph, and WidgetBlueprint assets.
- `asset.inspect` reads Blueprint, enum, UserDefinedStruct, Material,
  MaterialFunction, PCG graph, and WidgetBlueprint assets through the requested
  `kind`.
- `asset.edit` edits asset-level metadata through `operation=updateMetadata`.
  Enum entries remain available through `kind=enum` and
  `operation=updateEntries` as a compatibility special case. UserDefinedStruct
  field edits are available through `kind=userDefinedStruct` field operations.

### Scope

- asset identity
- asset class
- top-level metadata
- asset summary

### Notes

- `asset.create` is the preferred creation entrypoint for supported asset
  categories.
- `asset.inspect` and `asset.edit` are the preferred generic asset-facing
  entrypoints for asset identity, summaries, and metadata.
- `asset.inspect` may delegate to existing domain readers for graph-shaped
  assets, but structural graph/node/member edits remain on their domain tools.
- `asset.edit` should not absorb structural Blueprint, Material, PCG, or Widget
  editing operations.
- `blueprint.inspect` remains the canonical Blueprint overview entrypoint. It
  returns asset/class identity, lightweight member lists, counts, and routing
  hints to dedicated class, member, and graph inspect tools. It should not
  inspect a specific graph node; use `blueprint.node.inspect` for node details.

## Domain 2: Inheritance / Interfaces

Inheritance and interfaces are Blueprint class contract operations. Public MCP
tools should expose them through `blueprint.class.inspect` and
`blueprint.class.edit`.

### Tools

- `blueprint.class.inspect`
- `blueprint.class.edit`

### Class Edit Operations

Recommended asset-level operations for this domain:

- `setParent`
  - `args.parentClassPath`
- `addInterface`
  - `args.interfaceClassPath`
- `removeInterface`
  - `args.interfaceClassPath`
  - `args.preserveFunctions` defaults to `false`
- `setSettings`
  - `args.settings` is an object containing editable Blueprint Class Settings.
  - Writable settings are limited to `UBlueprint` editor-facing fields:
    `displayName`, `description`, `namespace`, `category`, `hideCategories`,
    `runConstructionScriptOnDrag`, `runConstructionScriptInSequencer`,
    `generateConstClass`, `generateAbstractClass`, `deprecated`,
    `shouldCookPropertyGuids`, and `compileMode`.
  - Read-only runtime state such as `status` and factory identity such as
    `blueprintType` are not writable through this operation.
- `setDefault`
  - `args.property` names one editable class-default property on the generated
    Blueprint class or an inherited parent class.
  - `args.value` is UE import text for that property. The first version accepts
    the same simple value families currently returned by
    `classDefaults`: bool, numeric, name, string, text, enum/byte, object/class
    references, and common structs such as vector, rotator, transform, and color.
  - Arrays, sets, maps, delegates, function params, transient properties,
    deprecated properties, and arbitrary nested object mutation stay unsupported
    until there is a dedicated inspect/edit surface for those shapes.

Implemented interfaces are read through `blueprint.class.inspect`; read-only
queries should not be modeled as `blueprint.class.edit` operations.
`blueprint.class.edit` accepts `dryRun` and `expectedRevision` as mutation
controls. It does not expose `returnDiff` or `returnDiagnostics`; implemented
diff and diagnostics are always returned in the mutation envelope.

### Class Settings Write Mapping

UE exposes the relevant settings as `EditAnywhere` fields on `UBlueprint` in
`Engine/Classes/Engine/Blueprint.h`. The editor customization writes these
fields directly or through property handles, then marks the Blueprint modified;
`deprecated`, `generateConstClass`, `generateAbstractClass`, and compile-mode
style changes should be treated as structural because they affect the generated
class contract.

`namespace` needs the same semantic care as the editor: after assignment, the
Blueprint namespace registry/editor context may need refresh. Loomle should
update the stored `UBlueprint::BlueprintNamespace` and, when the registry API is
available, register or rebuild namespace state instead of treating the namespace
as inert metadata.

### Class Default Write Mapping

`blueprint.class.inspect` reports class defaults from the generated class CDO,
compared against the direct parent class CDO. `setDefault` should write the same
storage: `Blueprint->GeneratedClass->GetDefaultObject(false)`.

The write path resolves a single `FProperty`, verifies it is editable and
serializable by the same rules used by inspect, calls `Modify()` on the CDO and
Blueprint, validates the import text against temporary property storage, imports
the value with UE property import text, marks the Blueprint modified, and returns
the previous value, new value, inherited value when there is a parent property,
and whether the property is now overridden.

Defaults for Blueprint-owned member variables still belong to this class-default
operation because their actual runtime default lives on the generated class CDO.
Variable declaration, type, metadata, category, replication, and exposure flags
remain `blueprint.member.edit` concerns.

### Scope

- current parent class
- inherited contract summary
- implemented interfaces
- interface add/remove lifecycle
- Blueprint Class Settings stored on `UBlueprint`
- generated class identity and class flags
- generated class CDO default overrides relative to the direct parent CDO
- metadata maps for the Blueprint asset, generated class, and parent class

### Notes

- `blueprint.class.inspect` is read-oriented and summarizes the Blueprint class
  lineage, implemented interface contract, Class Settings, CDO overrides, and
  metadata.
- Class Defaults are intentionally simple: the tool takes no extra query
  parameter and returns only properties overridden by the current Blueprint CDO
  relative to its direct parent class CDO. Full parent default browsing belongs
  to a future class-level inspect surface, not this Blueprint overview.
- interface management stays separate from generic asset metadata and member editing.

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

`blueprint.member.inspect` accepts both `memberKind="event"` and
`memberKind="customEvent"`. `event` returns all event signature nodes discovered
in Blueprint ubergraph pages, including native engine event nodes and custom
event nodes. `customEvent` is a narrower view over the same UE source data and
returns only `UK2Node_CustomEvent` entries.

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

## Blueprint Member Edit Contract

`blueprint.member.edit` is the current mutation surface for Blueprint-owned
variables, functions, macros, dispatchers, custom events, and components.

### Request Shape

The public top-level request is intentionally small:

- `assetPath`
- `memberKind`
- `operation`
- `args`
- `dryRun`

`schema.inspect` is the source of truth for each `memberKind.operation` args
shape. `memberKind="event"` is the edit surface for Custom Events. The narrower
`memberKind="customEvent"` name is inspect-only, where it filters event nodes to
`UK2Node_CustomEvent` entries.

### Result Shape

The current result reports whether the request errored and whether it was
applied:

- `isError`
- `assetPath`
- `memberKind`
- `operation`
- `dryRun`
- `applied`
- `valid`
- `previousRevision`
- `newRevision`
- `resolvedRefs`
- `planned`
- `diff`
- `diagnostics`
- `code`
- `message`
- `reason`
- `details`

`dryRun=true` validates the request and resolved UE references, then returns
`applied=false` with a planned edit summary and structured change set. It keeps
`previousRevision == newRevision`.

`expectedRevision` is part of the implemented contract and returns
`REVISION_CONFLICT` without applying when it does not match the current
Blueprint class revision.

## Domain 9: Graph

Graph is a conceptual domain. Public MCP tools should keep high-frequency graph inspection and graph mutation explicit, while lower-frequency graph management can remain folded into broader edit surfaces if schema pressure requires it.

### Tools

- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.layout`
- `blueprint.graph.palette`

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
- `blueprint.graph.list` inventories Blueprint-owned top-level graphs from the
  same UE asset arrays Loomle can resolve later: event/ubergraph pages,
  function graphs, macro graphs, delegate signature graphs, and implemented
  interface graphs.
- `includeCompositeSubgraphs=true` additionally reports inline collapsed
  graphs owned by `UK2Node_Composite`-style nodes with a `BoundGraph`. These
  entries use `graphRef.kind="inline"`, keep `ownerNodeId`, and point back to
  their containing graph through `parentGraphRef`.
- Top-level graph-list failures must expose actionable error codes. Missing or
  unloadable Blueprint assets return `ASSET_NOT_FOUND`; malformed arguments
  return `INVALID_ARGUMENT`; malformed bridge data remains `INTERNAL_ERROR`.
  Optional composite subgraph enumeration failures do not hide top-level graph
  results and must be returned as `diagnostics[]`.
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
- `blueprint.graph.inspect` defaults to `view="summary"` for graph
  orientation. It does not expose a raw node-list view.
- Exact pin names, pin defaults, and connection details are read through
  `blueprint.node.inspect` after `blueprint.graph.inspect` identifies the
  relevant node or flow.

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
- `linkedTo` should only be returned when callers request connection detail.
  Otherwise top-level `semanticSnapshot.edges` is the connection surface.
- `linkedTo` mirrors UE's reciprocal `UEdGraphPin::LinkedTo` storage. It is a
  peer list, not a flow-direction edge list. Direction-oriented responses should
  use `links[*].directionNormalized`, `fromNodeId` / `fromPin`, and
  `toNodeId` / `toPin`; when a link is between an output and an input pin,
  `from` must be the output pin and `to` must be the input pin.

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
- Links should not be duplicated by default in both top-level edges and pin-local
  link arrays. Pin-local link arrays are reserved for connection detail.

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

- public graph-facing requests should prefer the `graph` object
- `graph.id` is preferred after the graph has been discovered
- `graph.name` is the recommended name-based form
- `blueprint.graph.inspect` should not expose top-level `graphName`
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
- `layout` is for visual formatting of an execution tree without changing
  Blueprint behavior.

This split is the core agent-facing editing model for Blueprint graphs.

## blueprint.graph.layout

`blueprint.graph.layout` is the visual formatting interface.

It should be used when:

- the graph already contains the nodes to organize
- the caller wants the execution tree from one root to become easier to read
- the operation should not change execution or data semantics
- the caller wants a dry-run movement plan before applying layout

It should not be used for one-off placement of a single node or a small
hand-picked set of unrelated nodes. Use `blueprint.graph.edit` with explicit
`moveNode` commands for manual positioning.

### Required Properties

- visual-only by default
- deterministic
- dry-run capable
- root-scoped
- diff-friendly

### First-Version Operation

The first version has one operation: format the execution tree reachable from
one root node.

There is no selection mode and no shortcut syntax. Selection-style movement is
intentionally left to explicit `moveNode` commands because a partial selection
has ambiguous behavior around unselected upstream, downstream, and intermediate
nodes.

### Top-Level Request Shape

Recommended shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graph": { "name": "EventGraph" },
  "root": { "id": "branch1" },
  "spacing": { "x": 360, "y": 180 },
  "origin": { "x": 400, "y": 200 },
  "dryRun": true
}
```

Recommended request fields:

- `assetPath`
- `graph`
- `root`
- `spacing`
- `origin`
- `dryRun`
- `expectedRevision`

Rules:

- `assetPath`, `graph`, and `root` are required.
- `root` is a stable node id returned by `blueprint.graph.inspect`.
- If `origin` is omitted, the root keeps its current graph-space position.
- `dryRun` and `expectedRevision` follow the unified mutation contract.

### Formatting Rules

Layout starts from `root`, follows execution output pins downstream, stays
within the addressed graph, and skips already visited nodes. It does not include
unrelated graph nodes.

The formatter uses one algorithm for the whole tree:

- The execution skeleton is laid out first.
- Exec links are aligned by pin anchor, not by node top-left position.
- For a normal exec link, the target node position is chosen so the source
  output exec pin anchor and target input exec pin anchor share the same
  graph-space y coordinate.
- Linear chains keep a straight horizontal execution wire and consistent column
  spacing.
- Nodes with multiple execution outputs, such as Branch, Sequence, Switch, and
  Gate-style nodes, create one lane per execution output pin. Each lane starts
  from that output pin's anchor and uses consistent vertical spacing.
- Branch lanes reserve enough vertical space for their child subtrees so lanes
  do not overlap.
- The formatter may read data-flow links connected to nodes in the execution
  tree to identify support nodes, but data-flow-only traversal must not expand
  the execution tree itself.
- Support/data nodes with a clear single consumer in the execution tree are
  placed compactly below the consuming node or lane, ordered by data dependency
  and input pin order where available.
- Shared support/data nodes, unresolved data ownership, reroute-heavy wiring,
  and unsupported pin layout sources produce diagnostics instead of blocking the
  layout.
- The formatter must not insert, remove, reconnect, or clean up reroute nodes.
- The formatter must not create or resize comment boxes.
- The formatter must not change links, pin defaults, node-local state, or graph
  semantics.

`spacing.x` controls the primary execution-column distance. `spacing.y`
controls the minimum lane and support-node distance. Later versions may add
more detailed spacing controls only if the simple pair is not expressive enough
for real Blueprint graphs.

### Layout Source Rules

`blueprint.node.inspect` and graph inspection can expose node layout and exec
pin anchor data. The formatter should prefer the most reliable available source
but should not fail only because layout geometry is estimated.

Rules:

- `source: "slate"` or another editor-measured source is preferred when the
  Blueprint editor is open and the graph widget can provide geometry.
- `source: "estimate"` is usable and should produce a deterministic layout.
- Unsupported or partial pin layout falls back to conservative default offsets
  when possible.
- Estimated, partial, or unsupported geometry returns diagnostics such as
  `PIN_LAYOUT_ESTIMATED`, `PARTIAL_PIN_LAYOUT`,
  `PIN_LAYOUT_UNSUPPORTED`, or `EDITOR_LAYOUT_RECOMMENDED`.
- Diagnostics should tell the agent that opening the Blueprint editor window
  and retrying may provide more accurate pin anchors.

### Result Shape

Dry run and execution should share the same shape:

```json
{
  "isError": false,
  "valid": true,
  "applied": false,
  "dryRun": true,
  "operation": "blueprint.graph.layout",
  "assetPath": "/Game/Example/BP_Test",
  "graphRef": { "name": "EventGraph" },
  "root": { "id": "branch1" },
  "previousRevision": "rev-42",
  "newRevision": "rev-42",
  "planned": {
    "style": "exec_tree",
    "resolvedNodeCount": 5,
    "supportNodeCount": 2,
    "moves": [
      {
        "node": { "id": "nodeA" },
        "role": "exec",
        "from": { "x": 100, "y": 200 },
        "to": { "x": 400, "y": 200 },
        "constraints": ["exec_anchor_y"]
      }
    ]
  },
  "diff": {
    "scope": "blueprint.graph.layout",
    "changes": []
  },
  "diagnostics": []
}
```

For `dryRun=true`, `applied` must be false and revisions must remain unchanged.
For a real apply, the result should report the same plan shape plus the applied
revision. Diff entries describe node position changes only.

### Explicitly Excluded From Layout MVP

- `organizeGraph`
- implicit single-node shortcut syntax
- whole-graph formatting
- selection formatting
- automatic region discovery beyond the root execution tree
- reroute insertion
- reroute cleanup
- wire routing
- comment creation
- comment fitting
- data-flow-only tree traversal

## blueprint.graph.inspect

`blueprint.graph.inspect` is the read surface for graph orientation and targeted
edit preparation. It should stay one tool, but callers choose response size with
`view`.

Recommended request shape:

```json
{
  "assetPath": "/Game/Example/BP_Test",
  "graph": { "name": "EventGraph" },
  "view": "summary"
}
```

Recommended views:

- `summary` is the default graph orientation view. It returns graph boundary,
  entry/root node references, chain summaries, loose node references, exec/data
  link counts, and a `nodes` dictionary keyed by node id so repeated summary
  references do not duplicate node details.
- `exec_flow` requires `rootNode.id` and returns the execution subgraph reachable
  from that node as lightweight `nodes[]`, `links[]`, `openExecOutputs[]`, and
  traversal metadata.
- `data_flow` requires `rootPin.node.id` plus `rootPin.pin` and traces data
  dependencies for that pin. It defaults to upstream traversal and returns the
  same lightweight `nodes[]` plus `links[]` shape.
- Flow roots are validated against the resolved graph snapshot before a result
  is shaped. Missing `rootNode.id` or `rootPin.node.id` returns
  `NODE_NOT_FOUND`; a missing `rootPin.pin` on an existing node returns
  `PIN_NOT_FOUND`.
- `traversal.maxDepth` is bounded to `1..128`, and `traversal.maxNodes` is
  bounded to `1..1000`; out-of-range values return `INVALID_ARGUMENT`.

`wiring` is intentionally not a `blueprint.graph.inspect` view. Exact pin
names, defaults, and link details for connection-oriented edits belong to
`blueprint.node.inspect` after the relevant node is known.

Readability notes:

- Local `K2Node_MacroInstance` nodes are two-hop readable when their macro graph
  exists on the same Blueprint asset. The caller should use the macro extension
  fields and then inspect the matching graph from `blueprint.graph.list`.
- External macro instances expose their call surface in the current graph, but
  their body is not same-asset readback.
- `K2Node_Tunnel` nodes are graph boundary/interface nodes, not hidden bodies.
- `K2Node_AsyncAction` nodes expose their Blueprint-callable interface while
  the implementation remains runtime or C++ backed.
- `UK2Node_Timeline` exposes template-level summary where available, but
  authored curve keyframes and interpolation remain a separate deeper readback
  concern until Timeline curve serialization is added.

## blueprint.node.inspect and blueprint.node.edit

`blueprint.node.inspect` is the focused read surface for one graph node after
`blueprint.graph.inspect` has identified that the node has node-local structure.
It returns the full serialized node, node-local editable state, and
`editCapabilities` so callers know whether `blueprint.node.edit` is applicable.

`blueprint.graph.inspect` marks such nodes with `hasNodeEditCapabilities: true`
and `inspectWith: "blueprint.node.inspect"`. That routing hint comes from the
bridge's UE-node capability calculation, not from client-side class-name
guessing.

### Node Layout Data

Serialized Blueprint nodes expose graph-space layout data on `node.layout`.
`position` comes from `UEdGraphNode::NodePosX/Y`. `size` and `bounds` use
`w/h` and `x/y/w/h` fields, with legacy `width/height` and
`left/top/right/bottom` fields retained for compatibility. `sizeSource` and
`boundsSource` identify whether the size is `model`, `estimate`, `slate`, or
`unsupported`.

The first layout-data version only adds pin anchors for exec pins. Exec pins
may include:

```json
{
  "name": "then",
  "category": "exec",
  "layout": {
    "side": "right",
    "offset": { "x": 240, "y": 31 },
    "anchor": { "x": 340, "y": 231 },
    "source": "estimate"
  }
}
```

`offset` is relative to the node's top-left graph-space position. `anchor` is
absolute graph-space position. Non-exec pins do not expose pin layout in the
first version. The initial implementation uses `estimate`; a later Slate-backed
pass may return `source: "slate"` when the graph widget has reliable geometry.

`blueprint.node.edit` is limited to UE-native node-local structural actions. It
does not replace `blueprint.graph.edit` for graph wiring/layout, and it does not
replace `blueprint.member.edit` for member signatures.

Supported operation families:

- `addPin`, `removePin`, `insertPin`, and `renamePin` for UE add-pin nodes such
  as Switch, Sequence, container, operator, and Format Text nodes.
- `movePin` for Format Text argument ordering.
- `restorePins` for SetFieldsInStruct field visibility.

Mapping rules:

- Call `blueprint.node.inspect` first and use current pin names from its
  `node.pins` or node-specific `editState`.
- Use `schema.inspect` with `tool: "blueprint.node.edit"` and an operation name
  for the second-layer operation schema.
- Public request fields are `assetPath`, `graph`, `node`, `operation`, `args`,
  optional `dryRun`, and optional `expectedRevision`.
- `dryRun=true` resolves the asset, graph, node, revision, and operation-specific
  preconditions, but does not call UE mutation APIs or mark the Blueprint dirty.
- `expectedRevision` is enforced against the containing graph revision before
  the node-local edit is applied.
- `returnDiff` and `returnDiagnostics` are not public inputs for this tool.
  Results always include the mutation envelope fields, diagnostics, diff,
  revision metadata, and `opResults`.
- Select node `removePin` follows UE `RemoveOptionPinToNode`: it removes the
  last removable option rather than an arbitrary named option.
- SetFieldsInStruct `removePin` hides existing struct field pins; `restorePins`
  restores hidden fields. These operations do not create new struct schema.

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

- `addFromPalette`
- `removeNode`
- `duplicateNode`
- `moveNode`
- `connect`
- `disconnect`
- `insertExec`
- `bypassExec`
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
  "expectedRevision": "rev-42"
}
```

Rules:

- `assetPath` is required
- `graph` is required
- `commands` is required and ordered
- `dryRun` and `expectedRevision` follow the unified mutation contract
- implemented diff and diagnostics are returned directly, without public
  `returnDiff` or `returnDiagnostics` switches

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

#### addFromPalette

Creates one node by executing a selected `blueprint.graph.palette` entry.

Required fields:

- `kind`
- `entry`

Optional fields:

- `alias`
- `position`
- `fromPins`
- `defaults`

Recommended shape:

```json
{
  "kind": "addFromPalette",
  "entry": {
    "id": "blueprint.palette:..."
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
- `entry` should come directly from `blueprint.graph.palette`; callers should not
  guess Blueprint node classes in public requests
- `defaults` only initializes pin defaults on the new node
- when `position` is omitted, `addFromPalette` uses deterministic default
  placement instead of the graph origin
- when `fromPins` contains an exec output pin, the new node is placed to the
  right of the source node and its primary exec input anchor is aligned to the
  source exec output anchor
- `addFromPalette` does not implicitly attach the node to existing structure
  except where UE's palette action uses `fromPins` for native autowiring
- Self references, macro instances, and schema actions are selected through
  `blueprint.graph.palette`

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

#### insertExec

Inserts one node into an existing exec link.

Required fields:

- `kind`
- `from`
- `node`
- `to`

Optional fields:

- `inputPin`
- `outputPin`

Recommended shape:

```json
{
  "kind": "insertExec",
  "from": {
    "node": { "id": "node-a" },
    "pin": "then"
  },
  "node": { "alias": "newStep" },
  "to": {
    "node": { "id": "node-b" },
    "pin": "execute"
  }
}
```

Rules:

- `from` must be an output exec pin
- `to` must be an input exec pin
- `node` may reference an existing node or an alias created earlier in the same
  `graph.edit` request
- `inputPin` defaults to `execute`
- `outputPin` defaults to `then`
- `insertExec` requires an existing direct exec link from `from` to `to`
- the command replaces `from -> to` with `from -> node.inputPin` and
  `node.outputPin -> to`

#### bypassExec

Removes one exec-chain node while preserving the surrounding exec chain.

Required fields:

- `kind`
- `node`

Optional fields:

- `inputPin`
- `outputPin`

Recommended shape:

```json
{
  "kind": "bypassExec",
  "node": { "id": "node-to-bypass" }
}
```

Rules:

- `inputPin` defaults to `execute`
- `outputPin` defaults to `then`
- both pins must be exec pins
- the selected input pin must have exactly one upstream exec link
- the selected output pin must have exactly one downstream exec link
- `bypassExec` replaces `upstream -> node -> downstream` with
  `upstream -> downstream`
- the bypassed node is removed after the replacement link is created
- use `removeNode` for plain deletion when surrounding exec links should not be
  preserved

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

They remain archived as design history in `docs/archive/legacy/BLUEPRINT_GRAPH_REFACTOR_GENERATE_RETIRED.md`. The public path is explicit `blueprint.graph.inspect`, `blueprint.graph.palette`, `blueprint.graph.edit`, and `blueprint.graph.layout`.

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

Core Blueprint mutation interfaces should support:

- `dryRun`
- `expectedRevision`

`returnDiff` and `returnDiagnostics` are retired as public switches for cleaned
surfaces. Tools should return implemented diff and diagnostics fields directly
instead of requiring the agent to request them.

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

- `asset.create`
- `asset.inspect`
- `asset.edit`
- `blueprint.inspect`
- `blueprint.class.inspect`
- `blueprint.class.edit`
- `blueprint.member.inspect`
- `blueprint.member.edit`
- `blueprint.graph.list`
- `blueprint.graph.inspect`
- `blueprint.graph.edit`
- `blueprint.graph.layout`
- `blueprint.graph.palette`
- `blueprint.compile`

## Deferred Topics

The following topics are intentionally deferred to a separate design pass:

- compile payload details
- final recipe file format
- final asset/member edit command schemas
- compile contract
- preview and transaction semantics
- unified identifier model for graph elements
