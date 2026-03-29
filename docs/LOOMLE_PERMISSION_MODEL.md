# LOOMLE Permission Model

## Summary

LOOMLE has two distinct capability tiers:

- a static collaboration tier
- a runtime integration tier

These tiers do not have the same permission requirements.

This distinction should be explicit in the product.

## Capability Tiers

### 1. Static Collaboration Tier

This tier does not depend on live Unreal runtime connectivity.

Examples:

- role skills
- guides
- catalogs
- examples
- design and planning discussion
- review and documentation work

This tier should remain usable under more limited or default host permissions
whenever file access is still available.

### 2. Runtime Integration Tier

This tier depends on local execution and live runtime connectivity.

Examples:

- running the local LOOMLE client
- connecting to the local runtime transport or launching a platform-required bridge
- reaching Unreal bridge connectivity
- using `context`
- using `execute`
- using `graph.query`
- using `graph.mutate`
- using `graph.verify`

This tier may require explicit permission beyond a host's default file-only
access patterns.

## Permission Layers

LOOMLE runtime availability depends on several permission layers.

### Layer 1: Local client execution

The host must allow execution of the local LOOMLE client.

If this is blocked, the runtime tier cannot start at all.

In this case:

- static tier may still be available
- runtime tier is unavailable
- `doctor` cannot help if it depends on the blocked client itself

### Layer 2: Project path access

The client must be able to access the Unreal project root and required project
paths.

If this is blocked, the client may run but still fail to locate:

- the project
- the plugin
- the runtime endpoint metadata or platform-required bridge binary

### Layer 3: Local runtime transport

The client must be allowed to connect to or launch the project-local runtime.

If this is blocked, the client may run but runtime connectivity still fails.

### Layer 4: Unreal bridge availability

Even if the client and local runtime transport are available, Unreal runtime connectivity may
still be unavailable because:

- the plugin is not loaded
- the editor needs restart
- the runtime is not ready
- the bridge connection is degraded or blocked

This is a runtime-state issue rather than a host-execution issue, but users and
agents still experience it as "runtime unavailable."

## Product States

The product should conceptually recognize these states.

### State A: Static-Only

LOOMLE files are available, but runtime execution is not available.

Meaning:

- role skills and static references can still be used
- Unreal runtime features cannot be used

### State B: Client Blocked

The host will not allow execution of the LOOMLE client.

Meaning:

- static-only usage
- no local diagnosis through the client

### State C: Project Access Blocked

The client runs, but cannot access required project paths.

Meaning:

- runtime unavailable
- likely fix is path or access authorization

### State D: Runtime Launch Blocked

The client runs, but cannot connect to or launch the local runtime path.

Meaning:

- runtime unavailable
- likely fix is local execution authorization for the project runtime chain

### State E: Runtime Unavailable

The client and server may run, but Unreal bridge connectivity is not ready or
healthy.

Meaning:

- runtime unavailable for a different reason
- this is not necessarily a host permission problem

### State F: Full Runtime

The local client, local runtime path, and Unreal bridge are all working.

Meaning:

- both static and runtime tiers are available

## Product Rule

LOOMLE should not rely on the agent to infer permission escalation on its own.

Instead, the product should make the distinction clear:

- static mode is still useful
- runtime mode needs local execution and connectivity

When runtime is blocked, the user and agent should understand:

- what failed
- whether it is likely a permission issue
- whether they can continue in static mode
- whether runtime access needs explicit authorization

## Authorization Guidance

When runtime is blocked by execution restrictions, the right product behavior is
not a vague generic error.

The user or agent should be told something close to:

- LOOMLE static collaboration features are still available
- LOOMLE runtime features require permission to execute the local client and
  access the local runtime path

This keeps the product understandable and avoids making LOOMLE look completely
broken when only the runtime tier is unavailable.

## Doctor Limitation

`doctor` is useful only after the client itself is allowed to run.

That means:

- `doctor` can help with post-launch diagnosis
- `doctor` cannot solve the pre-launch permission problem if the host will not
  execute the LOOMLE client at all

This limitation should be understood in product messaging.

## Design Implication

Because runtime permissions are not guaranteed, LOOMLE should keep investing in
a strong static collaboration tier.

That means the product remains useful even in environments where runtime
execution is restricted.

This is a core reason to keep LOOMLE Lite:

- explicit
- project-local
- readable
- useful before runtime permission is granted
