# Loomle Graph Lang Experiment

This directory is an experimental TypeScript prototype for Loomle Graph Lang
(LGL). It is not part of Loomle's public protocol, release package, MCP
manifest, or Unreal Engine bridge.

The experiment exists to validate whether a compact, schema-driven graph text
format can improve agent workflows before any Loomle 0.7 integration decision.

## Current Scope

- Parse a small LGL graph form into Graph IR.
- Parse a small LGL patch form into Patch IR.
- Parse explicit `use` bindings for palette queries, palette entry ids, component
  context, from-pin context, and simple `where` disambiguation clauses.
- Print parsed documents back to LGL-like text.
- Compile patch operations to abstract edit commands shaped like future adapter
  input.
- Keep Unreal-specific behavior out of this package until the language and IR
  model are stable.
- Draft query and pin-detail syntax in docs/examples before implementation.

## Commands

Install dependencies from this directory, then run:

```sh
npm test
```

Parse an example:

```sh
npm run parse -- examples/blueprint/begin-play-print.lgl
```

Compile a patch example:

```sh
npm run compile -- examples/blueprint/insert-delay.patch.lgl
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
- query examples for nodes, edges, paths, subgraphs, and node details

The examples intentionally use the proposed compact inspect form:

```txt
begin@7A9D = EventBeginPlay() at (0, 0) size (180, 80)
delay@81EF = Delay(1.0) at (320, 0) size (200, 100)
print@C2B0 = PrintString("Ready") at (640, 0) size (220, 120)

begin -> delay -> print
```

Current TypeScript parser support may lag these examples while the language is
being discussed.

Compact `graph` examples are readback snapshots and do not include `use`
bindings. `use` appears in `patch` examples because patches need palette
bindings to create new nodes.

Layout readback may appear inline on node lines with `at` and `size`, and on pin
detail lines with `anchor`. Patch layout mutation is currently limited to
`move node to (...)` and `move node by (...)`.

Palette binding examples:

```txt
use PrintString from palette "Print String" where function = "/Script/Engine.KismetSystemLibrary.PrintString"
use TriggerBeginOverlap from palette "On Component Begin Overlapped" context component "Trigger"
use GetDisplayName from palette "Get Display Name" context from breakHit.HitActor
use StablePrint from palette entry "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"
```

## Non-Goals

- This is not a Blueprint DSL.
- This does not replace `blueprint_graph_edit`, `material_graph_edit`, or
  `pcg_graph_edit`.
- This does not mutate Unreal assets.
- This does not define a stable public Loomle interface.
