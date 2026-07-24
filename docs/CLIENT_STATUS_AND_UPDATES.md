# Client Status And Updates

## Intent

`status` is Loomle's read-only MCP control-plane snapshot. It answers whether
the Client is current and whether the bound Unreal project is usable without
mixing those responsibilities into `project` or SAL.

```text
status({})
```

The tool has no arguments, never explicitly selects or switches a project,
never edits UE, and never installs an update. Like other session-aware calls,
it may complete the normal automatic binding rules before reporting state.
`project` remains the only tool that lists candidates or explicitly binds and
switches projects.

## Result

The result is concise ordinary MCP text rather than SAL Object Text:

```text
client:
  version: 0.7.0-rc.1
  pid: 1234
  target: win32-x64
  executable: "C:/.../LoomleBridge/Resources/Loomle/win32-x64/loomle.exe"
update:
  status: available
  version: 0.7.0-rc.2
  release: "https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.2"
  asset: "https://github.com/loomle/loomle/releases/download/..."
  sha256: "..."
session:
  project: "<stable-project-id>"
  name: "Game"
  status: ready
bridge:
  version: 0.7.0-rc.1
  protocolVersion: 2
  plugin: "C:/.../Engine/Plugins/Marketplace/LoomleBridge"
```

`client` is always present. `update.status` is `current`, `available`, or
`unknown`. Update discovery failure is informational and never makes the tool
an MCP error. An unbound session reports `project: none` and `status: unbound`.
Bridge fields are included only when they are known from the selected project's
native runtime or persistent project record.

The Client PID and executable path identify the exact stdio MCP process. They
exist so an agent can stop the correct Windows process during an approved
update rather than terminating processes by a broad executable name.

## Update Discovery

The public machine-readable source is:

```text
https://loomle.ai/releases.json
```

It contains one current release per channel and exact target asset URLs and
SHA-256 values. A prerelease Client follows the `prerelease` channel; a stable
Client follows `stable`. The release process updates this file only after the
referenced GitHub Release exists.

The Client validates the manifest shape, compares semantic versions, applies a
short network timeout, and caches the result in-process. Unsupported targets,
offline use, malformed content, and timeouts return `unknown`; project binding,
SAL schema, and UE operations remain unaffected.

## Agent Guidance

The permanent tool description only asks the agent to call `status` once before
the first Loomle operation in a task. Detailed guidance appears only when an
update is available.

Windows:

```text
next: Ask the user before updating. After approval, ensure affected Unreal
Editors are closed, use a normal PowerShell to find Loomle Client processes
with the executable path above and stop each with Stop-Process -Id <pid>,
replace the complete plugin, then restart the MCP Server.
```

macOS:

```text
next: Ask the user before updating. After approval, ensure affected Unreal
Editors are closed, replace the complete plugin, then restart the MCP Server.
```

macOS can replace the running Client file; its existing process continues with
the already loaded image until the MCP Server restarts. Windows must release
every process using the same Client executable before replacement. In both
cases Unreal Editor must close before replacing the loaded Bridge module.

The guidance deliberately does not define an updater, update operation, shell
script, process-management abstraction, or SAL syntax. General-purpose agents
already own downloading, verification, permissions, transactional replacement,
and rollback.

## UE Mapping

The Client version comes from the generated product-version module. PID,
platform target, and executable path come from the running Client process.

`LoomleBridge` already publishes `pluginVersion`, `pluginPath`,
`pluginInstallScope`, `pluginManagedBy`, and `protocolVersion` in its native
project and runtime records. Client discovery preserves those fields and
`status` reports the facts for the bound project; no additional UE RPC or
parallel version source is introduced.

## Verification

Tests must cover:

- the six public MCP tools and empty `status` input;
- Client identity on supported targets;
- current, available, malformed, offline, and unsupported update states;
- prerelease and stable channel selection;
- Windows-only Client-stop guidance;
- bound ready, bound offline, and unbound session reports;
- preservation of Bridge version and plugin path from native records; and
- update discovery failure never blocking other public tools.
