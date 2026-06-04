# Tool Schema Exposure Contract

Loomle keeps the shared manifest as the source of truth for every public tool
contract. MCP `tools/list` must remain directly usable: agents should not have
to know a hidden convention before they can make a normal first-level tool call.

## Intent

The first-level tool input schema is the live call contract. It should describe
the accepted top-level request shape clearly enough that an agent can call the
tool without guessing.

`schema_inspect` exists for details that are too large or too polymorphic for
the first-level schema:

- operation-specific payloads inside edit tools
- full or verbose output schemas
- examples, error notes, and source documentation slices
- future large secondary schema details

It must not be required just to learn ordinary first-level input fields such as
`assetPath`, `graph`, `query`, `view`, `limit`, or `dryRun`.

## `tools/list`

Every listed tool must include an MCP-valid `inputSchema` with
`type: "object"`.

Public MCP tool names must be Claude-safe: `^[a-zA-Z0-9_-]{1,64}$`.
Use underscore names such as `blueprint_graph_inspect` in `tools/list`,
documentation, tests, and agent-facing examples. Dotted names remain valid only
inside Loomle implementation boundaries such as bridge RPC dispatch tools,
transform identifiers, and operation names like `variable.create`.

`tools/list` exposes the full first-level input schema for every public tool.
This includes:

- top-level `required`
- all top-level properties
- nested schema needed to form a valid first-level request envelope
- optional fields that shape normal workflows, including palette query and
  context fields
- mutation controls such as `dryRun` and `expectedRevision`

`tools/list` should not inline every operation-specific payload for tools whose
request contains a second-level operation envelope. For those tools, the
first-level schema must keep the envelope and selector, then provide an explicit
schema inspection hint.

Examples of required first-level envelopes:

- `blueprint_graph_edit.commands[].kind`
- `material_graph_edit.commands[].kind`
- `pcg_graph_edit.commands[].kind`
- `widget_tree_edit.commands[].kind`
- `blueprint_node_edit.operation` and `args`
- `blueprint_member_edit.memberKind`, `operation`, and `args`
- `pcg_parameter_edit.operation` and `args`

Palette tools are ordinary first-level tools. Their input schemas must stay in
`tools/list`; they must not collapse to empty property objects simply because
many fields are optional.

## Structured Hints

When a first-level input schema contains an operation-specific envelope, the
tool must expose a machine-readable hint that tells the agent how to fetch the
second-level schema.

The exact manifest field name may evolve, but the public data should carry this
information:

```json
{
  "schemaHints": [
    {
      "purpose": "operation_schema",
      "schemaTool": "schema_inspect",
      "domain": "blueprint",
      "tool": "blueprint_graph_edit",
      "operationFrom": "commands[].kind",
      "include": ["operation", "examples", "errors", "notes"]
    }
  ]
}
```

For member edit operations whose schema name is composed from multiple fields:

```json
{
  "schemaHints": [
    {
      "purpose": "operation_schema",
      "schemaTool": "schema_inspect",
      "domain": "blueprint",
      "tool": "blueprint_member_edit",
      "operationFrom": "memberKind + '.' + operation"
    }
  ]
}
```

Natural-language descriptions may repeat these instructions, but prose is not
the contract. Agents should not have to parse truncated descriptions to discover
`schema_inspect`.

## `schema_inspect`

`schema_inspect` reads the complete contract from the manifest:

- `include: ["input"]` returns the full tool-level `inputSchema`
- `include: ["output"]` returns the full tool-level `outputSchema`
- `include: ["operation"]` returns the selected operation contract as
  `operationSchema`
- `include: ["examples"]`, `["errors"]`, and `["notes"]` return operation
  support material when available

The public include name `operation` avoids the ambiguous old term `schema`.
The manifest may still store operation contracts under an internal `schema`
field.

For tools with operation-specific payloads, omitting `operation` returns a
compact operation index:

```json
{
  "domain": "blueprint",
  "tool": "blueprint_graph_edit",
  "operations": [
    {
      "name": "addFromPalette",
      "category": "core",
      "summary": "Execute one selected blueprint_graph_palette entry."
    }
  ]
}
```

## Examples

### `blueprint_graph_palette`

`blueprint_graph_palette` is a first-level tool. `tools/list` should expose its
input fields directly, including `assetPath`, `graph`, `query`,
`contextSensitive`, `fromPins`, `limit`, and `offset`.

The selected palette entry is output data. The edit command that consumes it is
operation-specific, so `blueprint_graph_edit.addFromPalette` remains documented
through `schema_inspect`.

### `blueprint_graph_edit`

`blueprint_graph_edit` keeps its first-level input schema in `tools/list`,
including `assetPath`, `graph`, mutation controls, and the command envelope.
The command payload behind each `commands[].kind` is second-level and must be
looked up through `schema_inspect`.

### `blueprint_member_edit`

`blueprint_member_edit` keeps `assetPath`, `memberKind`, `operation`, `args`,
and mutation controls in `tools/list`. The exact `args` schema is selected by
`memberKind + "." + operation` and is returned by `schema_inspect`.

## Audit Expectations

Tests should verify both surfaces:

- `tools/list` exposes complete first-level input schemas
- palette, inspect, query, compile, and simple edit tools remain directly
  callable from `tools/list`
- tools with second-level operation payloads expose the selector envelope and a
  structured schema hint
- operation-specific payload schemas do not leak into `tools/list`
- `schema_inspect` can retrieve operation, full input, output, examples, errors,
  and notes
- Rust native and Python MCP expose the same public contract
