# Loomle MCP for Unreal

Loomle connects AI agents to a live Unreal Editor through a compact,
structured, and verifiable editing language. It gives the agent eight stable
MCP calls while SAL (Structured Agent Language) carries detailed Unreal
queries, schema discovery, stable references, dry runs, edits, and readback.

## Features

- Inspect and edit Blueprints, Widgets, assets, and the active editor context.
- Discover exact schemas and Palette actions instead of guessing Unreal names.
- Keep object references stable across queries and multi-step edits.
- Dry-run SAL patches before applying them, then verify the result by reading
  it back.
- Bind one Client installation to multiple Unreal projects without maintaining
  a separate MCP configuration for every project.
- Use the bundled Agent Skills for repeatable Blueprint, Widget, Python, and
  PIE workflows.

## Requirements

- Unreal Engine 5.7 or 5.8.
- macOS on Apple Silicon or Windows on x64.
- The matching Loomle Bridge plugin installed in Unreal Engine and enabled in
  the project.

The desktop extension contains the Loomle MCP Client. The Unreal Bridge is a
separate native plugin because it must run inside the matching Unreal Engine
version.

## Installation

1. Install Loomle from your MCP host's directory, or open this `.mcpb` file in
   a compatible desktop host.
2. Download the Loomle Bridge package matching Unreal Engine 5.7 or 5.8 from
   [GitHub Releases](https://github.com/loomle/loomle/releases/latest), or
   install the Fab-approved version from the
   [Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428).
3. Copy the complete `LoomleBridge` directory to
   `<UE_5.x>/Engine/Plugins/Marketplace/LoomleBridge`.
4. Open the Unreal project, enable Loomle Bridge if prompted, and restart the
   Editor once.

No API key or Loomle account is required. The extension runs locally and uses
the Node.js runtime supplied by compatible desktop hosts.

## Examples

### 1. Inspect a Blueprint before changing it

**Prompt**

> Inspect the active Blueprint, summarize its variables and Event Graph, and
> tell me which project you connected to.

**Expected behavior**

Loomle checks `status`, binds only when the project is unambiguous, reads the
active editor context, and queries the Blueprint with SAL. It returns a compact
object representation without modifying the project.

### 2. Dry-run and verify a Blueprint edit

**Prompt**

> Add an `IsReady` Boolean variable to this Blueprint. Dry-run the change
> first, show me any validation errors, then apply it and verify the result.

**Expected behavior**

Loomle discovers the exact Blueprint schema, sends an ordered SAL patch with
`dryRun=true`, reports validation feedback without mutation, applies the same
validated plan after approval, and reads the Blueprint back to verify it.

### 3. Trace execution flow without rearranging the graph

**Prompt**

> Trace the execution flow from BeginPlay in the active Blueprint and explain
> the branches. Do not modify or rearrange any nodes.

**Expected behavior**

Loomle resolves the active graph and returns the connected execution path with
stable node references. Because this is a query, the Blueprint remains
unchanged.

## Configuration and project selection

Most users need no extension settings. Keep the target Unreal project open. If
several projects are available, ask the agent to list Loomle projects and bind
the intended one. The binding is local to the MCP session and remains stable
across Editor restarts.

## Privacy

The Loomle Client and Bridge run locally. Loomle does not receive Unreal
project content, MCP requests, Blueprint data, generated code, or editor
activity. Published Clients make limited version-check requests to their own
distribution channel and GitHub; those requests do not include project or MCP
contents.

Read the complete [Loomle Privacy Policy](https://loomle.ai/privacy/).

## Support

- Documentation: [loomle.ai/install.html](https://loomle.ai/install.html)
- Issues and support: [github.com/loomle/loomle/issues](https://github.com/loomle/loomle/issues)
- Source code: [github.com/loomle/loomle](https://github.com/loomle/loomle)

Loomle is licensed under the MIT License. Third-party notices are included in
`THIRD_PARTY_NOTICES.txt`.
