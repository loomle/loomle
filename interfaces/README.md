# Loomle Interfaces

This workspace owns Loomle's static Unreal Engine interface definitions. It is
the layer between the general-purpose SAL language and the UE behavior exposed
by Loomle Bridge.

- [`GUIDE.md`](GUIDE.md) is the compact resident guide published exactly once
  as the `sal_schema` MCP tool description. It also explains the separate
  session-level `project` binding that selects which UE project SAL operates on.
- `asset.md`, `blueprint.md`, `class.md`, `graph.md`, `state_tree.md`,
  `widget.md`, `level.md`, `pcg.md`, and `pcg_component.md` define the nine
  static Domain interfaces. The three Scene/PCG cards are Query-only in this
  release.
- `src/generated/catalog.ts` embeds those documents for the standalone Client.

The documents describe Loomle's UE-facing capabilities. Core grammar is owned
by the resident guide; a card closes one Domain's Target, identity, Query,
Patch, Palette, and handoff surface. Dynamic `with schema` remains
authoritative for a concrete Target or object.

Run `npm run generate` after editing the guide or an interface document. Run
`npm test` to regenerate the catalog, compile the package, and verify that every
catalog entry matches its source document.
