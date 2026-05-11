# Blueprint Graph Layout

## Intent

`blueprint.graph.layout` is the visual formatting surface for Blueprint graphs.

It exists so agents can make a recently edited graph region readable without
changing Blueprint behavior. The first version is intentionally narrow: it
formats a selected region or a downstream execution tree. It does not format an
entire graph.

## Tool Boundary

`blueprint.graph.layout` owns visual organization:

- choose a set of existing nodes
- compute target node positions
- move those nodes
- return a dry-run plan or an applied diff

It does not own:

- node creation, which belongs to `blueprint.graph.edit`
- pin connection changes, which belong to `blueprint.graph.edit` or
  `blueprint.graph.refactor`
- structural transformations, which belong to `blueprint.graph.refactor`
- recipe expansion, which belongs to `blueprint.graph.generate`
- whole-graph formatting
- reroute insertion, reroute cleanup, or wire routing in the first version
- comment fitting or comment creation in the first version

## First-Version Operation

The first public operation is:

```text
format
```

No shortcut form is supported. A single node is formatted through
`scope.mode="selection"` with one node in `scope.nodes`.

## Top-Level Schema

Recommended first-version request shape:

```json
{
  "assetPath": "/Game/Blueprints/BP_Test",
  "graphName": "EventGraph",
  "operation": "format",
  "scope": {
    "mode": "tree",
    "root": { "id": "branch1" }
  },
  "direction": "right",
  "style": "simple",
  "spacing": { "x": 360, "y": 180 },
  "origin": { "x": 400, "y": 200 },
  "dryRun": true
}
```

Required fields:

- `assetPath`
- graph address, such as `graphName`
- `operation`
- `scope`

Optional fields:

- `direction`
- `style`
- `spacing`
- `origin`
- `dryRun`
- `expectedRevision`
- `returnDiagnostics`
- `returnDiff`

## Scope Modes

### selection

`selection` formats only the nodes explicitly named by the caller.

```json
{
  "operation": "format",
  "scope": {
    "mode": "selection",
    "nodes": [
      { "id": "nodeA" },
      { "id": "nodeB" }
    ]
  }
}
```

Rules:

- `nodes` is required and must contain at least one node.
- Only listed nodes may be moved.
- Loomle must not expand the selection automatically.
- One-node formatting uses this mode.

### tree

`tree` formats a downstream execution tree starting from one root node.

```json
{
  "operation": "format",
  "scope": {
    "mode": "tree",
    "root": { "id": "branch1" }
  }
}
```

Rules:

- `root` is required.
- The root node is included.
- The first version follows execution output pins downstream.
- The first version does not follow data-flow-only links.
- Traversal stays within the addressed graph.
- Already visited nodes are skipped to avoid cycles.
- Comment nodes are not included.
- Reroute nodes are not inserted, removed, or cleaned up.

This mode is the equivalent of asking Loomle to format the connected execution
region to the right of a selected Blueprint node.

## Formatting Options

### direction

Supported values:

- `right`
- `down`

Default:

```text
right
```

### style

Supported first-version value:

```text
simple
```

`simple` lays nodes out in a predictable grid/tree order. It should favor
readability and deterministic diffs over editor-perfect formatting.

### spacing

Recommended default:

```json
{ "x": 360, "y": 180 }
```

### origin

If `origin` is omitted:

- `tree` keeps the root anchored at its current position.
- `selection` uses the current selection bounding box top-left as the origin.

## Dry Run

`dryRun=true` must compute the same layout plan without mutating the asset.

Dry run is important because layout can move many nodes at once. The returned
plan should be usable by an agent to decide whether to apply the same request
without `dryRun`.

## Result Shape

Dry run and execution should use the same result structure:

```json
{
  "changed": true,
  "dryRun": true,
  "operation": "format",
  "scope": {
    "mode": "tree",
    "resolvedNodeCount": 5
  },
  "nodesMoved": [
    {
      "node": { "id": "nodeA" },
      "from": { "x": 100, "y": 200 },
      "to": { "x": 400, "y": 200 }
    }
  ],
  "warnings": []
}
```

Rules:

- `nodesMoved` must include before and after positions.
- Nodes whose target position equals their current position may be omitted.
- `changed=false` means the format plan produced no movement.
- First-version `format` must not report link changes.

## Explicit Non-Goals

The first version must not implement:

- `organizeGraph`
- implicit single-node shortcut syntax
- whole-graph formatting
- automatic region discovery beyond `tree`
- reroute insertion
- reroute cleanup
- wire routing
- comment creation
- comment fitting
- data-flow-only traversal

