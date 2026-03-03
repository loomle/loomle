# Loomle Design Docs

## Naming (Canonical)

- External protocol/module: `Loomle Graph`
- Internal runtime/control plane: `Loomle Bridge`
- Per-graph implementation layer: `Adapter`
- Blueprint adapter canonical name: `LoomleBlueprintAdapter`
- Python canonical symbol (target): `unreal.LoomleBlueprintAdapter`

## Documents

1. `LOOMLE_GRAPH.md`
- bridge contract (`graph/query/mutate/watch`), data schema, error model.

2. `LOOMLE_BLUEPRINT_ADAPTER.md`
- Blueprint adapter operation contract and mapping to current UE implementation.

3. `LOOMLE_BRIDGE.md`
- Runtime architecture, tool routing, adapter orchestration, compatibility policy.

## Read Order

1. `LOOMLE_GRAPH.md`
2. `LOOMLE_BLUEPRINT_ADAPTER.md`
3. `LOOMLE_BRIDGE.md`
