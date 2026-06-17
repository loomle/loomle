# Blueprint Adapter

## Intent

The Blueprint adapter makes LGL work against Unreal Blueprint graphs while
preserving UE semantics. It should use existing Loomle bridge capabilities and
UE creation paths instead of inventing its own Blueprint node model.

This document is a future adapter design. The current LGL experiment closes the
SDK-level loop with a memory adapter; it does not implement the Unreal
Blueprint adapter.

## Responsibilities

The adapter owns:

- exporting Blueprint graph snapshots as compact LGL `graph` documents
- executing LGL `query` documents over Blueprint graph data
- executing palette queries for Blueprint creation entries
- resolving `Name = palette({id: "entry-id"})` bindings through UE palette/actions
- validating and applying `patch` documents through Blueprint graph mutation paths
- mapping UE and bridge errors into LGL diagnostics

## RPC Boundary

The TypeScript LGL package parses and normalizes LGL text before crossing into
Unreal. The UE bridge should receive normalized `LglObject` JSON, not raw LGL
text.

Blueprint semantics stay inside Unreal:

- resolve assets and graphs
- query Blueprint palette/action databases
- resolve palette entry ids
- execute UE node spawners
- validate node types, pins, fields, pin directions, and links
- inspect current graph edges for operations such as `insert`
- apply graph edits through UE editor APIs and transactions
- perform dry-run planning through the same validation path as real mutation

The adapter may translate normalized `LglObject` JSON into existing operation-based
bridge calls internally, but that translation is an implementation detail.

## Query Cache

Full graph snapshots are cache/offline primitives reached through an empty
query body, not a separate public SDK method:

```txt
query blueprint("/Game/BP_Door"/EventGraph)
```

They may produce compact LGL:

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

begin@A001: EventBeginPlay() {at: [0, 0], size: [180, 80]}
delay@A002: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
print@A003: PrintString({InString: "Ready"}) {at: [640, 0], size: [220, 120]}

begin.Then -> delay.Exec/Completed -> print.Exec
```

Compact graph snapshots should not emit palette bindings. The graph already
exists. Palette bindings are for patch-time creation.

Large snapshots should normally be written to cache/workspace files and
referenced by path. Agent-facing reads should prefer targeted `query`
documents, which return small LGL snippets.

## Query

`query` should accept LGL query text:

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find surrounding around branch depth 2
```

Query results should be LGL-first:

- `find nodes` returns node lines.
- `find path` returns a compact graph/path snippet.
- `find surrounding` returns a compact `graph` snippet.
- `find node` returns a compact `graph` snippet with requested pin/default/layout lines.
- `find palette entry` returns palette entry documents for patch-time creation.

## Palette

Palette is a first-class graph editing concept. The Blueprint adapter exposes
palette through LGL query and patch documents. It is not a separate SDK method.

Palette entries are discovered with query:

```txt
query blueprint("/Game/BP_Door"/EventGraph)

find palette entry "Print String"
```

Patch documents bind stable palette entry ids after the patch header and before
creating nodes. They do not perform fuzzy palette search:

```txt
PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"})
```

For context-sensitive entries, the palette query result must carry a stable
entry id that encodes or restores the required context:

```txt
TriggerBeginOverlap = palette({id: "palette:blueprint:component_event:/Game/BP_Door.Trigger.OnComponentBeginOverlap"})
```

## Patch

`patch` resolves palette bindings, validates node/pin references, computes
changes, and applies them unless the document requests `dry run`.

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
add delay
disconnect begin.Then -> print.Exec
connect begin.Then -> delay.Exec/Completed -> print.Exec
move delay to (320, 0)
```

`insert` creates a node and replaces an existing direct link:

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

Single-link removal:

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

disconnect health.Value -> branch.Condition
```

Plain node removal:

```txt
patch blueprint("/Game/BP_Door"/EventGraph)

remove print
```

Adapter mapping:

- `insert` maps to `insertExec` when the affected pins are exec pins.
- `remove` maps to `removeNode`.

Node creation must go through UE palette/action spawners. The adapter may call
existing `blueprint_graph_palette` and `blueprint_graph_edit` logic internally,
but those are backend mechanisms, not the LGL-facing API.

Blueprint node reconstruction is target-specific maintenance. The adapter should
call the bridge's reconstruct behavior automatically when a supported add or set
operation needs UE to refresh pins or node-owned metadata. Manual LGL
`reconstruct node preserve links` remains available as an escape hatch for
diagnostic-guided repair, but it is not the normal editing path.

## Layout

Layout readback:

```txt
delay@A002: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
delay.Exec: exec in {anchor: [320, 24]}
```

Patch layout mutation starts with node movement only:

```txt
move delay to (320, 0)
move print by (240, 0)
```

Ordinary Blueprint node size and pin anchors are readback metadata in the first
version. Comment boxes and reroute nodes need separate design.

## Existing Tools

Existing Blueprint graph tools can remain as adapter internals or advanced
debug surfaces. In LGL mode, normal agent workflows should prefer:

- graph query
- palette search
- graph patch
