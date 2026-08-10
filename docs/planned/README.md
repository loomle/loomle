# Planned Designs

Documents in this directory describe capabilities that are not part of the
current 0.7 public interface. Each document records verified UE 5.7 behavior,
the current SAL boundary, and the unresolved questions that require discussion
before implementation.

Implementation requires a confirmed SAL/interface design and current UE source
validation. A planned document is not evidence that the TypeScript Client or
Bridge route is publicly available.

Current planned designs:

- `BLUEPRINT_USER_DEFINED_STRUCT_DESIGN.md`: UE-native UserDefinedStruct
  identity, field, validation, and mutation design.
- `SCENE_PCG_DOMAIN_FAMILY_DESIGN.md`: joint ownership, result-only Target
  navigation, transaction, persistence, and publication architecture for Level
  authoring, PCG Graphs, PCG Components, Python-managed live World workflows,
  and typed PCG execution.
- `LEVEL_DOMAIN_DESIGN.md`: persistent source-map Target, Actor/Component
  identity, World Partition and Level Instance semantics, authored Patch, and
  exact Level save closure.
- `PCG_DOMAIN_DESIGN.md`: authored asset-backed PCG Graph Target, Node/Pin
  identity, certified topology and Settings mutation, transaction, and Graph
  save.
- `PCG_RUNTIME_DOMAIN_DESIGN.md`: level-owned PCG Component configuration,
  Bridge-private PCG World epoch/tickets, and typed PCG execution on the shared
  async lifecycle; no public live-World SAL Domain.
- `PYTHON_FALLBACK_DESIGN.md`: confirmed high-privilege Unreal Editor Python
  `run`/`poll` fallback contract, explicit PIE/SIE workflow boundary, and the
  planned `sal.object()` projection extension.
