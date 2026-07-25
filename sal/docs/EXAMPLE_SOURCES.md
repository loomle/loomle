# Example Notes

The files under `examples/blueprint/` are contract examples, not serialized
Blueprint exports. They exercise the confirmed SAL Text shapes:

- ordered Node, Pin, Edge, and Comment readback;
- Blueprint `summary`;
- plural Node search and exact Target-relative references;
- execution-flow traversal;
- ordered Graph Patch, `insert`, compile, and save;
- Widget tree readback;
- Palette and `with schema` discovery.

Text after `---` is one possible result and is not part of the request. Current
examples use brace ObjectExpr, flat `target { domain: ... }`, Target-relative
native identity paths, and explicit related Target handoffs. Real UE readback
must use the actual native Class Paths, GUIDs, owner scopes, Pin names, fields,
values, and Palette entries returned by the executor. Placeholders exercise
parsing, formatting, ordering, and workflow only.

Each shown result models the first canonical MCP Result Text block. Mutation
metadata and diagnostics, when relevant, belong to later independent
SAL-comment text blocks and are never appended after terminal `no_objects`.
