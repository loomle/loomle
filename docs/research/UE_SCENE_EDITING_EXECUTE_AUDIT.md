# UE Scene Editing Via `execute`

> **Status: historical research, not a current capability contract.** This
> audit predates the 0.7 TypeScript Client and still cites removed Rust/Python
> server code, old tool names, and machine-local paths. The Bridge still
> contains an internal `execute` fallback, but the current four-tool Client does
> not expose it. Re-audit the repository and UE 5.7 source before using this
> document to design a public scene-editing workflow.

Historical audit of using `execute`-driven Unreal-side Python as a fallback for
non-graph editor automation in Loomle.

## Scope

This document answers one narrow question:

- how much of Unreal scene or level editing can agents already do
  *reliably* through `execute`

It does not propose a new API yet. It only summarizes:

- what is already implemented
- what is already exercised in tests
- what remains theoretically possible but operationally weak

## Historical Bottom Line

`execute` is a working Unreal-side Python fallback, but it is not yet a mature
scene-editing work surface.

Today it is best described as:

- reliable for small, explicit editor automation tasks
- proven for asset creation/deletion and a few limited level-instance actions
- not yet backed by a scene-oriented guide, targeting contract, or validation
  loop comparable to the graph workflow

## Evidence Recorded At The Time

### 1. The transport itself

At the time of the audit, `execute` was part of the baseline 0.6 tool surface:

- [archived MCP protocol](../archive/legacy/MCP_PROTOCOL.md)
- [archived RPC interface](../archive/legacy/RPC_INTERFACE.md)
- removed Rust server schema: `mcp/server/src/schema.rs`

Runtime execution is handled inside the Unreal Editor process via
`IPythonScriptPlugin`:

- [Bridge runtime implementation](../../engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRuntime.inl)

### 2. Asset-level editor automation

The strongest existing evidence is asset manipulation rather than scene
manipulation.

Verified patterns in tests and tools include:

- create a temporary Blueprint asset with `AssetToolsHelpers`
- check asset existence with `EditorAssetLibrary`
- delete assets during cleanup

Examples:

- [legacy smoke coverage](../../tests/e2e/test_bridge_smoke.py)
- [legacy asset cleanup coverage](../../tests/e2e/test_bridge_smoke.py)
- removed helper: `tools/create_temp_blueprint_asset.py`

### 3. Minimal level-instance editing

There is one meaningful, real level-editing path already exercised:

- spawn a `PCGVolume`
- get its `pcg_component`
- bind a graph to that component
- select the actor in the level editor

Example:

- [legacy PCG fixture coverage](../../tests/e2e/test_bridge_regression.py)

This is important because it proves that `execute` is not limited to asset
registry operations; it can already manipulate level instances in-editor.

## What Was Only Partially Proven

These categories are likely possible in Unreal Python, but this repository does
not yet prove that agents can do them comfortably and repeatedly:

- generic Actor spawn by arbitrary class
- Actor delete
- Actor transform edits
- Actor attach or detach
- folder or World Outliner organization
- Data Layer operations
- sublevel or World Partition editing
- component add or remove on existing actors
- batch scene querying and batch edits

The issue is not raw engine capability. The issue is missing productized
workflow.

## Why It Was Not Yet Comfortable

### 1. Raw Python strings are still the primary unit

Agents currently send literal Python source strings:

- [legacy smoke execute helper](../../tests/e2e/test_bridge_smoke.py)

That works, but it means scene editing today depends on:

- hand-constructed code
- manual JSON printing conventions
- ad hoc object targeting

### 2. Result contracts are still ad hoc

Most verified examples rely on:

- `print(json.dumps(...))`
- then parsing the resulting object from tool output

Example parser:

- [legacy execute-result parser](../../tests/e2e/test_bridge_smoke.py)

This is good enough for controlled tests, but it is not yet a polished scene
editing interface.

### 3. Python runtime readiness still needs retries

The smoke test explicitly retries `execute` until Python is ready:

- [legacy execute retry helper](../../tests/e2e/test_bridge_smoke.py)

That tells us the channel is workable, but not instant or fully frictionless.

### 4. Timeout behavior is split across layers

The MCP/server side gives `execute` a longer deadline:

- removed Rust server deadline: `mcp/server/src/sdk.rs`

But the Unreal-side wrapper still imposes a fixed internal timeout for `exec`
mode:

- [Bridge runtime timeout](../../engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRuntime.inl)

That is acceptable for short automation, but it is a mismatch for longer scene
tasks.

### 5. There is no scene-oriented validation loop yet

Graph editing already has a clear rhythm:

- `graph.query`
- `graph.mutate`
- `graph.verify`

Scene editing does not yet have an equivalent workspace guide or proof pattern.
The top-level workspace README already hints that this is a separate concern:

- [archived workspace guide](../archive/workspace/Loomle/README.md)

## Historical Capability Matrix

### Stable in the audited 0.6 environment

- asset create
- asset delete
- asset existence checks
- spawn a specific actor class for test setup
- set level selection
- bind a component property after spawn

### Likely engine-capable but not operationalized in that environment

- move or rotate arbitrary actors
- edit common actor properties
- place common scene actors such as `StaticMeshActor`
- set folders or labels
- add components to existing placed actors
- bulk scene selection and rewrite

### Not modeled as a Loomle workflow in that environment

- stable scene-object targeting contract
- scene examples library
- scene smoke or regression inventory
- scene guide parallel to graph guides
- scene validation checklist

## Future Re-Audit Direction

Before designing a larger scene-editing API, first establish a minimum proven
workflow around `execute`.

Recommended first batch:

1. query or identify a stable target actor
2. spawn an actor
3. delete an actor
4. move an actor
5. set one safe actor property

For each of those, add:

- one canonical `execute` example
- one validation pattern
- one smoke or regression test
- one small usage guide entry

## Historical Product Framing

The audit described its then-current state as follows:

- `execute` is already a dependable Unreal-side Python escape hatch
- it is not yet a scene-editing platform
- the next milestone is not “support everything in the world editor”
- the next milestone is “make a tiny, repeatable, validated scene-editing
  subset comfortable for agents”

That is the right base to build on.
