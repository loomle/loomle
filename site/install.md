---
layout: default
title: Install
nav_order: 2
description: Install Loomle 0.7 for Unreal Engine 5.7 or 5.8 on macOS or Windows and connect an MCP host.
---

# Install

The latest stable Loomle release is available for Unreal Engine 5.7 and 5.8.
Choose the package matching your exact engine version. Each package contains
the Unreal Bridge binaries and self-contained Loomle Clients for both supported
platforms.

[View the latest release](https://github.com/loomle/loomle/releases/latest){: .btn .btn-primary .fs-5 .mr-2 }
[Read the Quickstart](quickstart.html){: .btn .fs-5 }

{: .warning }
> The public [Fab listing](https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428)
> still distributes Loomle 0.6. Do not use that package with the 0.7
> documentation. The same 0.7 package will move to Fab after its Fab release is
> ready.

## Compatibility

| Unreal Engine | Operating system | Architecture | Package |
| --- | --- | --- | --- |
| 5.7 | macOS | Apple Silicon | `loomle-bridge-ue5.7.zip` |
| 5.7 | Windows | x64 | `loomle-bridge-ue5.7.zip` |
| 5.8 | macOS | Apple Silicon | `loomle-bridge-ue5.8.zip` |
| 5.8 | Windows | x64 | `loomle-bridge-ue5.8.zip` |

Other engine versions, operating systems, and architectures are not part of
this release.

## 1. Remove an Old Project Plugin

Unreal gives a project plugin precedence over an engine plugin with the same
name. An old Loomle 0.6 copy can therefore silently hide the newly installed
0.7 plugin.

Before installing or updating Loomle:

1. Close Unreal Editor for every affected project.
2. Check `<Project>/Plugins/LoomleBridge`.
3. Back up local modifications if that directory exists.
4. Remove it or move it outside the project's `Plugins` directory.
5. Repeat this check for every project that previously used Loomle 0.6.

## 2. Download the Complete Package

[Download for UE 5.7](https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.7.zip){: .btn .btn-primary }
[Download for UE 5.8](https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.8.zip){: .btn .btn-primary }

Matching [UE 5.7](https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.7.zip.sha256)
and [UE 5.8](https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.8.zip.sha256)
SHA-256 sidecars are available beside the ZIPs on the
[latest release page](https://github.com/loomle/loomle/releases/latest).

### macOS security

This release is not notarized. macOS may block the downloaded
Client. Review the release source and package, then use **Privacy & Security**
in System Settings if macOS asks you to approve it.

### Windows security

This release is not Authenticode-signed. Before extracting, open the
downloaded ZIP's **Properties** and use **Unblock** if Windows shows that
option. Windows may also display a trust or SmartScreen warning.

## 3. Install Into Unreal Engine

Extract the ZIP. It contains one `LoomleBridge` directory. Copy that complete
directory to:

```text
<UE_5.x>/Engine/Plugins/Marketplace/LoomleBridge
```

Common Epic Launcher engine roots are:

```text
macOS:   /Users/Shared/Epic Games/UE_5.7 or UE_5.8
Windows: C:\Program Files\Epic Games\UE_5.7 or UE_5.8
```

Custom engine installs may use another root. The final descriptor must be:

```text
<UE_5.x>/Engine/Plugins/Marketplace/LoomleBridge/LoomleBridge.uplugin
```

Avoid an accidental nested path such as
`LoomleBridge/LoomleBridge/LoomleBridge.uplugin`. When updating, close Unreal
Editor and replace the entire old `LoomleBridge` directory instead of merging
files from different versions.

Open the project, enable `LoomleBridge` if Unreal asks, and restart Unreal
Editor when prompted.

## 4. Configure the MCP Host

Configure one stdio MCP server named `loomle`. Its command is the absolute path
to the Client bundled inside the installed plugin:

| Platform | Client path below `LoomleBridge` |
| --- | --- |
| macOS | `Resources/Loomle/darwin-arm64/loomle` |
| Windows | `Resources/Loomle/win32-x64/loomle.exe` |

The Client is self-contained. Do not install Python, `uv`, Node.js, or a
separate global Loomle package, and do not copy the Client out of the plugin.
Loomle's workflow Skills are provided through the same MCP connection. Do not
install or configure a separate Skill copy for Codex, Claude, or another MCP
host.

### Codex

Add the absolute Client path to the Codex MCP configuration:

```toml
[mcp_servers.loomle]
command = "/absolute/path/to/LoomleBridge/Resources/Loomle/darwin-arm64/loomle"
args = ["mcp"]
```

On Windows, use the corresponding `win32-x64/loomle.exe` path. Forward slashes
are accepted in the path and avoid TOML backslash escaping.

### Claude Desktop-style hosts

```json
{
  "mcpServers": {
    "loomle": {
      "command": "/absolute/path/to/LoomleBridge/Resources/Loomle/darwin-arm64/loomle",
      "args": ["mcp"]
    }
  }
}
```

Use the Windows Client path on Windows. After changing MCP configuration,
restart Codex, Claude, or the relevant MCP host.

## 5. Verify the Connection

Keep the target Unreal project open, then:

1. Call `status({})`.
2. If the session is unbound, call `project({})`.
3. Use a returned `projectId` with
   `project({ projectId: "<id>" })`.
4. Call `editor({})`.
5. Call `sal_schema({})` and confirm that the interface index is available.
6. Call `agent_skill({})` and confirm that the resident Skill catalog is
   available.

`sal_schema` and `agent_skill` are local and remain available when Unreal
Editor is offline. The UE-backed tools require the bound project to have one
healthy Editor runtime.

Continue with the [Quickstart](quickstart.html).

## Troubleshooting

- **The MCP host does not show Loomle:** restart the host after changing its
  configuration and verify the absolute Client path.
- **The project is offline:** open that exact Unreal project with
  `LoomleBridge` enabled.
- **Loomle still behaves like 0.6:** search the project for an old
  `Plugins/LoomleBridge` directory.
- **The Client is blocked:** follow the platform security guidance above.
- **Several projects are available:** call `project({})` and bind the intended
  `projectId`; Loomle never guesses between projects.
