# Engine Layer

This directory will become the canonical home for Unreal-side LOOMLE runtime code.

Planned contents:
- `LoomleBridge/`

Migration note:
- Current Unreal plugin source still lives at the repository root in `Source/`, `Config/`, and `LoomleBridge.uplugin`.
- Those paths remain authoritative until the packaging and install flow is updated to read from `engine/`.
