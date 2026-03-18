# Blueprint Semantics

This file explains how Blueprint graph semantics are realized in practice.

Use it after `GUIDE.md` when you need help choosing the right node family,
understanding which pins matter, or avoiding common Blueprint graph mistakes.

## Core Model

Blueprint graphs have two different flows:

- `exec` flow controls when work happens
- `data` flow carries values into and out of nodes

Important rule:

- do not confuse a valid data connection with a valid exec path
- do not assume a visually nearby node is part of the same exec chain

## Node Families

### Event And Entry Nodes

Examples:

- `Event BeginPlay`
- custom events
- function entry nodes

How to realize the semantics:

- preserve their downstream interfaces when doing a local rewrite
- avoid replacing an entrypoint when a smaller downstream patch is enough

Validation clues:

- confirm the entry node still feeds the intended downstream exec pin

### Control-Flow Nodes

Examples:

- `Branch`
- `Sequence`
- `Delay`
- `DoOnce`

How to realize the semantics:

- the key semantics live in exec pins, not data pins
- verify the exact exec pins after mutation
- keep `True` and `False` or later sequence outputs visually separated

Validation clues:

- confirm the exact upstream exec pin
- confirm the exact downstream exec pins

### Variable Nodes

Examples:

- variable `Get`
- variable `Set`

How to realize the semantics:

- the key semantics live in the exact variable name and variable type
- `Set` usually participates in exec flow and data flow
- `Get` participates in data flow only

Validation clues:

- confirm the exact variable name before mutating
- for `Set`, verify both the exec path and the data value input
- for `Get`, verify the output data type before wiring it downstream

### Utility Nodes

Examples:

- reroutes
- comments
- debug print

How to realize the semantics:

- use reroutes to clarify wires, not to hide broken logic
- use comments to keep a local rewrite readable

## Important Distinctions

### `Branch` vs Data Selection

- `Branch` splits exec flow
- it does not choose between two data values

If the task is about which code path runs, think `Branch`.
If the task is about which value is passed, think data wiring instead.

### `Get` vs `Set`

- `Get` reads a variable value into data flow
- `Set` writes a variable value and usually participates in exec flow

Do not treat them as symmetric replacements.

### `DoOnce`

- `DoOnce` is a standard Blueprint macro-based control-flow shape
- treat it as a real control-flow node with stateful behavior
- verify it in the graph by readback; do not assume a synthetic lightweight node

## Mutation Style

Prefer direct graph edits over planner-specific payloads.

Use:

- `addNode.byClass` when you already know the class path
- `addNode.byAction` when action discovery is the safer creation path for the
  current graph context
- explicit `connectPins` and `disconnectPins` when restoring or reshaping local
  flow

When a node depends on pin context:

- inspect the current graph first
- wire it explicitly
- re-query rather than assuming the intended pin shape

## Layout Style

- keep exec flow readable from left to right
- keep `True` and `False` fanout separated
- prefer local layout with `scope="touched"`
- do not use layout as proof that the graph is correct
