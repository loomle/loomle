# Patch Model Draft

LGL is patch-first. Complete graph generation is useful, but agent
collaboration depends on small, reviewable edits.

## Pipeline

```txt
LGL patch
  -> parse patch objects
  -> palette/schema binding resolution
  -> schema validation
  -> adapter command plan
  -> dry run or apply
```

## Dry Run

When this experiment is integrated with Loomle, dry run must follow
`docs/MUTATION_DRY_RUN_CONTRACT.md`: parse, resolve, validate, and plan through
the same path as a real mutation, then stop before applying changes.

## Adapter Output

A future Blueprint adapter can map validated patch objects to
`blueprint_graph_edit` command batches without making LGL itself depend on
Blueprint internals.

Palette bindings are part of the command plan. They are not executable by
themselves; the adapter resolves them before turning `add` commands into
`addFromPalette` operations.

## Layout Mutation

Layout mutation belongs in `patch` documents. The first version only supports
moving graph nodes:

```txt
move delay to (320, 0)
move print by (240, 0)
```

`to` uses an absolute graph-editor canvas position. `by` uses a relative canvas
delta.

Do not include resize operations in the first version. Ordinary Blueprint node
size is editor-derived readback metadata. Comment boxes and reroute nodes need a
separate design before LGL exposes their mutation behavior.

Pin anchors are readback-only layout information. Patches should not attempt to
move pins directly.
