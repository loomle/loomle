# Blueprint Graph Edit

## Intent

`blueprint.graph.edit` is the explicit local mutation surface for Blueprint
graphs.

It is for changes where the agent already knows the target graph, target nodes,
target pins, and desired local operation. It should not infer structural intent,
search for nodes by fuzzy text, compile the Blueprint, validate the asset, or
expand recipes.

The interface should stay small in `tools/list`. Command-specific schemas are
loaded on demand through the schema inspection flow.

## Tool Boundary

`blueprint.graph.edit` owns local graph edits:

- create one node from a selected UE palette entry
- connect or disconnect explicit pins
- clear links on one pin
- set one editable pin default
- remove one node
- move one node

It does not own:

- palette discovery, which belongs to `blueprint.palette`
- structural refactors, which belong to `blueprint.graph.refactor`
- recipe expansion, which belongs to `blueprint.graph.generate`
- visual formatting, which belongs to `blueprint.graph.layout`
- compile or validate
- graph management such as add, rename, or delete graph

## Lightweight Public Schema

The `tools/list` schema should expose only the stable command envelope:

```json
{
  "type": "object",
  "properties": {
    "assetPath": { "type": "string", "minLength": 1 },
    "graph": { "type": "object" },
    "graphName": { "type": "string", "minLength": 1 },
    "commands": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "kind": { "type": "string", "minLength": 1 },
          "alias": { "type": "string", "minLength": 1 }
        },
        "required": ["kind"],
        "additionalProperties": true
      },
      "description": "Ordered Blueprint graph edit commands. Use schema.inspect with tool='blueprint.graph.edit' and operation=<kind> for command-specific schema."
    },
    "dryRun": { "type": "boolean" },
    "expectedRevision": { "type": "string" },
    "returnDiff": { "type": "boolean" },
    "returnDiagnostics": { "type": "boolean" }
  },
  "required": ["assetPath", "commands"],
  "additionalProperties": false
}
```

The lightweight schema is intentionally incomplete. It tells the agent how to
send an ordered command batch and how to discover the second-level command
schema.

## Command Classification

### Core Commands

These are the first-class public command vocabulary:

| Command | Purpose |
| --- | --- |
| `addFromPalette` | Execute one selected `blueprint.palette` entry. |
| `connect` | Create one explicit pin link. |
| `disconnect` | Remove one explicit pin link. |
| `breakLinks` | Remove all links from one pin. |
| `setPinDefault` | Set one editable pin default. |
| `removeNode` | Remove one node. |
| `moveNode` | Move one node by absolute position or delta. |

These commands are local, deterministic, and safe for agents to compose in a
batch.

### Secondary Commands

These commands may remain implemented, but they should not be emphasized in the
first-level schema summary:

| Command | Classification |
| --- | --- |
| `addCommentBox` | Layout / annotation. |
| `addReroute` | Layout / wire organization. |
| `setNodeComment` | Annotation. |
| `duplicateNode` | Advanced editing; can duplicate incomplete local state. |
| `reconstructNode` | Maintenance / repair. |
| `setNodeEnabled` | Editor state. |

They can be documented and returned by schema inspection under their category,
but they are not part of the core graph construction workflow.

### Internal Or Non-Public Operations

These operations should not be presented as normal public commands:

| Operation | Preferred Surface |
| --- | --- |
| `rebindMatchingPins` | Internal implementation or `blueprint.graph.refactor`. |
| `moveInputLinks` | Internal implementation or `blueprint.graph.refactor`. |
| `layoutGraph` | `blueprint.graph.layout` with `operation="format"`. |
| `compile` | `blueprint.compile`. |
| `moveNodes` | `blueprint.graph.layout` selection formatting, or explicit `moveNode` commands. |
| `addGraph` / `addFunctionGraph` / `addMacroGraph` | Graph management surface. |
| `renameGraph` | Graph management surface. |
| `deleteGraph` | Graph management surface. |

Keeping these out of the primary public vocabulary prevents
`blueprint.graph.edit` from becoming a dump of bridge internals.

## Shared Reference Types

### NodeRef

```json
{
  "oneOf": [
    { "type": "object", "required": ["id"], "properties": { "id": { "type": "string" } } },
    { "type": "object", "required": ["alias"], "properties": { "alias": { "type": "string" } } }
  ]
}
```

`id` references an existing node. `alias` references a node created earlier in
the same request.

### PinRef

```json
{
  "type": "object",
  "properties": {
    "node": { "$ref": "#/$defs/nodeRef" },
    "pin": { "type": "string", "minLength": 1 }
  },
  "required": ["node", "pin"],
  "additionalProperties": false
}
```

Pins are always node-qualified. Fuzzy pin lookup is not part of
`blueprint.graph.edit`.

## Core Command Schemas

### addFromPalette

Creates a node by executing one entry returned from `blueprint.palette`.

```json
{
  "kind": "addFromPalette",
  "entry": { "id": "palette:..." },
  "position": { "x": 400, "y": 200 },
  "alias": "branch"
}
```

Required fields:

- `kind`
- `entry.id`

Optional fields:

- `position`
- `alias`
- `fromPins`
- `contextSensitive`

Rules:

- `entry` should be the full palette entry returned by `blueprint.palette`.
- `alias` is request-local and is chosen by the agent.
- dry run must resolve the same palette entry but must not mutate the graph.
- schema actions are listed by `blueprint.palette` but rejected with
  `PALETTE_ENTRY_NOT_EXECUTABLE`.

### connect

Creates one explicit pin link.

```json
{
  "kind": "connect",
  "from": { "node": { "alias": "branch" }, "pin": "then" },
  "to": { "node": { "alias": "print" }, "pin": "execute" }
}
```

Required fields:

- `kind`
- `from`
- `to`

Rules:

- `from` must resolve to an output pin.
- `to` must resolve to an input pin.
- pin compatibility is enforced by UE.
- conversion, cast, or promotion nodes are not inserted implicitly.

### disconnect

Removes one explicit pin link.

```json
{
  "kind": "disconnect",
  "from": { "node": { "id": "node-1" }, "pin": "then" },
  "to": { "node": { "id": "node-2" }, "pin": "execute" }
}
```

Required fields:

- `kind`
- `from`
- `to`

### breakLinks

Removes all links from one pin.

```json
{
  "kind": "breakLinks",
  "target": { "node": { "id": "node-1" }, "pin": "InString" }
}
```

Required fields:

- `kind`
- `target`

Rules:

- this does not change the pin default value.
- this should not remove unrelated node state.

### setPinDefault

Sets the default value of one editable pin.

```json
{
  "kind": "setPinDefault",
  "target": { "node": { "alias": "print" }, "pin": "InString" },
  "value": "Hello"
}
```

Object or class defaults may use a structured value:

```json
{
  "kind": "setPinDefault",
  "target": { "node": { "alias": "spawn" }, "pin": "Class" },
  "value": { "object": "/Game/BP_Coin.BP_Coin_C" }
}
```

Required fields:

- `kind`
- `target`
- `value`

Rules:

- the target pin must support editable defaults.
- linked pins should fail with a constructive error.
- `setPinDefault` must not implicitly break links.

### removeNode

Removes one node and its links.

```json
{
  "kind": "removeNode",
  "node": { "id": "node-1" }
}
```

Required fields:

- `kind`
- `node`

Rules:

- removal does not auto-heal surrounding graph structure.

### moveNode

Moves one node by absolute position or by delta.

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

Required fields:

- `kind`
- `node`
- exactly one of `position` or `delta`

Rules:

- one command moves one node.
- multi-node layout belongs to a layout-specific workflow.

## Response Contract

The top-level result should include:

- `isError`
- `applied`
- `dryRun`
- `partialApplied`
- `assetPath`
- `graphName`
- `graphRef`
- `previousRevision`
- `newRevision`
- `opResults`
- `commandResults`
- `diagnostics`
- `diff`

Each entry in `opResults` includes `durationMs`, measured on the UE side for
that command after request-local reference rewriting has completed. The timing
is diagnostic data for agent and developer feedback; it must not be used as a
success criterion.

For `dryRun=true`, `applied` must be `false`, command results must report
`changed=false`, and graph revision/node count must not change.

## UE Implementation Model

The public command layer maps to UE graph operations through Loomle's bridge:

- `addFromPalette` resolves a UE Blueprint Action Menu entry and executes it
  when it is backed by a node spawner.
- link operations call UE schema link validation and pin connection APIs.
- pin default writes use UE pin default serialization and object/class
  resolution.
- node movement changes editor graph coordinates, not Blueprint runtime logic.

The public schema should describe agent intent. The bridge may keep lower-level
legacy operations internally, but those operations are not the public contract.

`blueprint.graph.edit` is a batch operation at the UE boundary. A single request
should resolve the target Blueprint and graph once per graph scope, reuse those
resolved objects while executing local graph edits, and avoid broadcasting graph
or Blueprint dirty notifications after every command. Commands that mutate the
same graph should record that the graph changed, then the bridge should emit one
`NotifyGraphChanged()` and one Blueprint dirty/modified update after the command
batch completes successfully or partially applies.

This batching must not replace UE semantics. Link edits still use the UE graph
schema's connection validation and pin mutation APIs. Node creation through
`addFromPalette` still executes UE action menu entries. The batching only moves
expensive editor notifications and repeated request-local lookup work out of the
per-command loop.
