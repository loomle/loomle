# Loomle Documentation

This directory is for current, trusted Loomle design documentation.

Loomle documents are written for both users and developers. They should be
concise and explicit about three things:

- Design intent: what agent workflow the interface supports and where its
  boundary is.
- Interface schema: tool inputs, outputs, edit operations, and error responses.
- UE implementation model: the Unreal Engine concepts and execution paths that
  Loomle maps to.

Historical design notes, old specs, and exploratory material are archived under
`archive/legacy/`. They may be useful background, but they are not the current
interface contract.

## Current Documents

- `BLUEPRINT_INTERFACE_DESIGN.md`: current Blueprint interface design.

## Planned Documents

Future formal documents should follow the same shape and replace archived
material as each area is redesigned:

- `MATERIAL_INTERFACE_DESIGN.md`
- `PCG_INTERFACE_DESIGN.md`
- `RUNTIME_INTERFACE_DESIGN.md`
- `MCP_INTERFACE_DESIGN.md`
