# LGL Bridge Code Layout

This document proposes the physical code layout for a future LGL-native UE
bridge implementation.

The layout is designed to enforce the architecture in
[`LGL_NATIVE_BRIDGE.md`](LGL_NATIVE_BRIDGE.md): old bridge code is reference
material and comparison-test input, not a runtime dependency for the new path.

## Goals

- Keep LGL-native bridge code physically separate from existing graph tool
  implementations.
- Make object-model decode/encode independent from Blueprint, Material, and PCG
  semantics.
- Make graph-domain adapters explicit modules.
- Keep Blueprint adapter code organized around UE responsibilities: resolve,
  read, palette, plan, apply, diagnostics.
- Make the first UE-backed spike small enough to review.

## Non-Goals

- Do not move existing graph tools during the first spike.
- Do not add LGL code into existing Blueprint graph edit files.
- Do not create wrapper files that translate LGL into old public tool payloads.
- Do not make C++ parse raw LGL text.
- Do not add Material or PCG adapters before the Blueprint path proves the
  architecture.

## Unreal Plugin Layout

Proposed root:

```txt
engine/LoomleBridge/Source/LoomleBridge/Private/Lgl/
```

Core files:

```txt
Private/Lgl/
  LglModule.h
  LglModule.cpp
  LglObjectModel.h
  LglJsonCodec.h
  LglJsonCodec.cpp
  LglSchemaValidator.h
  LglSchemaValidator.cpp
  LglDiagnostics.h
  LglDiagnostics.cpp
  LglResult.h
  LglResult.cpp
  LglGraphAdapter.h
  LglAdapterRegistry.h
  LglAdapterRegistry.cpp
```

Blueprint adapter files:

```txt
Private/Lgl/Blueprint/
  LglBlueprintAdapter.h
  LglBlueprintAdapter.cpp
  LglBlueprintResolve.h
  LglBlueprintResolve.cpp
  LglBlueprintRead.h
  LglBlueprintRead.cpp
  LglBlueprintPalette.h
  LglBlueprintPalette.cpp
  LglBlueprintPatch.h
  LglBlueprintPatch.cpp
  LglBlueprintPlan.h
  LglBlueprintPlan.cpp
  LglBlueprintApply.h
  LglBlueprintApply.cpp
  LglBlueprintDiagnostics.h
  LglBlueprintDiagnostics.cpp
```

The exact file count may shrink during implementation, but the boundaries should
remain visible in review.

## Core Responsibilities

### `LglModule`

Owns LGL RPC endpoint registration and top-level dispatch.

Responsibilities:

- register `lgl.object.query`
- register `lgl.object.patch`
- decode request envelopes
- run schema/structural validation
- dispatch by `Target.domain`
- encode `ObjectResult`

It should not contain Blueprint graph logic.

### `LglObjectModel`

Defines lightweight C++ structs for the normalized object contract:

```txt
FLglTarget
FLglGraphRef
FLglGraph
FLglNode
FLglPin
FLglEdge
FLglQuery
FLglFind
FLglPatch
FLglBinding
FLglOp
FLglValue
FLglDiagnostic
FLglObjectResult
```

These structs mirror `schema/lgl-object.schema.json`. They are not a UE graph
model. They are transport and adapter-boundary types.

The first spike may implement only the subset needed by `lgl.object.query`.
That subset should still use names and shapes from the schema so later generated
or expanded codecs do not require a semantic rewrite.

### `LglJsonCodec`

Converts between JSON and `LglObjectModel` structs.

Responsibilities:

- decode normalized `Query` / `Patch` JSON
- encode `Graph` / `Palette` / `ObjectResult` JSON
- preserve enough path context for diagnostics
- avoid UE graph semantics

### `LglSchemaValidator`

Validates object shape at the bridge boundary.

First implementation options:

- structural hand validation matching the schema
- embedded/generated schema validator if practical

The first spike can start with structural validation if it rejects malformed
requests and protects C++ codecs from invalid shapes.

### C++ Type Generation Strategy

`schema/lgl-object.schema.json` is the machine contract. TypeScript already
generates object-model types from it. The UE bridge should follow the same
contract, but the first spike does not need full C++ type generation.

Recommended sequence:

1. Hand-write a minimal C++ object subset for the first query spike.
2. Validate request/response JSON at the bridge boundary.
3. Add fixture tests that decode and encode schema-valid JSON examples.
4. Expand hand-written structs/codecs only as operations require them.
5. Reassess generated or semi-generated C++ codecs after the object boundary is
   proven against UE.

Reasons not to start with full C++ generation:

- Recursive `Value` and discriminated unions need careful Unreal-style design.
- Generated code may not follow UE conventions or be pleasant to review.
- The first spike needs to prove UE object RPC and Blueprint readback before
  optimizing the type maintenance workflow.

Requirements while structs/codecs are hand-written:

- schema remains the source of truth
- no extra C++-only object fields at the RPC boundary
- accepted/rejected JSON fixtures exercise the codecs
- generated TypeScript types and C++ structs are reviewed against the same
  schema changes

### `LglDiagnostics`

Shared helpers for diagnostics:

```txt
unknown_domain
invalid_request
graph_not_found
unknown_node
unknown_pin
unknown_palette_entry
missing_insert_edge
```

Diagnostics should include actionable `suggestion` text when possible.

### `LglResult`

Helpers for assembling `ObjectResult`:

- success with object
- error with diagnostics
- append warnings
- attach graph snippets

### `LglGraphAdapter`

Domain adapter interface:

```cpp
class ILglGraphAdapter
{
public:
    virtual FString Domain() const = 0;
    virtual FLglObjectResult Query(const FLglQuery& Query) = 0;
    virtual FLglObjectResult Patch(const FLglPatch& Patch) = 0;
};
```

### `LglAdapterRegistry`

Maps domain names to adapters:

```txt
blueprint -> FLglBlueprintAdapter
material  -> future material adapter
pcg       -> future PCG adapter
```

The registry should make unknown domains fail with `unknown_domain`.

## Blueprint Adapter Responsibilities

### `LglBlueprintAdapter`

Top-level Blueprint adapter implementation.

Responsibilities:

- report domain `blueprint`
- dispatch query variants
- dispatch patch variants
- own helper objects if needed

It should stay thin. Detailed UE work belongs in the files below.

### `LglBlueprintResolve`

Resolves UE objects:

- asset path -> Blueprint asset
- graph name/id -> `UEdGraph`
- node alias/id -> `UEdGraphNode`
- pin ref -> `UEdGraphPin`

This layer owns ambiguity and not-found diagnostics.

### `LglBlueprintRead`

Converts UE graph state into LGL objects:

- graph snapshot
- selected node snippet
- path snippet
- surrounding snippet
- pin/default/layout readback
- node alias/id assignment

It should use UE graph data directly.

### `LglBlueprintPalette`

Owns Blueprint palette/action behavior:

- palette query
- stable palette id generation
- stable palette id decode
- context-sensitive action lookup
- node spawner resolution

The old palette tool is reference-only. Useful UE API logic may be moved here,
but this module should not call the old tool implementation.

### `LglBlueprintPatch`

Top-level patch dispatcher:

- classify operations
- order operations when the language requires it
- call planner/apply helpers
- assemble changed snippets

### `LglBlueprintPlan`

Dry-run and apply share this planning path.

Responsibilities:

- resolve palette bindings
- resolve node specs
- validate fields and values
- validate pin references
- validate existing graph state such as `insert` old edge
- produce a plan that can be returned for dry run or applied later

### `LglBlueprintApply`

Applies a validated plan to UE:

- create transaction
- invoke node spawners
- set defaults/fields
- break links
- create links through UE schema
- move nodes
- reconstruct nodes when required
- mark Blueprint dirty

This module is the only Blueprint LGL module that should mutate UE graph state.

### `LglBlueprintDiagnostics`

Maps UE failures and adapter validation errors into LGL diagnostics.

Diagnostics should be phrased for agent recovery:

- run `find node <name> with pins`
- run `find palette entry "..."`
- query surrounding graph before patching

## Include Rules

Allowed:

- LGL core includes generated or hand-written LGL object model types.
- Blueprint adapter includes LGL core headers and UE Blueprint headers.
- Existing bridge files may include LGL module registration only when wiring RPC.

Disallowed:

- LGL modules including existing public graph edit tool implementation headers.
- LGL patch code constructing old public edit command payloads.
- LGL query code calling old public graph inspect tool handlers.
- LGL palette code calling old public palette tool handlers.

If old code contains necessary UE API logic, move a focused helper into the LGL
module and adapt it to LGL object boundaries.

## MCP And Client Layout

Do not expose agent-facing tools until object RPC works.

Future tool/client files may look like:

```txt
client/src/lgl_tools.rs
mcp/python/loomle_mcp/lgl.py
mcp/manifest/lgl_tools.json
```

The tools should call the TypeScript SDK or equivalent parser/formatter path:

```txt
LGL text
  -> parse/validate
  -> lgl.object.query / lgl.object.patch
  -> ObjectResult
  -> format
```

## Test Layout

Suggested tests:

```txt
tests/lgl/
  fixtures/
    query_find_node.json
    query_palette_print_string.json
    patch_insert_delay_dry_run.json
  test_lgl_object_query.py
  test_lgl_object_patch_dry_run.py
  test_lgl_blueprint_readback_compare.py
```

Test roles:

- Object RPC tests verify decode/dispatch/encode.
- Codec fixture tests verify C++ structs can round-trip schema-valid object
  examples.
- Rejected fixture tests verify malformed objects fail before adapter dispatch.
- Blueprint readback tests compare new LGL output against known UE graph state.
- Comparison tests may call old tools as an oracle, but production LGL code must
  not call old tool implementations.
- Patch dry-run tests verify planning without asset mutation.
- Apply tests should compile or query after mutation to verify graph state.

## First Spike Layout

The behavior contract for this spike is documented in
[`LGL_BRIDGE_QUERY_SPIKE.md`](LGL_BRIDGE_QUERY_SPIKE.md).

The first implementation spike should touch only:

```txt
Private/Lgl/LglObjectModel.h
Private/Lgl/LglJsonCodec.h
Private/Lgl/LglJsonCodec.cpp
Private/Lgl/LglDiagnostics.h
Private/Lgl/LglDiagnostics.cpp
Private/Lgl/LglResult.h
Private/Lgl/LglResult.cpp
Private/Lgl/LglGraphAdapter.h
Private/Lgl/LglAdapterRegistry.h
Private/Lgl/LglAdapterRegistry.cpp
Private/Lgl/LglModule.h
Private/Lgl/LglModule.cpp
Private/Lgl/Blueprint/LglBlueprintAdapter.h
Private/Lgl/Blueprint/LglBlueprintAdapter.cpp
Private/Lgl/Blueprint/LglBlueprintResolve.h
Private/Lgl/Blueprint/LglBlueprintResolve.cpp
Private/Lgl/Blueprint/LglBlueprintRead.h
Private/Lgl/Blueprint/LglBlueprintRead.cpp
```

First spike scope:

- `lgl.object.query`
- `Target.domain = "blueprint"`
- empty query
- `find node <name> with pins, defaults`
- no mutation
- no palette
- no public MCP LGL text tool yet

This proves the new object boundary and UE readback path before adding patch
complexity.
