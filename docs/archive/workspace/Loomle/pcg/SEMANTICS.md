# PCG Semantics

This file explains how PCG graph semantics are realized in practice.

Use it after `GUIDE.md` when you need help choosing the right node family,
understanding which pins and parameters matter, or avoiding common PCG graph
mistakes.

This file explains usage semantics.

It does not replace:

- `catalogs/node-index.json` for the curated working set
- `catalogs/node-database.json` for the full static UE node inventory
- `examples/` for concrete mutate payloads

## Core Model

After you choose the intended node, realize it with primitive mutate
operations such as:

- `addNode.byClass`
- `connectPins`
- `disconnectPins`
- `setPinDefault`
- `layoutGraph`

Readback matters because actual pin names and nested settings often carry the
real meaning of the edit.

## Semantic Families

### `source`

Brings data into the graph.

Node names in this family:

- `Get Actor Data`
- `Get PCG Component Data`
- `Get Primitive Data`
- `Get Spline Data`
- `Get Landscape Data`
- `Get Texture Data`

Most common usage:

- `Get Actor Data` is the most common source node because it can read actor,
  component, or remote PCG output depending on mode
- place source nodes near the left edge
- read selector-backed nested settings after mutation

Most important parameters and wiring:

- actor filter / actor selector configuration
- mode selection
- output pin `Out`

Common confusion:

- `Get Actor Data` is broad and mode-sensitive
- `Get Spline Data` is a specialization and is usually clearer when the source
  is definitely spline data
- if you only need generated output from another PCG component, `Get PCG
  Component Data` is usually semantically tighter than a generic actor parse

### `create`

Synthesizes points or shapes from settings.

Node names in this family:

- `Create Points`
- `Create Points Grid`
- `Create Points Sphere`
- `Create Spline`

Most common usage:

- `Create Points` is the default left-side point generator when you want a
  direct synthetic source
- use create nodes as pipeline starters unless they are fed by a structural
  parent like `Loop` or `Subgraph`

Most important parameters and wiring:

- point count / density-style settings
- transform or bounds settings
- output pin `Out`

Common confusion:

- `Create Points` is generic
- `Create Points Grid` and `Create Points Sphere` are more specific shape
  generators and are usually better when the geometric intent is already known

### `sample`

Derives samples or subsets from an input.

Node names in this family:

- `Surface Sampler`
- `Spline Sampler`
- `Select Points`
- `Mesh Sampler`
- `Select Grammar`

Most common usage:

- `Surface Sampler` is the common terrain or surface distribution node
- `Spline Sampler` is the common path-driven distribution node
- `Select Points` is the common subsample node when you want to keep only a
  ratio of points

Most important parameters and wiring:

- sample source input pin `In`
- density / ratio / spacing settings
- output pin `Out`

Common confusion:

- `Select Points` is a sampling node, not a control-flow select node
- if the intent is "keep some points", think `sample`
- if the intent is "split matched and unmatched points into two outputs", think
  `filter`

### `transform`

Changes spatial or transform state.

Node names in this family:

- `Transform Points`
- `Projection`
- `Copy Points`
- `Apply Scale To Bounds`

Most common usage:

- `Transform Points` is the default point-transform adjustment stage
- `Projection` is the default landscape or surface projection stage

Most important parameters and wiring:

- for `Transform Points`, transform mode and scale/rotation/translation fields
- for `Projection`, nested settings under `projectionParams`
- standard flow is `In -> Out`

Common confusion:

- `Projection` is not just a cosmetic transform
- projection often changes what metadata is available downstream, so later
  filters can fail semantically even when topology looks correct

### `meta`

Writes or reshapes metadata.

Node names in this family:

- `Add Tag`
- `Attribute Noise`
- `Match And Set`
- `Point Match And Set`
- `Sort Attributes`
- `Create Attribute Set`

Most common usage:

- `Add Tag` is the simplest metadata-writing node and is often used to mark a
  branch for later whole-data routing
- `Attribute Noise` is common when you want controlled variation before a later
  filter or spawn

Most important parameters and wiring:

- attribute or tag names
- metadata write target
- output pin `Out`

Common confusion:

- `Add Tag` works at the data/tag level
- attribute-writing nodes work at the attribute/value level
- if downstream logic depends on a named attribute, confirm that the write
  actually shows up in `effectiveSettings` or readbacked data before moving on

### `predicate`

Computes a boolean-like condition.

Node names in this family:

- `Attribute Compare Op`
- `Attribute Boolean Op`
- `Point To Attribute Set`

Most common usage:

- use `Attribute Compare Op` when you want to compute a condition before a
  branch or select stage

Most important parameters and wiring:

- target attribute selection
- comparison operator
- output attribute naming when the predicate is written into data

Common confusion:

- a predicate computes a condition
- it does not itself behave like `route` or `filter`
- if you need a two-output split, you still need a route/filter style node
  downstream

### `branch`

Pure control-flow routing based on an existing decision.

Node names in this family:

- `Branch`
- `Switch`

Most common usage:

- use when the decision already exists as a boolean, enum, string, or integer

Most important parameters and wiring:

- controlling attribute or selector
- output pins such as `Output A`, `Output B`, or multiple switch outputs

Common confusion:

- `Branch` is not `Filter Data By Attribute`
- `Branch` consumes an existing decision
- `Filter Data By Attribute` computes its own decision from data and then routes

### `select`

Chooses one upstream source among alternatives.

Node names in this family:

- `Boolean Select`
- `Select (Multi)`
- `Proxy`

Most common usage:

- use when you want one chosen upstream source to continue downstream

Most important parameters and wiring:

- the selection condition
- multiple candidate input pins
- the single chosen output

Common confusion:

- `select` chooses among inputs
- `sample` chooses subsets of points
- `route` splits whole data objects into multiple outputs

### `route`

Routes whole data objects.

Node names in this family:

- `Filter Data By Tag`
- `Filter Data By Attribute`
- `Filter Data By Type`
- `Filter Data By Index`

Most common usage:

- `Filter Data By Tag` is the most common whole-data route node after `Get Actor
  Data` or other world-sourced inputs
- `Filter Data By Attribute` is the most common whole-data attribute-driven
  route node when you need to decide whether an entire input dataset continues

Most important parameters and wiring:

- for `Filter Data By Tag`: tag list and matched branch selection
- for `Filter Data By Attribute`:
  - `Attribute` for existence mode
  - `TargetAttribute` for value or range mode
- key output pins:
  - `InsideFilter`
  - `OutsideFilter`

Common confusion:

- `Filter Data By Attribute` is a `route` node, not a true element filter
- it splits whole data objects, not individual points inside the data
- if only some points should survive, this is usually the wrong node

Most important example:

- use `Filter Data By Tag` after `Get Actor Data` when you want whole upstream
  data objects with matching tags to continue on `InsideFilter`, and all others
  to fall through `OutsideFilter`

### `filter`

Splits individual elements or points inside a dataset.

Node names in this family:

- `Filter Attribute Elements`
- `Filter Attribute Elements by Range`
- `Density Filter`
- `Point Filter`
- `Point Filter Range`
- `Filter Elements By Index`
- `Self Pruning`

Most common usage:

- `Filter Attribute Elements` is the general-purpose element filter
- `Filter Attribute Elements by Range` is the range form
- `Density Filter` is a specialized density-only filter

Most important parameters and wiring:

- comparison operator or range bounds
- threshold constant vs threshold input choice
- key output pins:
  - `InsideFilter`
  - `OutsideFilter`
- when threshold data comes from another input, verify extra filter input pins
  such as secondary filter inputs before wiring

Common confusion:

- `Density Filter` is specialized and efficient, but the official docs describe
  it as effectively superseded by the more general Attribute Filter family
- `Filter Attribute Elements` and `Filter Data By Attribute` are not
  interchangeable
- if the desired behavior is per-point splitting, choose `filter`
- if the desired behavior is whole-data routing, choose `route`

Most important example:

- use `Filter Attribute Elements` when only points with `Slope >= 0.6` should
  continue on `InsideFilter` while the remaining points still flow separately on
  `OutsideFilter`

### `spawn`

Realizes points into output content.

Node names in this family:

- `Static Mesh Spawner`
- `Spawn Actor`
- `Create Target Actor`
- `Apply On Actor`

Most common usage:

- `Static Mesh Spawner` is the default point-to-mesh realization node
- `Spawn Actor` is used when the output should be actor-based instead of mesh
  instance based

Most important parameters and wiring:

- mesh selector or actor class configuration
- any attribute-driven selection mode
- output connection from the final point-producing stage into the spawner input

Common confusion:

- if the spawner appears "broken", the problem is often upstream metadata or
  selector configuration rather than the spawner node itself
- always inspect `meshSelector`, actor selector, and diagnostics after mutation

### `struct`

Shapes graph structure.

Node names in this family:

- `Subgraph`
- `Loop`

Most common usage:

- `Subgraph` is used to encapsulate and reuse graph structure
- `Loop` is used when the same internal pattern should run repeatedly over a
  structured input

Most important parameters and wiring:

- subgraph reference or loop wiring
- upstream/downstream interface pins

Common confusion:

- these are not ordinary transform nodes
- they change graph shape and execution structure, so interface verification is
  more important than a single node's local settings

## Important Distinctions

### `Branch` vs `Filter Data By Attribute`

- `Branch` is pure control flow and consumes an existing decision
- `Filter Data By Attribute` performs its own attribute-based test and routes
  whole data objects

### `Filter Data By Attribute` vs `Filter Attribute Elements`

- `Filter Data By Attribute` routes whole input data objects
- `Filter Attribute Elements` splits elements inside the input data

If only some points match and you need those points separated from the rest,
prefer `Filter Attribute Elements`.

If you need to decide whether an entire upstream dataset should continue down a
branch, prefer `Filter Data By Attribute`.

## Readback And Validation Clues

- prefer `graph.query` after every meaningful PCG edit, not just for topology
  but also for node `effectiveSettings` and node-level `diagnostics`
- selector-backed nodes often need nested readback through `actorSelector`,
  `componentSelector`, or `meshSelector`
- `Projection` semantics usually live inside `effectiveSettings.projectionParams`
- when a PCG pipeline looks empty, inspect node `diagnostics` first

## Layout Style

- source nodes should appear on the left
- downstream processing stages should move to the right
- parallel branches should separate vertically
- do not use layout as proof that the graph is correct
