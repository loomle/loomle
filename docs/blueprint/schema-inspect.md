# Blueprint Schema Inspection

## Intent

Blueprint tools have nested operation schemas. Exposing all nested schemas in
`tools/list` wastes context and makes the first-level tool list harder for an
agent to scan.

Schema inspection provides a second-level lookup path. The first-level tool
schema stays small, and the agent asks for the exact operation schema only when
it needs that operation.

Not every Blueprint-facing tool should support schema inspection. Tools whose
first-level schema fully describes their request shape, such as `asset.create`,
`asset.inspect`, `asset.edit`, `blueprint.inspect`, `blueprint.class.inspect`,
and `blueprint.class.edit`, should stay self-contained in `tools/list`.
`schema.inspect` is reserved for tools with a deliberately compressed
operation envelope.

## Design Principle

The formal documentation is the source of truth.

A schema inspection tool may return structured slices of that documentation,
but it must not become a second business inventory or a separate model of
Blueprint behavior.

## Proposed Tool

`schema.inspect`

```json
{
  "domain": "blueprint",
  "tool": "blueprint.member.edit",
  "operation": "variable.create",
  "include": ["schema", "examples", "errors"]
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
        "enum": ["summary", "schema", "examples", "errors", "notes"]
      },
      "default": ["summary", "schema"]
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
  "tool": "blueprint.graph.edit",
  "operation": "addFromPalette",
  "category": "core",
  "summary": "Execute one selected blueprint.palette entry.",
  "schema": {},
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
  "tool": "blueprint.graph.edit",
  "operations": [
    { "name": "addFromPalette", "category": "core" },
    { "name": "connect", "category": "core" },
    { "name": "addCommentBox", "category": "annotation" }
  ]
}
```

## Agent Flow

For Blueprint graph node creation:

1. Call `blueprint.palette` to find a UE Action Menu entry.
2. Call `schema.inspect` with `tool="blueprint.graph.edit"` and
   `operation="addFromPalette"`.
3. Call `blueprint.graph.edit` with the returned command shape.

For pin editing:

1. Inspect the graph with `blueprint.graph.inspect`.
2. Call `schema.inspect` for `connect`, `disconnect`, `breakLinks`, or
   `setPinDefault`.
3. Call `blueprint.graph.edit`.

For Blueprint member editing:

1. Call `schema.inspect` with `tool="blueprint.member.edit"` and no operation
   to list supported `memberKind.operation` entries.
2. Call `schema.inspect` again with an operation such as `variable.create`,
   `event.addInput`, or `component.create`.
3. Call `blueprint.member.edit` using the returned top-level request shape.

## Relationship To Resources

Resources are a good transport for full documents. `schema.inspect` is a small
structured retrieval tool on top of those documents.

The preferred architecture is:

- documents define design intent and schemas
- resources expose complete documents when the client supports them well
- `schema.inspect` returns focused machine-readable slices for agent runtime use

This keeps `tools/list` small while still giving agents a reliable way to avoid
guessing nested schemas.

## Boundaries

`schema.inspect` should not:

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

- `blueprint.graph.edit` operation index
- core graph edit commands
- secondary graph edit commands as non-core entries
- `blueprint.palette` entry and `addFromPalette` relationship
- `blueprint.member.edit` operation index
- common member edit request schemas for variable, function, macro, dispatcher,
  event, and component operations

Other Blueprint tools should only be added if their first-level schema becomes
too large or too polymorphic to remain clear in `tools/list`.
