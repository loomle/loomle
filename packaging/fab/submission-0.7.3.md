# Fab 0.7.3 Resubmission Copy

## Listing title

LOOMLE MCP for Unreal with SAL - Structured, Verifiable AI Editing

## Short description

Connect Codex, Claude, and other MCP agents to the live Unreal Editor through
three core SAL calls. Read and safely edit supported Unreal objects, load
resident Agent Skills on demand, and work across projects from one local MCP
configuration.

## Description

LOOMLE MCP connects AI agents to Unreal Engine 5.7 through a native Editor
Bridge and a bundled, self-contained local MCP Client.

Instead of expanding into hundreds of tool-per-action endpoints, Loomle uses
SAL—Structured Agent Language—and three core object calls:

- `sal_schema` discovers the live object model, domain guidance, and exact
  capabilities available in the Editor.
- `sal_query` reads live Unreal state, structure, relationships, execution
  flow, and precise layout geometry.
- `sal_patch` composes ordered edits, dry-runs and validates the plan, then
  applies it as one coherent change.

Resident Agent Skills are discovered and loaded through the same MCP
connection. They add domain-specific workflow guidance without requiring a
separate Codex-, Claude-, or host-specific Skill installation. Loomle 0.7.3
includes `format-unreal-blueprints`, which uses live node and Pin geometry,
move-only dry runs, and post-apply readback to improve Blueprint Graph layout
without changing graph behavior.

The current SAL interface supports Asset, Blueprint, Class, Graph, StateTree,
and Widget domains while preserving native Unreal names, values, identities,
and semantics.

Configure the bundled Loomle Client once in an MCP host. It discovers multiple
live Loomle-enabled projects and binds the selected project explicitly for the
current session, so switching projects does not require another MCP
configuration.

Supported environment:

- Unreal Engine 5.7
- macOS on Apple Silicon
- Windows on x64
- Editor-only engine plugin

The Client is self-contained. No separate Python, Node.js, `uv`, global Loomle
installation, host-specific Skill installation, or background daemon is
required.

## Version notes

Loomle 0.7.3 makes professional Agent Skills native to Loomle MCP.

- Adds the read-only `agent_skill` call for discovering and loading resident
  workflow guidance on demand.
- Includes the `format-unreal-blueprints` Skill with live geometry reading,
  move-only dry runs, absolute placement, and readback verification.
- Organizes the Unreal object workflow around three core SAL calls:
  `sal_schema`, `sal_query`, and `sal_patch`.
- Keeps Skills vendor-neutral and available through MCP without a separate
  host-specific installation.
- Supports Unreal Engine 5.7 on Apple Silicon macOS and x64 Windows.

## Executable declaration

This source package contains two builds of the same self-contained Loomle MCP
Client for the supported platforms:

- `LoomleBridge/Resources/Loomle/darwin-arm64/loomle`
- `LoomleBridge/Resources/Loomle/win32-x64/loomle.exe`

The Client is a required companion to the Unreal source plugin. It is not an
installer and is not launched by the plugin. The user configures Codex,
Claude, or another MCP host to start the matching Client with the `mcp`
argument. The Client communicates with that host through standard input and
output, discovers Loomle-enabled Unreal Editor sessions on the local machine,
and connects to the explicitly selected project through Loomle's
platform-native local RPC transport.

The Client runs only when invoked by the MCP host. It does not install files,
register an auto-start item, create a system service, or maintain a background
daemon. It exits when its MCP process ends. Its only outbound network request
is a short HTTPS read of `https://loomle.ai/releases.json` when the user or
agent calls `status`, used solely to report whether a newer Loomle release is
available. No project content is sent with that request, and Loomle includes no
telemetry or analytics reporting.

Both executable builds are produced from the Client source in the Loomle
repository. The source package also includes the complete Unreal Bridge C++
source and excludes Unreal-generated `Binaries`, `Intermediate`, and `Saved`
output.
