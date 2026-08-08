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
  version: 0.7.0-rc.3
  pid: 1234
  target: win32-x64
  executable: "C:/.../LoomleBridge/Resources/Loomle/win32-x64/loomle.exe"
update:
  status: available
  version: 0.7.0
  release: "https://github.com/loomle/loomle/releases/tag/v0.7.0"
  asset: "https://github.com/loomle/loomle/releases/download/..."
  sha256: "..."
session:
  project: "<stable-project-id>"
  name: "Game"
  status: ready
bridge:
  version: 0.7.0-rc.3
  protocolVersion: 5
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

The Client reads GitHub's public latest-release endpoint directly:

```text
https://api.github.com/repos/loomle/loomle/releases/latest
```

Every Client compares against the latest published stable release, including a
Client whose current version is a prerelease. The Client derives the available
version from `tag_name`, selects the exact versioned
`loomle-bridge-<version>.zip` asset, and reads its download URL and GitHub
SHA-256 `digest` from that same Release response. It does not use the website
as a release manifest and does not follow a separate prerelease update channel.

The Client requires a non-draft, non-prerelease Release, an exact stable tag and
GitHub URL, and one uploaded versioned asset with a non-empty size and SHA-256
digest. It compares semantic versions, applies a short network timeout, and
caches the result in-process. Unsupported targets, offline use, malformed
content, GitHub rate limiting, and timeouts return `unknown`; project binding,
SAL schema, and UE operations remain unaffected.

`https://loomle.ai/releases.json` remains only as a frozen migration bridge for
Clients through 0.7.6. It was published one last time pointing to 0.7.7 and must
never advance again. Current Clients do not read that website file.

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

- the eight public MCP tools and empty `status` input;
- Client identity on supported targets;
- current, available, malformed, offline, and unsupported update states;
- prerelease Clients upgrading directly to the latest stable release;
- exact GitHub Release, versioned asset, URL, and digest validation;
- Windows-only Client-stop guidance;
- bound ready, bound offline, and unbound session reports;
- preservation of Bridge version and plugin path from native records; and
- update discovery failure never blocking other public tools.
