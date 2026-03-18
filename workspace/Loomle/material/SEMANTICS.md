# Material Semantics

This file explains how Material graph semantics are realized in practice.

Use it after `GUIDE.md` when you need help choosing the right node family,
understanding which pins matter, or avoiding common Material graph mistakes.

## Core Model

Material graphs are expression graphs, not exec graphs.

Important rule:

- there is no Blueprint-style exec flow here
- meaning comes from value flow into material properties and intermediate
  expressions
- the root sink matters more than local visual proximity

## Root Model

`__material_root__` is the graph sink for the owning Material.

How to realize the semantics:

- treat `__material_root__` as a special sink, not a normal expression node
- root property names such as `Base Color` or `Roughness` should be confirmed by
  readback before wiring
- do not guess unnamed output pins on upstream expressions; confirm them through
  `graph.query`

## Node Families

### Constant And Parameter Nodes

Examples:

- scalar parameter
- vector parameter
- texture parameter
- literal constants

How to realize the semantics:

- use parameters when the value should remain externally adjustable
- use constants when the value is local and fixed
- confirm parameter identity and type before wiring downstream consumers

### Math Nodes

Examples:

- `Add`
- `Multiply`
- `Lerp`
- `OneMinus`
- `Saturate`

How to realize the semantics:

- use these to form expression chains that eventually feed a root sink or a
  function output
- for named inputs like `A`, `B`, and `Alpha`, confirm pin names instead of
  guessing by memory

### Texture Sampling Nodes

Examples:

- texture sample
- texture parameter plus sample chain

How to realize the semantics:

- a texture parameter represents the asset input
- a texture sample performs the read
- if UV input matters, verify the exact UV pin and source connection after
  mutation

### Material Function Call Nodes

Examples:

- `MaterialFunctionCall`

How to realize the semantics:

- treat `childGraphRef` as the preferred handle for the referenced function
  graph
- do not rebuild function addresses manually when LOOMLE already returned a
  child graph ref
- confirm the function asset identity before wiring follow-up pins

## Important Distinctions

### Root Sink vs Ordinary Expression Nodes

- root properties live on `__material_root__`
- ordinary expression nodes feed other expressions or a root property

If the task says "connect to Base Color", think root sink, not just "connect to
the nearest node on the right".

### Parameter vs Constant

- a parameter exposes a named adjustable input to the Material
- a constant is only a local expression value

Do not swap them casually when the Material interface matters.

### Texture Parameter vs Texture Sample

- a texture parameter represents the asset input
- a texture sample performs the read

Do not treat a texture parameter as if it were already sampled color data.

### Material Root vs Material Function Subgraph

- `__material_root__` belongs to the owning Material graph
- `MaterialFunctionCall` may point to a separate function graph through
  `childGraphRef`

Be explicit about which graph you are editing before mutating.

## Mutation Style

Prefer direct graph edits over planner-specific payloads.

Before wiring:

- confirm root pin names with `graph.query`
- confirm intermediate output pin names with `graph.query`
- confirm function graph identity when `MaterialFunctionCall` is involved

## Layout Style

- material root is the right-side sink
- sink expressions should sit immediately to the left of the root
- upstream expressions should expand leftward by dependency depth
- prefer local layout with `scope="touched"`
- do not use layout as proof that the graph is correct
