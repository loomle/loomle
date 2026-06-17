# Loomle Graph Lang SDK Experiment

This directory is an experimental TypeScript scaffold for a Loomle Graph Lang
(LGL) SDK. It is not part of Loomle's public protocol, release package, MCP
manifest, or Unreal Engine bridge.

The experiment exists to validate whether a compact graph text format can become
the SDK-facing graph operation layer for Loomle. LGL is treated as the public
SDK contract for graph query and graph patching. Full graph snapshots are
cache/offline primitives requested through empty-body queries rather than a
separate public snapshot method. Any parser or graph model is an implementation
detail inside the SDK, not the design center.

## Current Scope

- Define the TypeScript SDK facade that tools, MCP, and CLI code would call.
- Define the adapter contract that lets Blueprint, Material, PCG, and future
  graph systems implement LGL behavior.
- Preserve LGL syntax docs and examples as discussion material before
  implementation.
- Keep Unreal-specific behavior behind a Blueprint adapter; the SDK surface
  should not expose UE bridge internals directly.

## Documents

- `docs/SDK_DESIGN.md`: SDK facade, adapter contract, diagnostics, and result
  types.
- `docs/OBJECT_MODEL.md`: Parsed `LglObject` model for `Target`, `Graph`, nodes,
  pins, edges, and layout.
- `docs/BLUEPRINT_ADAPTER.md`: Blueprint adapter responsibilities and how it
  maps LGL to existing Loomle/UE capabilities.
- `docs/TOOL_SURFACE.md`: How MCP/CLI tools wrap the SDK.
- `docs/LGL_SPEC.md`: Current graph, query, patch, pin, and layout text forms.
- `docs/PATCH_MODEL.md`: Patch execution, dry-run, and layout mutation boundary.
- `docs/EXAMPLE_SOURCES.md`: Sources behind the Blueprint example set.
- `schema/lgl-object.schema.json`: Draft JSON Schema for normalized LGL object
  compatibility. The current file starts with foundation types and will expand
  toward the full object model.

## Commands

Install dependencies from this directory, then run:

```sh
npm run build
```

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
