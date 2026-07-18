# Example Notes

The files under `examples/blueprint/` are contract examples, not serialized
Blueprint exports. They exercise the confirmed SAL Text shapes:

- ordered Node, Pin, Edge, and Comment readback;
- Blueprint `summary`;
- plural Node search and exact typed references;
- execution-flow traversal;
- ordered Graph Patch, `insert`, compile, and save;
- Widget tree readback;
- Palette and `with schema` discovery.

Text after `---` is one possible Object Text response and is not part of the
request. Real UE readback must use the actual native Class Paths, GUIDs, Pin
names, fields, values, and Palette entries returned by the executor. The SDK
examples use stable-looking placeholders only to test parsing, formatting,
ordering, and agent workflow.
