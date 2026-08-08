# Testing And Release Gates

## Intent

Loomle operates mutable Unreal Editor state. A successful compile is therefore
not evidence that a build is safe to release. Verification must prove both the
individual Client and Bridge contracts and a small number of complete packaged
workflows.

The test model has two complementary responsibilities:

- UE Automation covers the Bridge's UE-facing behavior broadly and deeply.
- Packaged end-to-end tests cover a small number of complete paths from the
  published Client through MCP, runtime discovery, RPC, SAL, and UE.

End-to-end tests do not duplicate the Bridge feature matrix. UE Automation does
not replace process, transport, installation, or packaged-binary verification.

## Implementation Status

The repository now provides:

- one macOS Apple Silicon runner with `automation` and `packaged_e2e` profiles,
  isolated project copies, bounded process ownership, crash/log inspection, and
  durable results;
- 135 in-module UE Automation tests covering the shared Bridge/RPC contracts
  and representative success, failure, lifecycle, and persistence paths across
  the active SAL interfaces;
- an authored Blueprint fixture plus a real packaged Client-to-UE smoke
  workflow; and
- Fab assembly and archive audits that exclude all native test code and test
  content from the release plugin.

The manually dispatched Fab workflow builds two candidates from the same
checkout. It runs complete UE Automation against the test-bearing development
plugin, then builds and audits the stripped release ZIP and runs packaged
end-to-end against that exact archive. Both runner result directories upload
even on failure; the release ZIP uploads only after every gate passes.

## Coverage Layers

### Fast Contract Tests

The existing root `npm test` command is the fast gate. It covers:

- SAL parsing, normalization, schema, diagnostics, and result conversion;
- static interface catalog consistency;
- Client MCP tools, project discovery, RPC, timeout, and cancellation behavior;
- product and protocol version generation;
- standalone Client and plugin assembly contracts; and
- UE runner and packaged-smoke orchestration contracts.

These tests do not launch Unreal Editor. The current Fab verification workflow
invokes them before native assembly.

The fast gate runs the runtime-lifecycle harness against the built JavaScript
Client entry. The packaged profile reruns the same harness against the exact
bundled executable.

The fast gate is a host-platform contract, not only a source-platform contract.
Tests that exercise another platform's project identity must pass that platform
explicitly and the implementation must use its matching path resolver. Tests
that exercise live MCP Roots use valid local `file:` URLs for the current host
and compare the paths produced by that host. Asynchronous Roots tests must
either release every pending operation or fail within their own bounded wait;
an impossible platform path must not leave the suite running indefinitely.

Native Client assembly also follows host process semantics. Subprocess commands
that are command wrappers use the host executable name (`npm.cmd` on Windows
and `npm` elsewhere), independent of the requested output target. Windows
command wrappers are launched through the Windows command shell because Node
cannot execute a `.cmd` file directly; native executables remain direct child
processes. Failure to resolve a required build command is an immediate build
failure and must never be mistaken for an executable smoke-test failure.
Self-hosted verification keeps the exact official Node archive in the runner
tool cache through `LOOMLE_CLIENT_RUNTIME_CACHE`. Clean checkouts reuse it only
after verifying the pinned SHA-256; cache corruption or a manifest version
change forces a fresh download. The Mac runner does not restore a remote npm
cache because `npm ci` is cheaper and more reliable than transferring that
cache on the dedicated host.

This repair changes no public MCP tool, input or output schema, edit operation,
or diagnostic code. Acceptance requires the complete root `npm test` gate and
the pinned-runtime `build:executable` plus `test:executable` pair to pass on the
Windows x64 verification host without orphaned test processes.

### UE Automation

UE Automation is the primary Bridge functional suite. Every public Bridge
surface must map to native tests that cover:

- one representative successful query or operation;
- invalid identity, invalid value, and unavailable-capability failure paths;
- dry-run and real apply for each mutation family;
- readback, transaction, undo, and atomic failure where applicable;
- relevant UE lifecycle states, including no ready publication before
  `OnEditorInitialized` and Game Thread progress refreshed by completed
  admitted requests;
- every previously observed crash or corruption regression.

Graph mutation regressions must include native callbacks that reconstruct or
remove Pins. At minimum they cover a single stable-reference Edge disconnect,
ordered disconnect/reconnect planning, and same-Patch Node-alias connections;
dry-run must preserve the source Graph and return a plan or diagnostic without
terminating the Editor.

Complete Bridge coverage means complete operation and state coverage. It does
not mean enumerating every UE Node class, reflected property type, palette
entry, or project asset. Generic reflected behavior uses representative native
types plus targeted regressions.

The maintained operation/state matrix is
[`UE_AUTOMATION_COVERAGE.md`](UE_AUTOMATION_COVERAGE.md). A green Automation
run is a release signal only when that matrix covers the active public surface;
test count alone is not a coverage metric.

Tests run in a dedicated `UnrealEditor-Cmd` process. The runner must fail when:

- an Automation test fails or times out;
- the test category is missing or executes zero tests;
- the Editor exits unexpectedly;
- the Editor log contains an assertion, fatal error, or critical error; or
- the run creates a new Unreal crash report.

An in-process assertion cannot be converted into an ordinary test failure, so
the outer process boundary is part of the native test contract.

Automation runs against a same-commit development plugin compiled with
`Source/LoomleBridge/Private/Tests`. It does not run against the final release
archive, because release assembly intentionally removes that subtree before
UHT and compilation.

### Packaged End-To-End

Packaged end-to-end uses the exact final candidate archive and bundled Client
that may later be published. It copies the authored host project to a temporary
directory, installs the archive, starts one Editor process, and exercises this
compact vertical workflow:

1. initialize MCP and verify the eight public tools;
2. bind the copied project while it is offline;
3. read local `sal_schema`, verify the six active interface modules, and load
   the packaged `format-unreal-blueprints` Skill through `agent_skill`;
4. start the Editor and observe the same project identity become ready;
5. prove Game Thread admission with the exact read-only fixture Asset query,
   then inspect the fixture through Blueprint queries;
6. dry-run a Blueprint description edit and prove the fixture is unchanged;
7. apply the edit, read it back, and restore the authored value;
8. read context through `editor({})`, reconnect the Client, then stop the Editor
   and observe the same project become offline.

This suite is intentionally small. Domain combinations and native edge cases
belong in UE Automation.

After the real UE vertical smoke, the candidate Client is exercised as a real
child process over a real local Unix socket or named pipe against a
deterministic private-Bridge test double. This phase verifies runtime
replacement without losing project binding, stale runtime-record rejection,
bounded failure of an in-flight request after disconnect, and recovery through
a replacement runtime without replay.

### Process Lifecycle

The same end-to-end runner owns lifecycle scenarios:

- clean Editor exit removes the runtime endpoint and reports the project
  offline within a bounded time;
- Editor restart preserves project binding while changing runtime identity;
- forced exit leaves no record that can be mistaken for a ready runtime;
- a request active during shutdown fails within its deadline;
- the runner terminates only the process group that it created.

Readiness polling is allowed. A public `ready` report proves listener health and
recent Game Thread progress, but UE may still be finishing startup work when
the first invocation arrives. Before any mutation, the harness therefore
retries the exact read-only fixture Asset query within the existing readiness
deadline only when the Bridge returns `runtime.editor_unresponsive`, meaning
the request was cancelled before Game Thread admission. Any other diagnostic
fails immediately. Once one read-only query is admitted successfully, failed
assertions, scenarios, and all Patch calls are never retried automatically.

The real Editor workflow currently covers clean Editor stop, offline
observation, and Client reconnection. A separate candidate-Client transport
phase covers runtime identity replacement, stale-record rejection, and
in-flight disconnect against a deterministic Bridge test double. It does not
constitute a real Editor restart or shutdown test. Real Editor restart, forced
Editor termination, and a request interrupted by real UE shutdown remain
pending. The packaged Win64 workflow now executes the real named-pipe path from
the bundled Client through the Editor Bridge.

## Blueprint Preflight Regression

Blueprint Patch executes real mutation logic against an isolated transient plan
before changing the source Blueprint. This is required by the shared mutation
dry-run and atomicity contract.

The plan must preserve every UE invariant reached by that logic. In particular:

- a transient generated Class must own an isolated transient CDO before any
  call that can reach `FBlueprintEditorUtils::MarkBlueprintAsModified`;
- the invariant must be checked again before executing the plan;
- a real target or loaded descendant with an unsafe generated Class must fail
  closed before UE can assert;
- dry-run must never weaken or skip the real validation path.

The July 23, 2026 Oasium crash is the canonical regression. The old plan built a
`UBlueprintGeneratedClass` without a CDO, then a normal Class Settings change
reached `UBlueprintGeneratedClass::UpdateCustomPropertyListForPostConstruction`
and asserted on `GetDefaultObject(false)`.

Blueprint-, Graph-, and Widget-owned Patch planners share one native Blueprint
sandbox. It must let UE's normal Blueprint duplication path create and compile
the copied Generated Class, Skeleton Class, CDOs, SCS, templates, and graphs.
Directly duplicating or hand-constructing `UBlueprintGeneratedClass` is not a
supported substitute.

The sandbox preserves every stable identifier used by SAL, repairs the copied
Class property-Guid maps after UE's ordinary duplication remap, and then audits
the source and copy exactly. UE may reconstruct a dynamic Pin while duplicating
a Blueprint and assign the same logical Pin a new `PinId`. Before the exact
audit, the sandbox must match such Pins one-to-one by native persistent identity
or an unambiguous structural locator and restore only the copied `PinId`.
Missing or ambiguous matches fail closed; `PersistentGuid` remains UE-owned and
is never rewritten. PinId uniqueness is checked within one owning Node,
matching UE's Pin lookup scope; reuse on another Node or Graph must not block
unrelated Blueprint, Graph, or Widget preflight. The copied Blueprint is held
by a strong object reference through the entire preflight and sheds its copied
standalone flag only after native duplication has completed. During
`StaticDuplicateObject` itself, UE keeps a non-auto-removing source-to-copy
annotation map alive through every copied object's `PostDuplicate`. Blueprint
`PostDuplicate` may synchronously compile and collect garbage before that map
is destroyed. The sandbox must therefore strongly retain the source Blueprint
and every currently nested source object for exactly the native duplication
call, then release them only after `StaticDuplicateObject` returns. This keeps
annotation keys alive without replacing, delaying, or weakening UE's native
duplication and compilation lifecycle. Together the source-graph guard and the
copied-Blueprint guard ensure that neither compile-time GC nor a long Editor
session can invalidate or leak the sandbox.
Compiler-error status alone is allowed: agents must remain able to repair a
broken Blueprint.

Graph and Widget may add only their domain-owned isolation after that common
step. Graph detaches internal Timeline curves while preserving shared-curve
aliasing. Widget verifies its Widget Tree, Slot, Navigation, Animation,
MovieScene, Extension, binding, and Widget-Guid state. Neither domain replaces
the valid Classes produced by UE.

Native tests must cover ordinary Blueprint and Widget Blueprint Class Settings,
dry-run source isolation, real apply and undo, a cold generated Class, and a
loaded descendant boundary. A Widget Blueprint regression must make one
annotated nested source object unreachable immediately before the duplicate's
native `PostDuplicate` compilation, proving that nested compile-time GC cannot
delete an annotation key. Graph, Pin, Variable/Dispatcher, SCS, Timeline, and
Widget identities must survive the sandbox unchanged. A reconstructed
`UK2Node_PromotableOperator` tolerance Pin is the canonical dynamic-Pin
regression: dry-run must restore its source `@NodeGuid/PinId`, while ambiguous structural
matches must still fail.

## Fixture Contract

The repository owns one minimal host project:

```text
tests/fixtures/ue/LoomleTestHost/
```

The template is read-only and contains
`/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E` plus
`LoomleE2E.fixture.json`, which records its exact native locator and baseline
description. Every run copies the template to a unique temporary directory.
Tests never use a user project or assets left by an earlier run. A temporary
runner workspace is removed after a normal run and retained only when
termination or cleanup fails, so the failure can be diagnosed.

Fatal Blueprint preflight coverage uses a normal healthy fixture: the faulty
state is the transient plan created by Loomle, not a deliberately corrupted
binary asset.

UE Automation may create transient fixtures directly. Packaged tests use only
public tools and must not require a private test RPC in the product.

## Runner Contract

One Node runner owns Editor processes and test artifacts. It accepts
`darwin-arm64` candidates on Darwin hosts and `win32-x64` candidates on Windows
x64 hosts. It accepts explicit engine, project-template, plugin/archive,
profile, target, and output paths. It must:

- spawn the Editor directly and retain its PID plus a process group where the
  host supports one;
- use a durable per-run Client state directory, exposed through a short
  temporary `HOME` alias on platforms whose Unix socket paths are bounded;
- enforce a deadline for every phase;
- on the first `SIGINT` or `SIGTERM`, request a graceful abort and keep
  handling repeated signals until cleanup and `result.json` are complete;
- terminate, then force-kill only its owned Editor or process group when
  necessary, and confirm that owned process has disappeared before removing
  its workspace;
- snapshot crash locations before and after the run;
- bind every report to the commit and candidate identity;
- record SHA-256 when the candidate is an archive; and
- preserve logs and results on both success and failure.

`--plugin-dir` accepts a compiled plugin root with the target Editor module
binaries; raw source is rejected. `--plugin-archive` accepts the equivalent
compiled archive and is required for the final release gate. `--output-dir`
must be a new, non-overlapping path: the runner never deletes or reuses an
existing directory.

Both profiles write:

```text
result.json
editor.log
client.stderr.log
runtime-state/
crashes/
```

Automation additionally writes `automation-report/`. Packaged end-to-end stores
its ordered smoke-step results in `result.json`.

`result.json` records product and protocol versions, commit, target, archive
hash when applicable, scenario status (`passed`, `failed`, `timed_out`, or
`crashed`), duration, and the first failing phase.

Automation uses a same-commit compiled development plugin that still contains
the native tests:

```sh
npm run test:ue-automation -- \
  --ue-root <UE-root> \
  --project-template tests/fixtures/ue/LoomleTestHost \
  --plugin-dir <compiled-same-commit-test-plugin> \
  --output-dir <new-artifact-directory> \
  --target darwin-arm64
```

The final packaged gate uses the audited release ZIP:

```sh
npm run test:packaged-e2e -- \
  --ue-root <UE-root> \
  --project-template tests/fixtures/ue/LoomleTestHost \
  --plugin-archive <final-release-candidate.zip> \
  --output-dir <new-artifact-directory> \
  --target darwin-arm64
```

`--plugin-dir` is also available for local packaged-candidate diagnosis, but it
does not replace the final archive gate.

## Test Code Boundary

Test fixtures and reflected test schemas are repository development inputs, not
release content. Native test builds may compile them, but the release artifact
must not contain test source, test assets, generated UHT test files, or a
test-only runtime module.

The current in-module Automation sources live under
`Source/LoomleBridge/Private/Tests`. Fab assembly excludes that exact subtree
before UE BuildPlugin invokes UHT or compilation. Assembly also requires the
plugin descriptor to contain exactly one module named `LoomleBridge`, so a
test-only module cannot cross the release boundary.

The release workflow repeats the boundary audit on both UE BuildPlugin output
and the extracted final ZIP. Neither may contain the test subtree,
`Intermediate/`, `Saved/`, or files below `Content/`, and both descriptors must
still name only the `LoomleBridge` module. The plugin keeps an empty `Content/`
directory as part of its distributable structure while
`CanContainContent=false`; BuildPlugin is allowed to drop the empty directory,
so release staging restores it before the final audit and archive.

The two candidates come from the same commit but serve different purposes:

- the Automation candidate retains and compiles the native test subtree;
- the release candidate excludes it before UHT, compilation, and archiving,
  then proves the published Client-to-UE path through packaged end-to-end.

## Release Gates

A candidate can be promoted only when the same commit passes:

1. fast contract tests;
2. the complete UE Automation category on every supported UE/platform build;
3. packaged end-to-end smoke against the exact candidate archive;
4. lifecycle verification with no new crash report;
5. archive structure, version, license, and hash audits;
6. an explicit platform signing and trust declaration in the release notes.

The release workflow consumes both already-tested native archives and their
matching result files. It must never rebuild, resign, or rewrite verified
native executable bytes after these gates. Promotion may mechanically merge
the verified platform fragments and create cross-platform ZIP containers under
the checked-in merge contract.

The Mac Apple Silicon and Windows x64 workflows implement the same native
sequence. GitHub prereleases and final releases may publish the unsigned
candidates used by QA when macOS Gatekeeper and Windows trust-warning
limitations are stated in their release notes. Platform signing remains
valuable distribution hardening for the agent-invoked Client, but it is not a
promotion prerequisite. The tested bytes must still be the published bytes.

The Windows x64 workflow follows the same candidate construction:
it builds a pinned native Node SEA Client, runs the complete UE Automation
category against a same-commit test-bearing plugin, builds and audits a stripped
Win64 plugin, and runs packaged end-to-end against the exact ZIP it uploads.
At job start it may terminate only an unattended Editor whose command line
names a `loomle-ue-automation-*` or `loomle-ue-packaged-e2e-*` temporary
workspace left by an interrupted runner job. Any other existing Editor remains
a hard failure.
PE audits require both `loomle.exe` and `UnrealEditor-LoomleBridge.dll` to use
the AMD64 machine type. An unsigned executable may be published only when the
checked-in release notes explain the resulting Windows trust warning.

The 2026-07-24 local Windows repair audit used pinned Node 24.18.0 and the UE
5.7.4 Installed Build. The repository test command passed; both the
test-bearing and stripped Win64 BuildPlugin candidates compiled; all 127 UE
Automation tests passed; and the nine-step packaged end-to-end workflow passed
against the final ZIP. The archive contained neither the native test subtree
nor `Intermediate/`, and the run produced no runner-classified log hazard, new
crash report, or orphaned Editor process. This validates the local source state
and Windows QA gates; promotion still requires a committed same-SHA workflow
run and the channel's signing policy.

The 2026-07-25 local macOS arm64 SAL v3 migration audit used the pinned Node
24.18.0 Client and a UE 5.7 Installed Build. The unrestricted repository test
command passed; same-source test-bearing and stripped BuildPlugin candidates
compiled; all 135 UE Automation tests passed; and the nine-step packaged
end-to-end workflow passed against the exact pre-version-bump local QA ZIP.
That ZIP's SHA-256 is
`744fb3b910dac25b285cc89efb427489432c13c89e947a7c739f182d0b801d8e`.
The archive excluded native tests and transient build state, both binaries
were arm64, and the run produced no runner-classified log hazard, new crash
report, or orphaned Editor process. Its hash is historical migration evidence,
not a future release-asset identity. No artifact was published; promotion
still requires a committed same-SHA workflow run and the channel's signing
policy.

Manual promotion takes successful `verify-fab-mac.yml` and
`verify-fab-windows.yml` run IDs. It requires both runs to belong to the same
commit, then checks out that exact commit and verifies both run identities,
native targets, result files, candidate hashes, product versions, and release
notes. It rejects shared-file drift, allowing only an explicitly verified
CRLF/LF-equivalent historical text representation and emitting LF. It then
mechanically merges the two verified source fragments and the two verified
compiled fragments, and publishes one
cross-platform source ZIP plus one cross-platform complete plugin ZIP with
their SHA-256 sidecars. Client and Bridge executable bytes are copied unchanged
from the exact native E2E-tested fragments. The tag is derived from the
checked-out product version rather than accepted as free-form input.

On Mac, architecture is verified from the built Client and Bridge binaries,
not expressed as a module `PlatformArchitectureAllowList`. A universal UE
Editor reports its compiled architecture as `MULTI`; restricting the module to
`Mac:arm64` would silently prevent it from loading in that Editor even when the
running process slice is arm64.

## Deliberate Initial Limits

The first framework does not introduce Gauntlet, BuildGraph, screenshot
regression, coverage services, a persistent shared test project, a Python test
runtime, or the retired 0.6 domain fixtures. Those additions require a concrete
gap that the current layers cannot express.
