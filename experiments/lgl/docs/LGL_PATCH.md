# LGL Patch Semantics

Patch documents edit one graph. They may bind palette entries, define local node
specs, change fields, create nodes, edit links, move nodes, and remove nodes.

Patch execution is adapter-owned. The text parser only produces normalized
patch objects; adapters validate graph schemas, palette ids, pins, links, and
native edit legality.

## Header And Dry Run

```txt
patch blueprint("/Game/BP_Door"/EventGraph) dry run
```

Dry run is part of the document, not an SDK side option. Adapters must follow
Loomle's mutation dry-run contract: parse, resolve, validate, and compute
changes through the same path as a real mutation, then stop before applying
changes.

## Bindings

Palette bindings name target creation sources:

```txt
Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
```

Node specs use call syntax:

```txt
delay = Delay({Duration: 1.0})
message = "Game Started"
```

Bindings are local to one patch document. Adapter phases classify bindings as
palette entries, node specs, values, or unsupported forms.

## Set

```txt
set print.InString = "Game Started"
```

`set` targets one node field or editable pin default. The adapter validates that
the field exists and that the value is legal for the target graph.

## Add

```txt
add delay
add begin.Then -> delay.Exec
add delay.Completed -> print.Exec
```

`add` creates one node from a local node spec. It may connect at most one side of
the new node. Use `insert` for two-sided replacement.

## Insert

```txt
insert begin.Then -> delay.Exec/Completed -> print.Exec
```

`insert` creates the middle node, requires an existing direct link from the
first pin to the last pin, removes that direct link, and connects the expanded
chain through the new node.

## Connect

```txt
connect begin.Then -> print.Exec
connect begin.Then -> delay.Exec/Completed -> print.Exec
```

A bare edge line inside a patch is shorthand for `connect`.

## Disconnect

```txt
disconnect threshold.ReturnValue -> branch.Condition
disconnect branch.Condition
```

The edge form removes one link. The pin form removes all links attached to one
pin.

## Remove

```txt
remove print
```

`remove` deletes one node and its attached links. It does not reconnect or heal
surrounding graph flow.

## Move

```txt
move delay to (320, 0)
move print by (240, 0)
```

`to` uses an absolute graph-editor canvas position. `by` uses a relative delta.

Node size and pin anchors are readback metadata in the current language.
Patches do not resize ordinary nodes or move pins directly. Comment boxes and
reroute nodes need separate design before they become patch targets.

## Reconstruct

```txt
reconstruct nodeName preserve links
```

`reconstruct` is a target-specific maintenance escape hatch. Adapters should
perform reconstruction automatically when supported `add` or `set` operations
need target-owned pins or metadata refreshed.
