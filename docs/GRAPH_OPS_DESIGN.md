# Graph Ops Design

## 1. Summary

This document now serves two purposes:

1. record the historical `graph.ops` / `graph.ops.resolve` design
2. state the current public direction for LOOMLE graph work

Status note:

- this document is historical design context, not active tool-surface reference
- `graph.ops` and `graph.ops.resolve` are no longer part of the active LOOMLE tool surface
- use `docs/MCP_PROTOCOL.md`, `docs/RPC_INTERFACE.md`, and `workspace/Loomle/` for current product-facing guidance

Current public direction:

- agent-first graph guidance should live in `workspace/Loomle/`
- the primary agent reference surface is:
  - graph-specific `GUIDE.md`
  - graph-specific `SEMANTICS.md`
  - graph-specific `catalogs/` and `examples/`
- `graph.mutate` remains the stable primitive execution layer
- `graph.query` and `graph.verify` remain the validation loop
- historical design explored `graph.ops` as an optional curated live listing
- historical design treated `graph.ops.resolve` as a secondary planning path rather than the preferred one

Historical proposal:

- `graph.ops`
- `graph.ops.resolve`

The intent is to give agents a stable semantic planning layer that is easier
to reason about than editor-menu-shaped action discovery with temporary
`actionToken` values.

That original proposal assumed a hard-cut move toward `graph.ops`,
`graph.ops.resolve`, and `graph.mutate` as the main public semantic flow. The
current direction is narrower and puts workspace-local references first.

## 2. Problem

The old action-token surface carried several different concerns:

- capability discovery
- context-sensitive editor action lookup
- temporary execution handles
- partial semantic planning

That overload is manageable for runtime plumbing, but it is not a clean public
semantic model for agents.

The main mismatch is this:

- agents want stable semantic operation identifiers
- the current interface returns ephemeral menu actions

This is especially visible across graph domains. Blueprint, Material, and PCG
do not expose the same discovery depth or internal mechanisms, but
those concerns ended up bundled behind one public name.

## 3. Design Goals

1. Keep workspace-local graph references as the primary agent-facing knowledge surface.
2. Keep `graph.mutate` primitive and UE-aligned.
3. Keep `graph.query` and `graph.verify` as the reliable readback and validation loop.
4. Allow `graph.ops` to exist as an optional curated live listing.
5. Avoid making `graph.ops.resolve` the center of the public workflow.

## 4. Non-Goals

- redefining `graph.mutate`
- promising exhaustive coverage of all Unreal-supported operations
- forcing all graph domains into one universal operation taxonomy
- introducing a second required naming layer that agents must memorize before they can use real node names

## 5. Tool Overview

Status note:

- `graph.ops` still fits the current direction as an optional live catalog.
- `graph.ops.resolve` should be treated as a historical planning design and an
  optional secondary capability, not as the default path new agent guidance should teach.

### 5.1 `graph.ops`

Purpose:

- list stable semantic operations known to LOOMLE for the current graph domain
- advertise graph-domain coverage and stability

This is an inventory surface, not an execution or menu-discovery surface.

### 5.2 `graph.ops.resolve`

Purpose:

- resolve one or more semantic `opId` values in a concrete graph context
- return a mutate-ready preferred plan plus structured alternatives
- report compatibility, determinism, and plan source explicitly

This is a planning surface, not a mutate surface.

Current status:

- no longer the preferred public planning path
- should not be the main dependency of workspace-local graph guides
- if retained, it should be documented as optional and secondary

## 6. Contract Principles

### 6.1 Uniform top-level framework

All graph domains should share:

- the same tool names
- the same `graphRef` addressing model
- the same output skeleton
- the same meta signals

### 6.2 Domain-specific implementation

The internal resolver for each graph domain can differ significantly:

- Blueprint: strongly context-sensitive, editor-driven discovery
- Material: more catalog-driven, lower context sensitivity
- PCG: node-template and topology-oriented semantic planning

The framework should unify the protocol, not the runtime mechanism.

### 6.3 Coverage is explicit, not implied

The APIs should never imply that LOOMLE can exhaustively enumerate every
operation Unreal supports.

Instead, LOOMLE should report:

- where a result came from
- how complete that result is within scope
- how stable the realization is

### 6.4 Observability is a prerequisite

`graph.ops.resolve` can only be as trustworthy as the underlying graph
observability surface.

This is especially important for PCG, where graph behavior depends heavily on:

- effective node settings
- nested settings payloads
- pin semantics
- subgraph target resolution
- actionable empty-input diagnostics

If `graph.query` cannot expose those reliably, `graph.ops.resolve` should not
pretend to offer high-confidence semantic planning for those operations.

## 7. `opId` Model

`opId` is a stable semantic identifier, not a natural-language intent and not
an editor action token.

Recommended naming pattern:

- cross-graph shared ops: `core.comment`, `core.reroute`
- Blueprint-specific ops: `bp.flow.branch`, `bp.math.multiply`
- Material-specific ops: `mat.texture.sample`
- PCG-specific ops: `pcg.route.data_by_tag`

Rules:

1. Use lowercase dotted identifiers.
2. Prefer short semantic names over engine-class names.
3. Only mint a cross-graph `opId` when the semantics are genuinely shared.
4. Keep graph-domain namespaces when behavior or meaning is domain-specific.

Recommended per-op metadata:

- `stability`: `stable | experimental`
- `scope`: `cross-graph | blueprint | material | pcg`
- `summary`: short agent-facing description
- `tags`: optional discovery tags

Current status note:

- this model is most useful for curated live catalog output
- it is no longer assumed to be the primary naming system agents rely on during everyday graph work
- workspace-local `SEMANTICS.md` files should prefer real node names and usage explanations over `opId`-first teaching

## 8. `graph.ops` Proposal

### 8.1 Input

Minimal input:

```json
{
  "graphType": "blueprint|material|pcg"
}
```

Optional future filters may include:

- `stability`
- `query`
- `scope`

`graph.ops` should not require a graph context for the first version.

### 8.2 Output

```json
{
  "graphType": "blueprint",
  "ops": [
    {
      "opId": "core.comment",
      "stability": "stable",
      "scope": "cross-graph",
      "summary": "Add a comment node or comment region."
    },
    {
      "opId": "bp.flow.branch",
      "stability": "stable",
      "scope": "blueprint",
      "summary": "Add a branch flow-control node."
    }
  ],
  "meta": {
    "source": "loomle_catalog",
    "coverage": "partial"
  },
  "diagnostics": []
}
```

### 8.3 Semantics

- `graph.ops` lists operations LOOMLE knows how to reason about.
- It is not expected to mirror the exact current editor menu.
- It is allowed to be curated.
- It should be treated as optional live catalog output rather than the primary agent knowledge source.

## 9. `graph.ops.resolve` Proposal

Status note:

The design below is retained as historical planning design material. It no
longer defines the preferred public workflow. New agent-facing guidance should
prefer:

1. workspace-local guides and semantics
2. primitive `graph.mutate`
3. `graph.query` / `graph.verify`

### 9.1 Input

```json
{
  "graphType": "blueprint|material|pcg",
  "graphRef": { "kind": "asset", "assetPath": "/Game/BP_Foo", "graphName": "EventGraph" },
  "context": {
    "fromPin": {
      "nodeId": "N1",
      "pinName": "Then"
    },
    "toPin": {
      "nodeId": "N2",
      "pinName": "Execute"
    },
    "edge": {
      "fromPin": {
        "nodeId": "N1",
        "pinName": "Then"
      },
      "toPin": {
        "nodeId": "N2",
        "pinName": "Execute"
      }
    }
  },
  "items": [
    { "opId": "bp.flow.branch" },
    { "opId": "core.comment" }
  ]
}
```

Notes:

- `graphRef` should remain the preferred addressing mode.
- `context` is optional.
- `context` should be allowed to narrow from graph scope to pin scope or edge
  scope when the semantic op depends on insertion position or endpoint role.
- `items` is batch-first by design.

### 9.2 Output

```json
{
  "graphType": "blueprint",
  "graphRef": { "kind": "asset", "assetPath": "/Game/BP_Foo", "graphName": "EventGraph" },
  "results": [
    {
      "opId": "bp.flow.branch",
      "resolved": true,
      "compatibility": {
        "isCompatible": true,
        "reasons": []
      },
      "preferredPlan": {
        "realizationKind": "spawn_node",
        "preferredMutateOp": "addNode.byClass",
        "args": {
          "nodeClassPath": "/Script/BlueprintGraph.K2Node_IfThenElse"
        },
        "source": "typed_discovery",
        "coverage": "contextual",
        "determinism": "stable"
      },
      "alternatives": []
    },
    {
      "opId": "core.comment",
      "resolved": true,
      "compatibility": {
        "isCompatible": true,
        "reasons": []
      },
      "preferredPlan": {
        "realizationKind": "spawn_node",
        "preferredMutateOp": "addNode.byClass",
        "args": {
          "nodeClassPath": "/Script/UnrealEd.EdGraphNode_Comment"
        },
        "source": "loomle_catalog",
        "coverage": "curated",
        "determinism": "stable"
      },
      "alternatives": []
    }
  ],
  "diagnostics": []
}
```

### 9.3 Required plan fields

Every resolved item should expose:

- `realizationKind`
- `preferredMutateOp`
- `args`
- `source`
- `coverage`
- `determinism`

Recommended enumerations:

- `source`: `typed_discovery | loomle_catalog | generic_fallback | mixed`
- `coverage`: `contextual | curated | partial`
- `determinism`: `stable | context_sensitive | ephemeral`

### 9.4 Single-op vs multi-step plans

Some semantic operations map cleanly to one mutate op.

Examples:

- add a Blueprint branch node
- add a comment node

Other operations, especially in PCG, may require an ordered batch:

1. add a node
2. configure nested settings
3. connect specific semantic pins
4. apply layout or compile

For that reason, `preferredPlan` should allow either:

- a single-op realization via `preferredMutateOp` + `args`
- a multi-step realization via ordered `steps[]`

Illustrative multi-step shape:

```json
{
  "realizationKind": "pipeline_insert",
  "source": "loomle_catalog",
  "coverage": "curated",
  "determinism": "context_sensitive",
  "steps": [
    {
      "op": "addNode.byClass",
      "clientRef": "filter",
      "args": {
        "nodeClassPath": "/Script/PCG.PCGFilterByTagSettings"
      }
    },
    {
      "op": "connectPins",
      "args": {
        "from": { "nodeRef": "$upstream", "pinName": "Out" },
        "to": { "nodeRef": "filter", "pinName": "In" }
      }
    }
  ]
}
```

This keeps the public planning model aligned with the existing ordered batch
shape of `graph.mutate`.

### 9.5 Optional plan enrichments

Some graph domains need more than a mutate-op name to be practical.

Recommended optional fields:

- `settingsTemplate`: nested settings skeleton or key required settings fields
- `pinHints`: semantic pin-role guidance such as primary input/output paths,
  default continuation outputs, and insertion-specific endpoints
- `verificationHints`: suggested structural checks after apply, ideally with
  machine-usable kinds and targets
- `runtimeVerification`: optional note when structural success does not imply
  trustworthy runtime output visibility

These fields are particularly useful for PCG.

### 9.6 Failure shape

An unresolved item should remain structured:

```json
{
  "opId": "core.reroute",
  "resolved": false,
  "compatibility": {
    "isCompatible": false,
    "reasons": ["requires_pin_context"]
  },
  "remediation": {
    "requiredContext": ["from_pin"],
    "missingFields": ["context.fromPin.nodeId", "context.fromPin.pinName"],
    "nextAction": "re-run graph.ops.resolve with context.fromPin",
    "fallbackKind": "direct_mutate"
  },
  "reason": "incompatible_context"
}
```

`remediation` should be treated as a first-class contract field rather than
free-form prose. Its job is to keep agents and skills inside the semantic
planning loop after an unresolved result.

## 10. Preferred Planning Policy

`graph.ops.resolve` should prefer the most deterministic realization that LOOMLE
can express.

Priority order:

1. stable direct realization, for example `addNode.byClass`
2. stable catalog-backed realization
3. context-sensitive typed discovery with deterministic class output
4. ephemeral editor-action binding only when no better realization exists

This matters because the design should not simply repackage `actionToken` under
new names.

If the implementation still depends on editor-native action binding internally,
that dependency should stay behind resolver internals rather than reappearing as
a public mutate contract.

## 11. Domain Guidance

### 11.1 Blueprint

Blueprint should support the richest resolver.

Recommended behavior:

- allow `fromPin` context
- use typed editor discovery when needed
- prefer stable class-based realization when a deterministic class mapping is
  available
- only fall back to action-binding plans when required by editor behavior

Blueprint is the main place where context-sensitive compatibility signals
matter.

### 11.2 Material

Material should start as a curated semantic catalog with compatibility checks.

Recommended behavior:

- expose a stable set of Material-semantic `opId` values
- use resolve to map those ops to material expression classes and required args
- treat editor-menu parity as a non-goal for the first version

Material can share the same output contract without pretending to have the same
discovery model as Blueprint.

### 11.3 PCG

PCG should lean into domain semantics instead of menu emulation.

Recommended behavior:

- expose pipeline-oriented semantic operations
- return plans that map to known node classes or node templates
- prefer ordered multi-step plans over pretending every operation is one node
- define explicit insertion behavior for upstream/downstream preservation
- include compatibility reasons when topology or pin-type assumptions block the
  requested operation
- expose enough settings and pin guidance that agents can avoid trial-and-error
- treat resolved settings visibility in `graph.query` as a dependency, not a
  nice-to-have

PCG should use the same public framework, but its resolver should remain
independent from Blueprint menu logic.

Examples of PCG-specific plan support that should be considered first-class:

- nested settings templates for projection/filter/spawner nodes
- semantic pin-role hints that match actual runtime pins, not abstract defaults
- verification hints for readback when the runtime output surface is known to be
  incomplete
- insertion semantics that can preserve or intentionally rewrite downstream
  edges
- composition semantics for turning multiple semantic ops into one local
  pipeline segment

### 11.4 PCG insertion and composition semantics

PCG should not treat `pipeline_insert` as a loose synonym for “spawn from this
source pin”.

If a plan claims insertion semantics, it should be able to express at least one
of these behaviors explicitly:

- preserve downstream by rewiring `upstream -> new node -> previous downstream`
- branch intentionally without claiming downstream preservation
- require edge-level context before safe insertion is possible

Likewise, batch resolution for multiple PCG semantic ops should eventually
distinguish between:

- independent per-op plans
- one composed local pipeline segment

If composition is out of scope for a request, the response should say so rather
than leaving callers to infer it.

## 12. Runtime Validation Boundary

Graph-semantic planning and runtime result inspection should stay separate.

`graph.ops.resolve` may include verification hints, but it should not claim to
solve PCG runtime result introspection by itself.

If runtime output inspection remains unreliable for common PCG workflows, a
separate capability such as `graph.verify` is a better design path than
overloading resolve.

## 13. Public Graph-Semantic Boundary

Historical public graph-semantic surface:

- `graph.ops`
- `graph.ops.resolve`
- `graph.mutate`

If editor-native action discovery remains useful internally, it should remain
an implementation detail of resolvers rather than a public contract.

Historical positioning:

- `graph.ops`: what stable semantic operations does LOOMLE know
- `graph.ops.resolve`: how would those operations be realized here

Current preferred boundary:

- workspace-local graph references define the primary semantic surface for agents
- `graph.mutate` is the primary execution surface
- `graph.query` and `graph.verify` provide validation
- `graph.ops` may supplement that flow as a live curated listing
- `graph.ops.resolve` is optional and secondary

## 14. Migration Plan

Historical migration plan:

- define tool names and response contracts
- add protocol docs
- hard-cut the legacy action-token public surface

### Phase 2: Blueprint first implementation

- implement `graph.ops` catalog for Blueprint
- implement `graph.ops.resolve` for a narrow stable set of Blueprint ops
- prefer class-based plans

### Phase 3: Material and PCG expansion

- add curated domain catalogs
- add per-domain resolve logic
- expand compatibility signaling

### Historical Phase 4: documentation repositioning

- keep user-facing workflows and examples centered on `graph.ops` and
  `graph.ops.resolve`
- guide agents only toward `graph.ops` and `graph.ops.resolve`
- keep `graph.mutate` limited to stable realization ops

Current documentation direction:

- keep user-facing workflows and examples centered on workspace-local
  graph-specific directories
- teach agents to begin with `GUIDE.md`, then open `SEMANTICS.md`, then use
  catalogs/examples when needed
- keep `graph.mutate` limited to stable primitive realization ops

## 15. Minimum Viable v1

Historical graph-ops v1 proposal:

- `graph.ops` returns curated stable ops per graph type
- `graph.ops.resolve` supports batch resolution
- Blueprint supports a small stable core:
  - `core.comment`
  - `core.reroute`
  - `bp.flow.branch`
- Material supports a small curated core
- PCG supports a small curated core
- the public semantic path is fully centered on `graph.ops` and
  `graph.ops.resolve`

Current minimum viable direction:

- workspace-local graph references are the agent-facing teaching surface
- `graph.mutate` remains primitive
- `graph.query` and `graph.verify` provide validation
- optional live catalogs remain curated rather than exhaustive

Additional PCG v1 expectations:

- every PCG plan can carry `pinHints` when output pins are non-obvious
- every PCG plan can carry `settingsTemplate` for nested settings payloads
- every PCG plan can carry `verificationHints` for post-mutate readback

### 15.1 Blueprint v1 op set

Blueprint is the strongest candidate for early semantic coverage.

Recommended required v1 ops:

- `core.comment`
- `core.reroute`
- `bp.flow.branch`

Recommended optional Blueprint stretch ops:

- `bp.flow.sequence`
- `bp.exec.delay`
- `bp.debug.print_string`

Expected realization profile:

- `core.comment`: usually `addNode.byClass` with stable realization
- `core.reroute`: context-sensitive and often requires `fromPin`
- `bp.flow.branch`: `addNode.byClass` with stable realization

### 15.2 Material v1 op set

Material should start with a small curated set that maps directly to currently
supported expression classes.

Recommended required v1 ops:

- `mat.constant.scalar`
- `mat.constant.vector3`
- `mat.math.multiply`
- `mat.param.scalar`
- `mat.param.vector`
- `mat.texture.sample`

Recommended optional Material stretch ops:

- `mat.math.add`
- `mat.math.lerp`
- `mat.utility.clamp`

Expected realization profile:

- almost always `addNode.byClass`
- frequent follow-up `connectPins`
- some flows naturally produce multi-step plans, for example multiplying two
  values and wiring the result into a root material property

### 15.3 PCG v1 op set

PCG should start with a deliberately small set that matches the current curated
class-backed node inventory and avoids pretending to cover the full PCG domain.

Recommended required v1 ops:

- `pcg.create.points`
- `pcg.meta.add_tag`
- `pcg.route.data_by_tag`
- `pcg.sample.surface`
- `pcg.transform.points`
- `pcg.sample.spline`
- `pcg.source.actor_data`
- `pcg.spawn.static_mesh`

Recommended optional PCG stretch ops:

- `pcg.route.data_if_attribute_value`
- `pcg.project.surface`
- `pcg.spawn.actor`

Expected realization profile:

- commonly `addNode.byClass`
- often followed by `connectPins`
- often benefits from `steps[]`, `pinHints`, and `settingsTemplate`
- may need `verificationHints` where runtime output visibility is weaker than
  structural readback

Known vNext pressure after the first release:

- insertion plans should preserve or explicitly account for downstream edges
- `pinHints` should reflect actual runtime outputs such as
  `InsideFilter` / `OutsideFilter`, not generic placeholders
- multi-item resolve should eventually distinguish independent resolution from
  composed pipeline planning

### 15.4 Cross-graph expectations

The v1 sets do not need equal breadth.

What should be equal across all three graph domains is:

- the tool names
- the response shape
- the `opId` contract
- the compatibility and plan metadata

What may legitimately differ is:

- number of supported `opId` values
- resolver depth
- frequency of multi-step plans
- confidence and coverage signals

## 16. Open Questions

1. Should `graph.ops` accept optional graph context in v1, or stay domain-only?
2. Should `graph.ops.resolve` return exactly one preferred plan or ranked
   alternatives by default?
3. Should `preferredPlan.args` be mutate-op-specific, or should plan payloads
   use an intermediate abstraction that mutate adapters translate later?
4. How much compatibility detail should be exposed before mutate time?
5. Should a resolved plan be allowed to emit ordered `steps[]` in v1, or should
   that wait until after the initial single-op rollout?
6. Should PCG runtime validation become a separate tool family rather than a
   resolve concern?
7. Should edge-scoped context become the preferred insertion contract for PCG
   and some Blueprint rewrite cases?
8. How should the protocol distinguish independent multi-item resolution from
   composed multi-op pipeline planning?
