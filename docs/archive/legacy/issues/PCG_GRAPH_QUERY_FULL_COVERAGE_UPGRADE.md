# Local Issue

## Title

Upgrade PCG `graph.query` into the primary full-coverage node readback surface

## Status

- State: active
- Scope: PCG query-surface upgrade
- Canonical location: this local issue document
- Related GitHub issues:
  - `#128` Spawn Actor documentation/fallback gap
  - `#130` parent GitHub tracking issue

## Summary

For PCG, `graph.query` should become the primary read surface for live node
truth, not just a topology-plus-major-settings surface.

The direction is not to mirror arbitrary Unreal objects. The direction is to
make the important, agent-relevant node truth queryable in a graph-native form.

The key product rule is:

- prefer `graph.query`
- keep graph boundaries graph-native
- keep fallback narrow and explicit

## Core Surface Model

Every PCG settings node should have one primary query truth surface:

1. `pin_default`
- Simple editable values are readable through pins or synthetic query defaults.

2. `effectiveSettings`
- Complex graph-local settings are readable through structured query output.

3. `childGraphRef`
- Structural second hops are readable as graph-native references.

4. `residual_gap`
- Only a narrow leftover set still requires fallback.

This should become the default mental model for PCG inspection in `LOOMLE`.

## What Already Exists

Today, `LOOMLE` already has the right building blocks:

- graph topology is queryable
- pins and many default values are queryable
- some nodes already expose structured `effectiveSettings`
- structural cross-graph cases already use `childGraphRef`

### Existing `effectiveSettings` Nodes

These nodes already expose structured query readback today:

1. `UPCGGetActorPropertySettings`
- actor selector
- component selection flags
- component class
- property name
- output attribute naming

2. `UPCGGetSplineSettings`
- actor selector
- component selector
- mode
- data filter

3. `UPCGStaticMeshSpawnerSettings`
- mesh selector type
- mesh selector parameter class
- out-attribute naming
- grouped mesh-selector details

These are the current baseline and should be treated as the first proven
`effectiveSettings` family, not as isolated one-off serializers.

## The Main Gap

The main gap is not that PCG node truth is fundamentally unreadable. The main
gap is that many live node-instance settings are already known to the catalog
and available on Unreal objects, but are not yet surfaced consistently through
`graph.query`.

That means today the product still falls back too often for:

- actor and component targeting
- spawned object identity
- override mappings
- grouped spawn configuration
- data layer and HLOD settings

## Design Direction

### 1. Treat `graph.query` as the default PCG readback contract

Agent guidance should assume:

- use `graph.query` first
- stay in `graph.query` if the needed truth is already surfaced there
- do not route routine PCG node inspection through `execute`

### 2. Expand generic query coverage aggressively

Many PCG nodes do not need custom serializers. They need broader generic query
coverage for ordinary editable settings such as:

- scalar values
- enums
- names and strings
- object and class references
- common small structs
- selector-like leaf values

This is the default path for ordinary node families.

### 3. Reserve `effectiveSettings` for high-value structured families

`effectiveSettings` should not mean “custom serializer for everything”.

It should mean:

- the node contains settings that are too structured, too grouped, or too
  important to be represented as flat pin defaults
- the agent needs a coherent, structured, near-complete readback of the node’s
  effective configuration

### 4. Keep graph boundaries graph-native

When a node points to another graph:

- use `childGraphRef`
- follow with graph-native queries

This is not a fallback gap.

### 5. Keep fallback narrow

After this upgrade, `execute` should remain available only for:

- temporary rollout gaps
- clearly documented residual cases
- information that is not yet productized in query form

It should not be the normal companion for routine PCG inspection.

## Priority Tiers

### Tier 0: Existing `effectiveSettings` Baseline

These are already in place and should be expanded, not redesigned:

- `UPCGGetActorPropertySettings`
- `UPCGGetSplineSettings`
- `UPCGStaticMeshSpawnerSettings`

### Tier 1: Highest-Value Upgrade Targets

These should be upgraded first because they are both high-frequency and heavily
fallback-prone.

1. `UPCGSpawnActorSettings`
2. `UPCGDataFromActorSettings`
3. `UPCGApplyOnActorSettings`

Why:

- actor-oriented truth is a top agent need
- selectors and target identity matter
- grouped settings and override mappings matter
- fallback dependence is currently too high

### Tier 2: Second-Wave Structured Spawn Families

These are also strong `effectiveSettings` candidates:

1. `UPCGSpawnSplineSettings`
2. `UPCGSpawnSplineMeshSettings`
3. `UPCGSkinnedMeshSpawnerSettings`

Why:

- they combine grouped spawn settings with asset/class/selector semantics
- they are too structured to be represented cleanly as flat defaults

### Tier 3: Existing Families That Need Completeness Work

These already have `effectiveSettings`, but they still need broader and more
complete coverage:

- `UPCGGetActorPropertySettings`
- `UPCGGetSplineSettings`
- `UPCGStaticMeshSpawnerSettings`

### Tier 4: Families That Should Stay Primarily Generic

These should mostly stay on the generic `pin_default` path:

- transform nodes
- ratio/select nodes
- ordinary filter and route nodes
- most meta/create nodes with simple editable values

They should only move into `effectiveSettings` if a concrete structured truth
need appears that generic query coverage cannot meet.

## What “Complete `effectiveSettings`” Means

For nodes that enter `effectiveSettings`, the goal is not a tiny summary. The
goal is a near-complete structured readback of the node’s effective
configuration from an agent’s point of view.

This does **not** mean mirroring the full UObject.

It **does** mean surfacing the settings needed to answer:

- what does this node target?
- what does it spawn / read / write / apply?
- which selectors and grouped settings shape its behavior?
- what important overrides are in effect?

## Detailed Target Model: `UPCGSpawnActorSettings`

`UPCGSpawnActorSettings` is the highest-priority target and should become a
standard `graph.query` success case.

Its `effectiveSettings` should aim to cover these groups completely:

### 1. Template Identity

- template actor identity
- template actor class identity
- other object/class references that determine what gets spawned

### 2. Spawn Behavior

- spawn mode
- attach/root behavior
- attribute-driven spawn controls
- other mode flags that materially affect spawn outcome

### 3. Property Override Mappings

- property override descriptions
- structured source-to-target mapping details
- override presence should not be reduced to a boolean

### 4. World Placement Groups

- `DataLayerSettings`
- `HLODSettings`
- any other grouped world-facing configuration that changes how spawned actors
  land in the level

### 5. Selector-Backed Truth

If this node contains selector-backed settings, they should be surfaced as
structured objects, not flattened strings.

## Detailed Target Model: `UPCGDataFromActorSettings`

The complete `effectiveSettings` model should cover:

- actor targeting rules
- actor selector structure
- inclusion/exclusion behavior
- child/component-related targeting behavior
- any grouped actor-source behavior that materially affects output

## Detailed Target Model: `UPCGApplyOnActorSettings`

The complete `effectiveSettings` model should cover:

- target actor identity rules
- selector-backed targeting
- grouped apply behavior
- attribute/property application mapping
- any flags that materially change which actors are affected and how

## Detailed Target Model: Tier 2 Spawn Families

These nodes should be treated with the same completeness goal:

- `UPCGSpawnSplineSettings`
- `UPCGSpawnSplineMeshSettings`
- `UPCGSkinnedMeshSpawnerSettings`

Their `effectiveSettings` should cover:

- spawned asset/class identity
- grouped spawn parameters
- selector-backed targeting or sourcing
- mode flags
- override groups where relevant

## Tests And Validation

This issue should be implemented together with tests, not ahead of them.

The most important test directions are:

1. generic query expansion tests
- simple editable values should become query-readable without node-specific
  fallback

2. `effectiveSettings` completeness tests
- high-priority nodes should expose a broad structured settings object

3. selector truth tests
- selector-backed values should round-trip as structured truth, not just text

4. workflow truth tests
- the structured settings should remain visible in realistic PCG workflows

5. residual-gap accounting
- the set of cases still requiring fallback should shrink and stay explicit

## Rollout Phases

### Phase A: Lock the surface model

- keep the four-surface model explicit:
  - `pin_default`
  - `effectiveSettings`
  - `childGraphRef`
  - `residual_gap`

### Phase B: Expand generic `pin_default`

- cover more ordinary field shapes broadly
- reduce unnecessary pressure on custom serializers

### Phase C: Upgrade Tier 1 node families

- `UPCGSpawnActorSettings`
- `UPCGDataFromActorSettings`
- `UPCGApplyOnActorSettings`

### Phase D: Upgrade Tier 2 node families

- `UPCGSpawnSplineSettings`
- `UPCGSpawnSplineMeshSettings`
- `UPCGSkinnedMeshSpawnerSettings`

### Phase E: Completeness pass on Tier 0 baseline nodes

- widen and harden existing `effectiveSettings` coverage

## Out of Scope

This issue does not mean:

- mirror every PCG UObject field into query
- turn every PCG node into a custom serializer
- eliminate `execute` entirely
- replace graph-native structural references with object dumps

## Acceptance Criteria

This issue is complete when:

- `graph.query` is the primary answer for nearly all PCG node inspection
- every PCG settings node has a clear intended query truth surface
- ordinary editable settings are broadly readable without custom node-specific
  work
- complex high-value families are surfaced through structured
  `effectiveSettings`
- structural second hops stay graph-native through `childGraphRef`
- `UPCGSpawnActorSettings` no longer requires routine fallback just to read
  property override mappings and grouped spawn configuration
- fallback guidance is narrow, explicit, and reserved for true residual gaps
