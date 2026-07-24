---
layout: default
title: Mutations and Finalization
parent: Core Concepts
nav_order: 4
description: Dry-run, apply, readback, transactions, compile, and save in Loomle.
---

# Mutations and Finalization

Loomle keeps authored changes, verification, compilation, and persistence
explicit.

## Ordered Patch

A Patch is one ordered unit:

```text
patch eventGraph dry run
print = node(palette: "palette-entry-id")
add print
move print to (640, 320)
```

Statements execute in written order. The complete Patch is parsed, resolved,
validated, and planned before live mutation begins.

## Dry Run Uses the Real Edit Path

The SAL Patch header carries dry-run state:

```text
patch eventGraph dry run
```

It follows the same domain edit path through parsing, identity resolution,
validation, and planning, then stops before applying changes to live authored
state.

Review:

- whether the Patch is valid;
- resolved references and bindings;
- ordered planned operations;
- effects and diagnostics; and
- the returned diff when that interface provides it.

If valid, send the same authored Patch again with dry-run state removed from
the header.

## Apply Is Transactional In Memory

Mutable interfaces use their native UE transaction and preflight model. A
failed authored batch must not leave a partially accepted edit sequence in
live in-memory state.

Exact behavior comes from the active interface card. External package save is
a separate boundary and cannot be rolled back as part of an in-memory
transaction.

## Read Back Before Finalizing

After apply, query the changed object or its owner again. Readback proves the
actual current UE state and gives the stable ids needed for follow-up work.

Do not treat a successful tool call as a substitute for inspecting the result.

## Compile and Save Are Explicit

Graph and Widget edits do not automatically compile or save their owning
Blueprint:

```text
patch door
compile
save
```

StateTree uses its own terminal Patch on the asset target:

```text
patch omle
compile
save
```

Terminal finalization forms are defined by the owner interface. Do not mix
authored source edits with compile/save unless that interface explicitly
allows it.

## Save Does Not Mean Compile

A save operation persists the exact owning Package. It does not imply
validation or compile. Saving stale compiled state may be valid but must be an
explicit choice.

If save fails after a successful authored edit or compile, the in-memory object
can remain dirty and unsaved. Inspect the returned diagnostics and retry only
after resolving the external package problem.
