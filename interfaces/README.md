# Loomle Interfaces

This workspace owns Loomle's static Unreal Engine interface definitions. It is
the layer between the general-purpose SAL language and the UE behavior exposed
by Loomle Bridge.

- [`GUIDE.md`](GUIDE.md) is the compact resident guide published exactly once
  as the `sal_schema` MCP tool description.
- `asset.md`, `blueprint.md`, `class.md`, `graph.md`, and `widget.md` define the
  corresponding static Query and Patch interfaces.
- `src/generated/catalog.ts` embeds those documents for the standalone Client.

The documents describe Loomle's UE-facing capabilities; they do not add SAL
grammar or replace dynamic `with schema` information returned for a concrete UE
object. Unavailable interfaces are omitted by the Client rather than reported
with separate status metadata.

Run `npm run generate` after editing the guide or an interface document. Run
`npm test` to regenerate the catalog, compile the package, and verify that every
catalog entry matches its source document.
