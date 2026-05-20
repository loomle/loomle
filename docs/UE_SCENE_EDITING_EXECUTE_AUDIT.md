# UE Scene Editing Via `execute`

Current status audit for using `execute`-driven Unreal-side Python as the
fallback surface for non-graph editor automation in `LOOMLE`.

## Scope

This document answers one narrow question:

- how much of Unreal scene or level editing can agents already do
  *reliably* through `execute`

It does not propose a new API yet. It only summarizes:

- what is already implemented
- what is already exercised in tests
- what remains theoretically possible but operationally weak

## Bottom Line

`execute` is a working Unreal-side Python fallback, but it is not yet a mature
scene-editing work surface.

Today it is best described as:

- reliable for small, explicit editor automation tasks
- proven for asset creation/deletion and a few limited level-instance actions
- not yet backed by a scene-oriented guide, targeting contract, or validation
  loop comparable to the graph workflow

## What Is Already Solid

### 1. The transport itself

The `execute` tool is real, documented, and part of the baseline tool surface:

- [MCP protocol execute section](/Users/xartest/dev/loomle/docs/MCP_PROTOCOL.md#L164)
- [RPC interface execute section](/Users/xartest/dev/loomle/docs/RPC_INTERFACE.md#L196)
- [server tool schema](/Users/xartest/dev/loomle/mcp/server/src/schema.rs#L39)

Runtime execution is handled inside the Unreal Editor process via
`IPythonScriptPlugin`:

- [BuildExecutePythonToolResult](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRuntime.inl#L1643)

### 2. Asset-level editor automation

The strongest existing evidence is asset manipulation rather than scene
manipulation.

Verified patterns in tests and tools include:

- create a temporary Blueprint asset with `AssetToolsHelpers`
- check asset existence with `EditorAssetLibrary`
- delete assets during cleanup

Examples:

- [smoke asset creation](/Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py#L1278)
- [asset cleanup](/Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py#L1356)
- [helper tool for temp Blueprint assets](/Users/xartest/dev/loomle/tools/create_temp_blueprint_asset.py#L87)

### 3. Minimal level-instance editing

There is one meaningful, real level-editing path already exercised:

- spawn a `PCGVolume`
- get its `pcg_component`
- bind a graph to that component
- select the actor in the level editor

Example:

- [PCG fixture creation in regression](/Users/xartest/dev/loomle/tests/e2e/test_bridge_regression.py#L757)

This is important because it proves that `execute` is not limited to asset
registry operations; it can already manipulate level instances in-editor.

## What Is Only Partially Proven

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

## Why It Is Not Yet Comfortable

### 1. Raw Python strings are still the primary unit

Agents currently send literal Python source strings:

- [smoke execute helper](/Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py#L1125)

That works, but it means scene editing today depends on:

- hand-constructed code
- manual JSON printing conventions
- ad hoc object targeting

### 2. Result contracts are still ad hoc

Most verified examples rely on:

- `print(json.dumps(...))`
- then parsing the resulting object from tool output

Example parser:

- [parse_execute_json](/Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py#L1156)

This is good enough for controlled tests, but it is not yet a polished scene
editing interface.

### 3. Python runtime readiness still needs retries

The smoke test explicitly retries `execute` until Python is ready:

- [call_execute_exec_with_retry](/Users/xartest/dev/loomle/tests/e2e/test_bridge_smoke.py#L1125)

That tells us the channel is workable, but not instant or fully frictionless.

### 4. Timeout behavior is split across layers

The MCP/server side gives `execute` a longer deadline:

- [server default deadline](/Users/xartest/dev/loomle/mcp/server/src/sdk.rs#L98)

But the Unreal-side wrapper still imposes a fixed internal timeout for `exec`
mode:

- [30 second wrapper timeout](/Users/xartest/dev/loomle/engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRuntime.inl#L1689)

That is acceptable for short automation, but it is a mismatch for longer scene
tasks.

### 5. There is no scene-oriented validation loop yet

Graph editing already has a clear rhythm:

- `graph.query`
- `graph.mutate`
- `graph.verify`

Scene editing does not yet have an equivalent workspace guide or proof pattern.
The top-level workspace README already hints that this is a separate concern:

- [workspace README note on runtime scene debugging](/Users/xartest/dev/loomle/workspace/Loomle/README.md#L172)

## Proven Capability Matrix

### Stable today

- asset create
- asset delete
- asset existence checks
- spawn a specific actor class for test setup
- set level selection
- bind a component property after spawn

### Likely engine-capable but not yet operationalized

- move or rotate arbitrary actors
- edit common actor properties
- place common scene actors such as `StaticMeshActor`
- set folders or labels
- add components to existing placed actors
- bulk scene selection and rewrite

### Not yet modeled as a `LOOMLE` workflow

- stable scene-object targeting contract
- scene examples library
- scene smoke or regression inventory
- scene guide parallel to graph guides
- scene validation checklist

## Recommendation

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

## Practical Product Framing

If we describe the current state honestly:

- `execute` is already a dependable Unreal-side Python escape hatch
- it is not yet a scene-editing platform
- the next milestone is not “support everything in the world editor”
- the next milestone is “make a tiny, repeatable, validated scene-editing
  subset comfortable for agents”

That is the right base to build on.
