# Tool Schema Exposure Contract

Loomle keeps the shared manifest as the source of truth for every tool contract,
but MCP `tools/list` is not the place to expose every detail by default.

## Intent

Agent startup context should describe what tools exist and how to make a basic
call without embedding every nested request and response shape. Full contracts
remain available on demand through `schema_inspect`.

## `tools/list`

Every listed tool must include an MCP-valid `inputSchema` with
`type: "object"`.

Public MCP tool names must be Claude-safe: `^[a-zA-Z0-9_-]{1,64}$`.
Use underscore names such as `blueprint_graph_inspect` in `tools/list`,
documentation, tests, and agent-facing examples. Dotted names remain valid only
inside Loomle implementation boundaries such as bridge RPC dispatch tools,
transform identifiers, and operation names like `variable.create`.

`schema_inspect` is the bootstrap exception and exposes its full input schema in
`tools/list`.

All other tools expose a thin input schema:

- keep top-level `type: "object"`
- keep top-level `required`
- keep required fields
- keep high-signal optional selector fields such as `kind`, `view`,
  `operation`, `memberKind`, `action`, `fn`, `tool`, and `domain`
- keep only shallow property facts needed for selection: `type`, `enum`,
  `const`, `minLength`, `default`, and short descriptions
- represent nested objects and arrays as shallow placeholders

Thin schemas must not expose nested `properties`, `$defs`, `oneOf`, `anyOf`, or
operation-specific command payloads.

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

## Audit Expectations

Tests should verify both surfaces:

- `tools/list` stays protocol-valid and thin by default
- `schema_inspect` can retrieve full input, output, and operation schemas
- Rust native and Python MCP expose the same public contract
