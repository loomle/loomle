# Blueprint Palette

## Intent

`blueprint.palette` exposes Unreal Engine's Blueprint Action Menu to agents.

Its purpose is creation discovery. An agent should use it to find what UE can
add to a Blueprint graph in a specific context, then execute the selected entry
through `blueprint.graph.edit`.

`blueprint.palette` is not a static node database and not a Loomle-curated node
catalog. It should return entries derived from UE's own action menu machinery
for the requested Blueprint, graph, and optional pin context.

The standard agent-facing node creation flow should be:

1. Query `blueprint.palette`.
2. Select a returned palette entry.
3. Execute that entry with `blueprint.graph.edit` using `addFromPalette`.

This moves node creation away from agents guessing K2 node classes or internal
construction details, and toward UE's own creation semantics.

## UE Model

Blueprint node creation in the editor is driven by the Blueprint Action Menu.
The relevant UE model includes:

- action context built from the Blueprint, graph, selected pins, and editor
  state
- action menu entries shown to the user
- node spawners behind many executable entries
- schema-specific filtering, especially through the K2 graph schema
- context-sensitive filtering for pin-drag and graph-context actions

Loomle should align with this model. The public result is a palette entry
because that is the editor-facing concept. Internally, most executable entries
will resolve to UE action/spawner objects.

Loomle should not expose raw UE spawner objects directly. It should serialize
enough stable information to let `blueprint.graph.edit` resolve and execute the
selected entry in the matching context.

## Tool Boundary

`blueprint.palette` is read-only.

It searches UE-supported creation actions. It does not create nodes, choose
positions, assign aliases, connect pins, or mutate the graph.

Mutation belongs to `blueprint.graph.edit`.

The recommended creation operation is `addFromPalette`. Lower-level by-class
creation may still exist for fallback or specialized cases, but it should not be
the primary agent-facing path for ordinary Blueprint node creation.

## `blueprint.palette`

### Input Schema

```json
{
  "type": "object",
  "properties": {
    "assetPath": {
      "type": "string",
      "minLength": 1,
      "description": "Blueprint asset path."
    },
    "graph": {
      "type": "object",
      "description": "Graph reference. If omitted, Loomle may use the Blueprint's primary event graph when unambiguous."
    },
    "query": {
      "type": "string",
      "description": "Search text matching UE action labels, keywords, categories, or node intent."
    },
    "contextSensitive": {
      "type": "boolean",
      "default": true,
      "description": "Whether to apply UE context-sensitive action filtering."
    },
    "fromPins": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "nodeId": { "type": "string" },
          "pinId": { "type": "string" },
          "pinName": { "type": "string" }
        },
        "additionalProperties": false
      },
      "description": "Optional dragged-from pin context, matching UE's pin-based action menu behavior."
    },
    "limit": {
      "type": "integer",
      "minimum": 1,
      "default": 50
    },
    "offset": {
      "type": "integer",
      "minimum": 0,
      "default": 0
    }
  },
  "required": ["assetPath"],
  "additionalProperties": false
}
```

### Response Schema

```json
{
  "type": "object",
  "properties": {
    "entries": {
      "type": "array",
      "items": { "$ref": "#/$defs/paletteEntry" }
    },
    "total": { "type": "integer" },
    "offset": { "type": "integer" },
    "limit": { "type": "integer" }
  },
  "required": ["entries", "total", "offset", "limit"],
  "$defs": {
    "paletteEntry": {
      "type": "object",
      "properties": {
        "id": {
          "type": "string",
          "description": "Opaque entry id valid for the palette query context."
        },
        "label": {
          "type": "string",
          "description": "User-visible UE action label."
        },
        "category": {
          "type": "string",
          "description": "UE action menu category when available."
        },
        "keywords": {
          "type": "array",
          "items": { "type": "string" }
        },
        "tooltip": {
          "type": "string"
        },
        "actionType": {
          "type": "string",
          "description": "High-level action classification such as nodeSpawner, variable, function, event, macro, component, or unknown."
        },
        "nodeClass": {
          "type": "string",
          "description": "Expected node class when UE exposes one."
        },
        "target": {
          "type": "object",
          "description": "Optional UE target identity, such as function, variable, event, macro, or class reference."
        },
        "requiresContext": {
          "type": "boolean",
          "description": "Whether execution depends on the same Blueprint, graph, or pin context used for search."
        }
      },
      "required": ["id", "label", "actionType"],
      "additionalProperties": false
    }
  }
}
```

### Error Behavior

Common failures should be structured and actionable:

- asset not found
- asset is not a Blueprint
- graph not found or ambiguous
- source pin not found
- no matching palette entries
- palette entry cannot be resolved
- UE action cannot be executed in the current context

Errors should tell the agent what to change next: asset path, graph reference,
query text, pin context, or edit operation input.

## `addFromPalette`

`addFromPalette` is a `blueprint.graph.edit` operation that executes one
selected palette entry.

The operation consumes the selected entry and provides mutation choices such as
position, alias, and optional pin context. These are agent decisions and should
not be returned by `blueprint.palette`.

```json
{
  "kind": "addFromPalette",
  "entry": {
    "id": "palette-entry:..."
  },
  "position": {
    "x": 400,
    "y": 200
  },
  "alias": "print",
  "fromPins": [
    {
      "nodeId": "...",
      "pinId": "..."
    }
  ]
}
```

### Operation Schema

```json
{
  "type": "object",
  "properties": {
    "kind": { "const": "addFromPalette" },
    "entry": {
      "type": "object",
      "properties": {
        "id": { "type": "string", "minLength": 1 }
      },
      "required": ["id"],
      "additionalProperties": false
    },
    "position": {
      "type": "object",
      "properties": {
        "x": { "type": "number" },
        "y": { "type": "number" }
      },
      "required": ["x", "y"],
      "additionalProperties": false
    },
    "alias": {
      "type": "string",
      "description": "Optional intra-request alias for nodes created by this operation."
    },
    "fromPins": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "nodeId": { "type": "string" },
          "pinId": { "type": "string" },
          "pinName": { "type": "string" }
        },
        "additionalProperties": false
      }
    }
  },
  "required": ["kind", "entry", "position"],
  "additionalProperties": false
}
```

## Internal Implementation

`blueprint.palette` should build the same kind of action context UE uses for
the Blueprint Action Menu:

1. Load and validate the Blueprint asset.
2. Resolve the target graph and optional source pins.
3. Build the UE Blueprint action context.
4. Ask UE's action menu machinery for available actions.
5. Apply query, pagination, and serialization.
6. Return only entries Loomle can later resolve and execute.

`addFromPalette` should reconstruct or validate the matching context before
execution. The entry id is not a permanent global id. It is valid only for the
context needed to reproduce the selected UE action.

## Coverage Model

Palette coverage should be audited against UE Action Menu entries, not against a
hand-written node list.

Coverage is an audit artifact. It helps Loomle identify gaps, but it must not
become a second source of truth for palette behavior. The source of truth for
discoverable actions remains UE's own action menu and spawner machinery.

Each discovered action should be classified along two axes.

Discovery coverage:

- `listed`: Loomle can return the entry from `blueprint.palette`.
- `filtered`: UE returns the entry only in specific contexts.
- `hidden`: Loomle cannot currently surface the entry.

Execution coverage:

- `executable`: Loomle can execute the entry through `addFromPalette`.
- `queryOnly`: Loomle can list the entry but cannot safely execute it yet.
- `unsupported`: Loomle knows the category but does not support execution.
- `unknown`: Loomle has not classified the entry yet.

Coverage should be summarized by UE action or spawner category:

| Category | UE Entries | Listed | Executable | Query Only | Unsupported | Notes |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Function call | TBD | TBD | TBD | TBD | TBD | Includes BlueprintCallable functions. |
| Variable get/set | TBD | TBD | TBD | TBD | TBD | Depends on Blueprint and member context. |
| Event | TBD | TBD | TBD | TBD | TBD | Includes overridable and custom events. |
| Macro | TBD | TBD | TBD | TBD | TBD | Includes macro library entries. |
| Flow control | TBD | TBD | TBD | TBD | TBD | Branch, Sequence, Gate, and related actions. |
| Operators | TBD | TBD | TBD | TBD | TBD | Equality, math, boolean, enum, and conversion actions. |
| Components | TBD | TBD | TBD | TBD | TBD | Add, get, and component-related actions. |
| Delegates | TBD | TBD | TBD | TBD | TBD | Bind, assign, call, and delegate event actions. |
| Timeline | TBD | TBD | TBD | TBD | TBD | May require Blueprint member side effects. |
| Comment/Reroute | TBD | TBD | TBD | TBD | TBD | Editor utility graph entries. |

The exact categories should be refined from UE source and empirical action menu
enumeration. They are documentation and audit groupings, not a replacement for
UE's action model.

## Non-Goals

`blueprint.palette` should not:

- return a complete static list of all Blueprint node classes
- maintain a Loomle-specific node catalog as the primary source of truth
- create nodes directly
- decide node position or alias
- hide UE context sensitivity behind Loomle-specific behavior
- promise palette entry ids are stable across sessions or unrelated contexts

## Audit Checklist

- Can an agent discover actions that UE would show in the same graph context?
- Can an agent create ordinary Blueprint nodes without guessing K2 node classes?
- Can a selected palette entry be executed through `blueprint.graph.edit`?
- Are context-sensitive pin-drag actions represented clearly?
- Are errors actionable enough for the agent to adjust the next call?
- Does the implementation use UE's action menu/spawner path instead of a second
  source of truth?
