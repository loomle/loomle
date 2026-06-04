# Blueprint Schema Inspection

## Intent

Blueprint tools have nested operation schemas. Exposing all nested schemas in
`tools/list` wastes context and makes the first-level tool list harder for an
agent to scan.

Schema inspection provides a second-level lookup path. The first-level tool
schema remains directly callable and keeps the operation envelope; the agent
asks for the exact operation payload schema only when it needs that operation.

The lookup path must be discoverable from `tools/list` through the global schema
exposure contract in
[`docs/TOOL_SCHEMA_EXPOSURE_CONTRACT.md`](../TOOL_SCHEMA_EXPOSURE_CONTRACT.md).
Blueprint edit tools that keep operation-specific payloads out of `tools/list`
must expose a structured hint that names `schema_inspect`, the Blueprint domain,
the target tool, and the operation selector path. Prose descriptions may repeat
that hint, but prose is not the contract.

All domain tools may use schema inspection for full input and output schemas.
Tools with nested edit operations additionally expose operation schemas through
the same lookup path.

## Design Principle

The formal documentation is the source of truth.

A schema inspection tool may return structured slices of that documentation,
but it must not become a second business inventory or a separate model of
Blueprint behavior.

## Proposed Tool

`schema_inspect`

```json
{
  "domain": "blueprint",
  "tool": "blueprint_member_edit",
  "operation": "variable.create",
  "include": ["operation", "examples", "errors"]
}
```

### Input Schema

```json
{
  "type": "object",
  "properties": {
    "domain": {
      "type": "string",
      "enum": ["blueprint"]
    },
    "tool": {
      "type": "string",
      "minLength": 1
    },
    "operation": {
      "type": "string",
      "description": "Optional operation or command name within the tool."
    },
    "include": {
      "type": "array",
      "items": {
        "type": "string",
        "enum": ["summary", "input", "operation", "examples", "errors", "notes", "output"]
      },
      "default": ["summary", "operation"]
    }
  },
  "required": ["domain", "tool"],
  "additionalProperties": false
}
```

### Response Shape

```json
{
  "domain": "blueprint",
  "tool": "blueprint_graph_edit",
  "operation": "addFromPalette",
  "category": "core",
  "summary": "Execute one selected blueprint_graph_palette entry.",
  "operationSchema": {},
  "examples": [],
  "errors": [],
  "notes": [],
  "source": {
    "document": "docs/blueprint/graph-edit.md",
    "section": "addFromPalette"
  }
}
```

When `operation` is omitted, the response should return a compact operation
index:

```json
{
  "domain": "blueprint",
  "tool": "blueprint_graph_edit",
  "operations": [
    { "name": "addFromPalette", "category": "core" },
    { "name": "connect", "category": "core" },
    { "name": "addCommentBox", "category": "annotation" }
  ]
}
```

## Agent Flow

For Blueprint graph node creation:

1. Read the `blueprint_graph_edit` `schemaHints` entry from `tools/list`; it
   points command schemas to `schema_inspect`.
2. Call `blueprint_graph_palette` to find a UE Action Menu entry.
3. Call `schema_inspect` with `tool="blueprint_graph_edit"` and
   `operation="addFromPalette"`.
4. Call `blueprint_graph_edit` with the returned command shape and the selected
   palette entry.

For pin editing:

1. Inspect the graph with `blueprint_graph_inspect` using `view="summary"` or
   `view="exec_flow"` to identify the relevant node.
   `summary` returns node references plus a de-duplicated `nodes` dictionary;
   `exec_flow` returns the reachable execution subgraph as lightweight
   `nodes[]` and `links[]`.
2. Inspect the node with `blueprint_node_inspect` to read exact pins and link
   details.
3. Follow the `blueprint_graph_edit` `schemaHints` entry for
   `commands[].kind`.
4. Call `schema_inspect` for `connect`, `disconnect`, `breakLinks`, or
   `setPinDefault`.
5. Call `blueprint_graph_edit`.

For Blueprint member editing:

1. Read the `blueprint_member_edit` `schemaHints` entry from `tools/list`; it
   identifies the operation as `memberKind + "." + operation`.
2. Call `schema_inspect` with `tool="blueprint_member_edit"` and no operation
   to list supported `memberKind.operation` entries when the operation set is
   not already known.
3. Call `schema_inspect` again with an operation such as `variable.create`,
   `event.addInput`, or `component.create`.
4. Call `blueprint_member_edit` using the returned top-level request shape.

## `tools/list` Hint Requirements

Blueprint first-level input schemas are not compressed. `tools/list` must show
ordinary Blueprint input fields directly, including palette fields such as
`assetPath`, `graph`, `query`, `contextSensitive`, `fromPins`, `limit`, and
`offset`.

Only operation-specific payloads move behind `schema_inspect`. Blueprint tools
that use operation-specific schemas must keep the operation selector envelope in
`tools/list` and include structured hints.

`blueprint_graph_edit` should expose a hint equivalent to:

```json
{
  "purpose": "operation_schema",
  "schemaTool": "schema_inspect",
  "domain": "blueprint",
  "tool": "blueprint_graph_edit",
  "operationFrom": "commands[].kind"
}
```

`blueprint_node_edit` should expose:

```json
{
  "purpose": "operation_schema",
  "schemaTool": "schema_inspect",
  "domain": "blueprint",
  "tool": "blueprint_node_edit",
  "operationFrom": "operation"
}
```

`blueprint_member_edit` should expose:

```json
{
  "purpose": "operation_schema",
  "schemaTool": "schema_inspect",
  "domain": "blueprint",
  "tool": "blueprint_member_edit",
  "operationFrom": "memberKind + '.' + operation"
}
```

Palette tools are ordinary first-level tools. `blueprint_graph_palette` must
not hide its normal workflow fields simply because some are optional.

## Relationship To Resources

Resources are a good transport for full documents. `schema_inspect` is a small
structured retrieval tool on top of those documents.

The preferred architecture is:

- documents define design intent and schemas
- resources expose complete documents when the client supports them well
- `schema_inspect` returns focused machine-readable slices for agent runtime use

This keeps `tools/list` small while still giving agents a reliable way to avoid
guessing nested schemas.

## Boundaries

`schema_inspect` should not:

- list UE palette entries
- search Blueprint nodes
- infer what operation the agent should perform
- execute edits
- maintain a curated node table
- duplicate UE behavior

It may:

- list documented operations for one tool
- return one operation's JSON schema
- return concise examples
- return expected error codes
- point to the source document section

## Error Codes

Suggested errors:

| Code | Meaning |
| --- | --- |
| `UNKNOWN_DOMAIN` | The requested domain is not documented. |
| `UNKNOWN_TOOL` | The requested tool has no schema documentation. |
| `UNKNOWN_OPERATION` | The requested operation is not documented for that tool. |
| `UNSUPPORTED_INCLUDE` | The requested include section is not supported. |

Errors should include available domains, tools, or operations when that helps
the agent recover without guessing.

## Initial Blueprint Coverage

The first implementation should cover:

- `blueprint_graph_edit` operation index
- core graph edit commands
- secondary graph edit commands as non-core entries
- `blueprint_graph_palette` entry and `addFromPalette` relationship
- `blueprint_member_edit` operation index
- common member edit request schemas for variable, function, macro, dispatcher,
  event, and component operations

Other Blueprint tools should only be added if their first-level schema becomes
too large or too polymorphic to remain clear in `tools/list`.
