# Blueprint Palette

## Intent

`blueprint.graph.palette` exposes Unreal Engine's Blueprint Action Menu to agents.

Its purpose is creation discovery. An agent should use it to find what UE can
add to a Blueprint graph in a specific context, then execute the selected entry
through `blueprint.graph.edit`.

`blueprint.graph.palette` is not a static node database and not a Loomle-curated node
catalog. It should return entries derived from UE's own action menu machinery
for the requested Blueprint, graph, and optional pin context.

The standard agent-facing node creation flow should be:

1. Query `blueprint.graph.palette`.
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

`blueprint.graph.palette` is read-only.

It searches UE-supported creation actions. It does not create nodes, choose
positions, assign aliases, connect pins, or mutate the graph.

Mutation belongs to `blueprint.graph.edit`.

The recommended creation operation is `addFromPalette`. Lower-level by-class
creation may still exist for fallback or specialized cases, but it should not be
the primary agent-facing path for ordinary Blueprint node creation.

## `blueprint.graph.palette`

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
      "description": "Graph reference from blueprint.graph.list or blueprint.graph.inspect. Use {\"id\":\"...\"} when available, otherwise {\"name\":\"EventGraph\"}."
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
          "node": {
            "type": "object",
            "properties": {
              "id": { "type": "string", "minLength": 1 }
            },
            "required": ["id"],
            "additionalProperties": false
          },
          "pin": { "type": "string", "minLength": 1 }
        },
        "required": ["node", "pin"],
        "additionalProperties": false
      },
      "description": "Optional dragged-from pin context, matching UE's pin-based action menu behavior."
    },
    "limit": {
      "type": "integer",
      "minimum": 1,
      "maximum": 500,
      "default": 50
    },
    "offset": {
      "type": "integer",
      "minimum": 0,
      "default": 0
    }
  },
  "required": ["assetPath", "graph"],
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
    "isError": { "const": false },
    "assetPath": { "type": "string" },
    "graphName": { "type": "string" },
    "total": { "type": "integer" },
    "offset": { "type": "integer" },
    "limit": { "type": "integer" }
  },
  "required": ["isError", "entries", "total", "offset", "limit", "assetPath"],
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
        },
        "executable": {
          "type": "boolean",
          "description": "Whether Loomle can safely execute this entry through addFromPalette in the current context."
        },
        "unavailableReason": {
          "type": "object",
          "description": "Structured diagnostic when executable is false for a known safety or support reason."
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
- palette entry is stale or unsafe to spawn
- UE action cannot be executed in the current context

Errors should tell the agent what to change next: asset path, graph reference,
query text, pin context, or edit operation input.

## `addFromPalette`

`addFromPalette` is a `blueprint.graph.edit` operation that executes one
selected palette entry.

The operation consumes the selected entry and provides mutation choices such as
position, alias, and optional pin context. These are agent decisions and should
not be returned by `blueprint.graph.palette`.

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
      "node": { "id": "..." },
      "pin": "Then"
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
          "node": {
            "type": "object",
            "properties": {
              "id": { "type": "string", "minLength": 1 }
            },
            "required": ["id"],
            "additionalProperties": false
          },
          "pin": { "type": "string", "minLength": 1 }
        },
        "required": ["node", "pin"],
        "additionalProperties": false
      }
    }
  },
  "required": ["kind", "entry"],
  "additionalProperties": false
}
```

## Internal Implementation

`blueprint.graph.palette` should build the same kind of action context UE uses for
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

### Spawner Safety

UE's Action Menu spawners are the source of truth for palette behavior, but some
spawners explicitly assume that their wrapped UE field is still valid. UE's own
implementation does not fully compatibility-check every spawner at invoke time;
for example, `UBlueprintFunctionNodeSpawner::Create` documents that callers
must do viability checks before use, and `Invoke` later passes the function into
`UK2Node_CallFunction::SetFromFunction`.

Loomle therefore preflights field-backed spawners before listing them as
executable and again before `addFromPalette` performs the UE action. This is a
guardrail around the UE execution path, not a separate node database.

The guarded spawner families are:

- function spawners, which require a valid `UFunction` and owner class
- event spawners, which require a valid event function and owner class unless
  the entry is a custom event
- variable spawners, which require a valid member property owner or local graph
  owner
- delegate spawners, which require a valid delegate property owner
- bound-event spawners, which require a valid event delegate owner

When a guarded entry is unsafe, `blueprint.graph.palette` returns it with
`executable=false` and an `unavailableReason`; `addFromPalette` returns the
same structured diagnostic instead of calling `FBlueprintActionMenuItem` and
risking an editor crash.

Within one `blueprint.graph.edit` request, repeated `addFromPalette` commands
with the same Blueprint action context should reuse the request-local action
menu builder result. Building the Blueprint Action Menu can scan and filter a
large UE action set, so repeated node creation in one batch should not rebuild
that menu for every command. The cache is request-local only; it must not make
palette entry ids stable across requests or bypass UE's action menu machinery.

## Coverage Model

Palette coverage should be audited against UE Action Menu entries, not against a
hand-written node list.

Coverage is an audit artifact. It helps Loomle identify gaps, but it must not
become a second source of truth for palette behavior. The source of truth for
discoverable actions remains UE's own action menu and spawner machinery.

Each discovered action should be classified along two axes.

Discovery coverage:

- `listed`: Loomle can return the entry from `blueprint.graph.palette`.
- `filtered`: UE returns the entry only in specific contexts.
- `hidden`: Loomle cannot currently surface the entry.

Execution coverage:

- `executable`: Loomle can execute the entry through `addFromPalette`.
- `queryOnly`: Loomle can list the entry but cannot safely execute it yet.
- `unsupported`: Loomle knows the category but does not support execution.
- `unknown`: Loomle has not classified the entry yet.

Coverage should be summarized by UE action or spawner category:

Current audit baseline:

- UE version: 5.7
- Project: Loomle test project
- Blueprint: temporary Actor Blueprint generated by
  `tools/audit_blueprint_palette.py`
- Measurement: live `blueprint.graph.palette` pagination over UE Action Menu results
- Audit command:
  `python3 tools/audit_blueprint_palette.py --json-out .tmp/blueprint-palette-audit.json --markdown-out .tmp/blueprint-palette-audit.md`
- Execution sample command:
  `python3 tools/audit_blueprint_palette.py --sample-execution --json-out .tmp/blueprint-palette-audit.json --markdown-out .tmp/blueprint-palette-audit.md`
- Grouped execution sample command:
  `python3 tools/audit_blueprint_palette.py --sample-execution --execution-sample-mode groups --json-out .tmp/blueprint-palette-audit.json --markdown-out .tmp/blueprint-palette-audit.md`

| Scenario | Graph | Context | UE Entries | Listed | Executable Path | Unique Labels | Unique Node Classes |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: |
| Event graph | `EventGraph` | `contextSensitive=true` | 17,924 | 17,924 fetched | `addFromPalette` | 17,545 | 125 |
| Event graph | `EventGraph` | `contextSensitive=false` | 35,118 | 35,118 fetched | `addFromPalette` | 30,673 | 126 |
| Construction script | `UserConstructionScript` | `contextSensitive=true` | 17,261 | 17,261 fetched | `addFromPalette` | 16,906 | 81 |
| Construction script | `UserConstructionScript` | `contextSensitive=false` | 33,922 | 33,922 fetched | `addFromPalette` | 29,635 | 82 |
| Function graph | `AuditFunction` | `contextSensitive=true` | 17,425 | 17,425 fetched | `addFromPalette` | 17,068 | 90 |
| Function graph | `AuditFunction` | `contextSensitive=false` | 34,176 | 34,176 fetched | `addFromPalette` | 29,858 | 91 |
| Macro graph | `AuditMacro` | `contextSensitive=true` | 17,470 | 17,470 fetched | `addFromPalette` | 17,105 | 115 |
| Macro graph | `AuditMacro` | `contextSensitive=false` | 32,872 | 32,872 fetched | `addFromPalette` | 28,858 | 115 |

Some UE result sets can report a total that differs by one from the number of
entries fetched by complete pagination. Treat this as an empirical audit note,
not a contract. The stable contract is that Loomle pages over UE's live Action
Menu result set for the requested context.

Representative entries confirmed in this baseline:

| Query / Intent | UE Label | Node Class | Status |
| --- | --- | --- | --- |
| Branch | `Branch` | `/Script/BlueprintGraph.K2Node_IfThenElse` | listed and executable |
| Gate | `Gate` | `/Script/BlueprintGraph.K2Node_MacroInstance` | listed and executable |
| Self | `Get a reference to self` | `/Script/BlueprintGraph.K2Node_Self` | listed and executable |
| Cast To Actor | `Cast To Actor` | `/Script/BlueprintGraph.K2Node_DynamicCast` | listed and executable |
| Print String | `Print String` | `/Script/BlueprintGraph.K2Node_CallFunction` | listed |
| Delay | `Delay` | `/Script/BlueprintGraph.K2Node_CallFunction` | listed |
| Sequence | `Sequence` | `/Script/BlueprintGraph.K2Node_ExecutionSequence` | listed |
| Equal Enum | `Equal (Enum)` | `/Script/BlueprintGraph.K2Node_EnumEquality` | listed |

These numbers are a point-in-time audit snapshot. They can change with UE
version, loaded modules, project plugins, Blueprint parent class, graph type,
selected pins, and context sensitivity.

Future audits should refine results into functional groups without turning the
groups into a curated source of truth:

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

Execution audits should sample representative listed entries by running
`addFromPalette` in two phases:

1. `dryRun=true` with `returnDiff=true`.
2. normal apply with `returnDiff=true`.

Dry run is correct only if it validates the same operation path, reports the
operation result, and leaves graph revision and node count unchanged. A dry run
that mutates graph state is a correctness bug, even if the later apply succeeds.

Grouped execution audits should select samples from UE-derived entries by
action type, node class, and category. The groups are audit buckets only; they
must not become a palette source of truth.

Current grouped audit result with one sample per group:

- samples: 106
- dry run validated without graph mutation: 106
- dry run reported `applied=false`: 106
- normal apply succeeded for executable node spawners: 102
- schema actions reported `PALETTE_ENTRY_NOT_EXECUTABLE`: 4
- unexpected failures: 0

Schema actions are still listed because UE includes them in Action Menu results,
but `addFromPalette` rejects them explicitly because they are not node spawners.
This is an execution-path distinction, not a reason to hide them from
`blueprint.graph.palette`.

## Non-Goals

`blueprint.graph.palette` should not:

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
