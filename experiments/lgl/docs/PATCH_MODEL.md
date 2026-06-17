# Patch Model Draft

LGL is patch-first. Complete graph generation is useful, but agent
collaboration depends on small, reviewable edits.

## SDK Flow

```txt
LGL patch
  -> SDK parse and source mapping
  -> adapter palette/schema binding resolution
  -> adapter validation
  -> adapter change computation
  -> dry run response or apply
```

## Dry Run

When this experiment is integrated with Loomle, dry run must follow
`docs/MUTATION_DRY_RUN_CONTRACT.md`: parse, resolve, validate, and compute
changes through the same path as a real mutation, then stop before applying
changes.

## Adapter Output

The SDK returns LGL-oriented diagnostics and computed changes. A Blueprint
adapter can map validated patch operations to existing bridge command batches
without making the SDK facade expose Blueprint internals.

Palette bindings are part of patch execution. They are not executable by
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
