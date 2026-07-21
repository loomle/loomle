# Blueprint Graph Layout

## Status

Current SAL reads stored Node layout with `with layout` and can move exact
Nodes explicitly. It does not automatically format a graph or region. This
document records the UE 5.7 behavior and the questions that must be resolved
before an automatic layout operation is designed.

## Intent

Automatic layout should make a recently edited Blueprint region readable
without changing graph behavior. It is a visual mutation over existing Nodes,
not an alternative creation, connection, or graph-transformation API.

## UE 5.7 Source Facts

Relevant source:

- `Editor/GraphEditor/Private/GraphEditorActions.cpp`
- `Editor/GraphEditor/Private/SGraphEditorImpl.h`
- `Editor/GraphEditor/Private/SGraphEditorImpl.cpp`
- `Editor/GraphEditor/Private/SGraphPanel.cpp`
- `Editor/Kismet/Private/BlueprintEditor.cpp`
- `Runtime/Engine/Private/EdGraph/EdGraphSchema.cpp`

The Blueprint Editor maps native Graph Editor commands for aligning selected
Nodes, distributing them horizontally or vertically, and straightening
connections. These commands delegate through the focused `SGraphEditorImpl` and
run inside `FScopedTransaction`.

The native helpers are editor-view operations:

- alignment uses the current selection and measured Node bounds;
- distribution requires more than two selected Nodes and preserves the first
  and last positions while spacing the interior Nodes;
- straightening uses selected or hovered Pins and the live `SGraphPanel` Pin
  widgets to align connected Nodes;
- Node size comes from Slate bounds when available, with the Node's stored size
  as a fallback;
- movement calls `UEdGraphSchema::SetNodePosition`, whose default
  implementation calls `Modify()` and writes `NodePosX` and `NodePosY`.

These paths do not provide one headless, deterministic API that accepts an
arbitrary subgraph and produces a complete layout. Some native behavior depends
on the open Graph Editor, selection, hover state, and measured Slate geometry.
That distinction must remain explicit in Loomle.

## Current SAL Boundary

Graph Object Text already exposes stored positions as part of the graph model,
and Graph Patch can move an exact Node to an explicit point. Automatic layout
must compose with that model rather than becoming a separate public tool. It
must not create Nodes, change links, add reroutes, or silently choose a region
from editor focus.

Compilation and save remain terminal operations on the owning Blueprint, not
implicit side effects of layout.

## Design Questions

The SAL operation must be discussed before implementation:

1. Should Loomle expose faithful native selection operations such as align,
   distribute, and straighten; a deterministic Loomle region formatter; or
   both as clearly distinct schema-discovered operations?
2. How is the region addressed: an explicit Node set, a bounded execution-flow
   traversal, or another exact selector returned by schema?
3. Can a headless algorithm obtain authoritative Node and Pin geometry when the
   Graph is not open, or must it report that native visual geometry is
   unavailable?
4. Which Nodes anchor the result, and how are cycles, shared downstream Nodes,
   comments, reroutes, latent paths, and data-only edges handled?
5. What deterministic ordering and spacing rules keep repeated layout
   idempotent across machines and editor sessions?
6. How should dry run express planned before/after Node positions in ordinary
   Object Text without inventing a second result language?
7. Is automatic collision avoidance part of the first operation, or should the
   first version stay limited to native align/distribute/straighten semantics?

## Safety Boundary

Any eventual operation must:

- resolve every target Node inside one exact Graph;
- validate the full move plan before changing positions;
- use UE transactions and schema-aware Node movement;
- preserve Nodes, Pins, links, defaults, comments, and graph semantics;
- produce the same plan in dry run and apply;
- return the Nodes it moved with their resulting stored layout;
- refuse partial application when required geometry or identity is missing.

Whole-graph formatting, reroute synthesis, wire routing, comment fitting, and
behavioral graph rewrites remain separate design topics until explicitly
specified.
