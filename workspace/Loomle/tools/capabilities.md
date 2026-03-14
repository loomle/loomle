# LOOMLE Graph Capabilities

Use these as the default mental model inside an installed LOOMLE project.

## Core graph loop

1. `graph.query`
2. `graph.mutate`
3. `layoutGraph(scope="touched")`
4. `graph.query` again when verification matters

## Layout rules

- Blueprint:
  - exec flow reads left to right
  - `True` and `False` branches fan out recursively
- Material:
  - `__material_root__` is the right-side sink
  - terminal expressions stay immediately to the left of the root
- PCG:
  - source nodes stay on the left
  - downstream stages move right
  - parallel branches separate vertically

## Mutate expectations

- New nodes should land in a reasonable local position even when `position` is omitted.
- `layoutGraph(scope="touched")` is the default cleanup step after a local logic block.
- Global reflow should be explicit and rare.
