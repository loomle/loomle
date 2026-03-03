# Loome Design Docs

## Naming (Canonical)

- External protocol/module: `Loome Graph`
- Internal runtime/control plane: `Loome Bridge`
- Per-graph implementation layer: `Adapter`
- Blueprint adapter canonical name: `LoomeBlueprintAdapter`
- Python canonical symbol (target): `unreal.LoomeBlueprintAdapter`

## Documents

1. `LOOME_GRAPH.md`
- MCP contract (`graph/query/mutate/watch`), data schema, error model.

2. `LOOME_BLUEPRINT_ADAPTER.md`
- Blueprint adapter operation contract and mapping to current UE implementation.

3. `LOOME_BRIDGE.md`
- Runtime architecture, tool routing, adapter orchestration, compatibility policy.

## Read Order

1. `LOOME_GRAPH.md`
2. `LOOME_BLUEPRINT_ADAPTER.md`
3. `LOOME_BRIDGE.md`
