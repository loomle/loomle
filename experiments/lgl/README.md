# Loomle Graph Lang SDK Experiment

This directory is an experimental TypeScript package for a Loomle Graph Lang
(LGL) SDK. It is not part of Loomle's public protocol, release package, MCP
manifest, or Unreal Engine bridge.

The experiment exists to validate whether a compact graph text format can become
the SDK-facing graph operation layer for Loomle. LGL is treated as the public
SDK contract for graph query and graph patching. Full graph snapshots are
cache/offline primitives requested through empty-body queries rather than a
separate public snapshot method. Any parser or graph model is an implementation
detail inside the SDK, not the design center.

## Current Scope

- Provide the `createLgl({ adapters })` facade used as `lgl.query(text)` and
  `lgl.patch(text)`.
- Parse accepted LGL text into normalized `LglObject` values.
- Format normalized objects back to LGL text.
- Validate normalized objects against `schema/lgl-object.schema.json`.
- Generate TypeScript object-model types from the schema.
- Exercise the adapter contract with `createMemoryGraphAdapter`.
- Keep Blueprint examples under parser/formatter conformance tests.
- Keep Unreal-specific behavior behind a Blueprint adapter; the SDK surface
  should not expose UE bridge internals directly.

## Documents

- `docs/SDK_DESIGN.md`: SDK facade, adapter contract, diagnostics, and result
  types.
- `docs/OBJECT_MODEL.md`: Parsed `LglObject` model for `Target`, `Graph`, nodes,
  pins, edges, and layout.
- `docs/LGL_TEXT.md`: Current accepted LGL document, graph, pin, edge, value,
  and palette text forms.
- `docs/LGL_QUERY.md`: Query forms and result semantics.
- `docs/LGL_PATCH.md`: Patch forms, dry-run intent, and mutation semantics.
- `docs/BLUEPRINT_ADAPTER.md`: Blueprint adapter responsibilities and how it
  maps LGL to existing Loomle/UE capabilities.
- `docs/TOOL_SURFACE.md`: How MCP/CLI tools wrap the SDK.
- `docs/EXAMPLE_SOURCES.md`: Sources behind the Blueprint example set.
- `schema/lgl-object.schema.json`: JSON Schema contract for normalized LGL
  objects and adapter/RPC results.

## Commands

Install dependencies from this directory, then run:

```sh
npm run build
npm test
```

`npm test` validates the normalized object fixtures in `fixtures/object` against
`schema/lgl-object.schema.json` and verifies that rejected boundary examples in
`fixtures/object-invalid` fail schema validation.

`npm run generate:types` regenerates TypeScript object-model types from
`schema/lgl-object.schema.json`. `npm test` runs `npm run check:generated` first
so schema changes fail unless the generated types are updated.

`npm run test:examples:extended` runs the extended Blueprint example corpus on
its own; the same check is included in the default `npm test` gate.

`npm run test:examples:reference` audits the larger Blueprint reference examples
without making them part of the default conformance gate.

The current parser/formatter is a minimal closed-loop implementation for the
core examples. It covers document headers, graph node and edge lines, simple
query forms, palette bindings, and a small patch set including `insert` and
`move`. It covers the current Core and Extended Blueprint examples, with larger
Reference examples available through `npm run test:examples:reference`.

The current facade is `createLgl({ adapters })`, returning an object used as
`lgl.query(text)` and `lgl.patch(text)`. It parses LGL text, dispatches by
target domain to an adapter, and formats adapter `ObjectResult` values back to
LGL text.

`createMemoryGraphAdapter` is an in-memory SDK test fixture for exercising the
adapter contract without Unreal Editor. It is useful for query/patch closed-loop
tests, but it is not a Blueprint semantics implementation.

## Example Set

Blueprint examples currently cover:

- BeginPlay -> Delay -> PrintString
- Branch with data-flow condition
- overlap-gated actor input
- launchpad overlap -> Cast -> LaunchCharacter
- per-tick LineTraceByChannel -> PrintString
- overlap-driven door Timeline
- input action -> socket transform -> SpawnActorFromClass
- small patch examples for inserting Delay and Branch guard nodes
- query examples for nodes, paths, surrounding context, palette discovery, and
  detailed node output

The examples intentionally use the proposed stable inspect form:

```txt
begin@7A9D: EventBeginPlay() {at: [0, 0], size: [180, 80]}
delay@81EF: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
print@C2B0: PrintString({InString: "Ready"}) {at: [640, 0], size: [220, 120]}

begin.Then -> delay.Exec/Completed -> print.Exec
```

Compact `graph` examples are readback snapshots and do not include palette
bindings. Patch examples bind stable palette entry ids when they create new
nodes.

Layout readback appears as trailing object metadata on node and pin detail
lines, such as `{at: [x, y], size: [w, h]}`, `{anchor: [x, y]}`, and
`{1.0, anchor: [x, y]}` for a pin default plus layout. Patch layout mutation is
currently limited to `move node to (...)` and `move node by (...)`.

Palette binding examples:

```txt
PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"})
TriggerBeginOverlap = palette({id: "palette:blueprint:component_event:/Game/BP_Door.Trigger.OnComponentBeginOverlap"})
GetDisplayName = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.GetDisplayName"})
```

## Non-Goals

- This is not a Blueprint DSL.
- This does not expose a public Graph IR.
- This does not require callers to understand parser internals.
- This does not require maintaining Rust and TypeScript versions of the same
  client.
- This does not mutate Unreal assets.
- This does not define a stable public Loomle interface.
