---
layout: default
title: Status
parent: MCP Calls
nav_order: 1
description: Inspect Client version, update availability, session binding, and Bridge health.
---

# Status

Call `status` once before the first Loomle operation in a task:

```text
status({})
```

It reports the running Client version, PID, platform target, executable path,
update availability, current project binding, and the bound Bridge's health and
version when known. It is read-only, has no arguments, and does not install
updates. It may complete Loomle's normal automatic binding rules, but only
`project` explicitly binds or switches a project.

If the session is unbound, continue with [`project`](project.html). Update
discovery is informational: offline access or an unavailable release manifest
reports `unknown` without blocking SAL or project operations.

When an update is available, Loomle asks the agent to get user approval, close
affected Unreal Editors, replace the complete plugin, and restart the MCP
Server. On Windows only, it also identifies the running Client by PID and
executable path so the agent can stop every process using that installation
before replacing the locked executable. macOS does not require that extra
process-stop step. The returned guidance names the ordinary PowerShell
`Stop-Process -Id <pid>` command explicitly; it is run outside the Client
connection being stopped.
