# Python Fallback Execution

## Status

The implementation is in progress on the Loomle 0.7 development branch. Its
public Client and Bridge source now include the eighth public tool, `python`,
but it is not a released contract until the packaged acceptance gates below
pass.

This document defines the eighth public tool, `python`. The tool has one
primary `run` operation and one continuation-only `poll` operation. It is not
part of a published Loomle release yet.

The design is confirmed. Client, Bridge, protocol, diagnostics, focused tests,
and release-documentation changes are present. Local arm64 `BuildPlugin`, the
complete 149-test UE Automation category, and exact-archive packaged acceptance
pass against both official UE 5.7 and UE 5.8 Launcher installations. The native
Windows release gates are still required before release.

The 2026-08-09 safe-entry audit moved Python admission from a Game Thread
TaskGraph task to the persistent Core Ticker described below. Focused tests now
prove that a TaskGraph entry is deferred and that synchronous
`AssetTools.ImportAssetTasks` imports and cleans up a PNG texture without
recursively processing the named-thread queue. These tests pass on the official
UE 5.7 and UE 5.8 Launcher installations for Mac arm64. The original reported
environment was Windows UE 5.7.4, so that platform remains an explicit release
gate rather than an inferred result from the Mac validation.

The 2026-08-09 PIE fallback audit removed the earlier blanket play-session
rejection. Focused admission automation passes on the official UE 5.7 and UE
5.8 Launcher installations for Mac arm64. A live public-MCP acceptance on UE
5.7 requested PIE from one short Python call, observed one valid Game World and
advancing gameplay time in later calls, requested stop, and confirmed that the
Game World was gone. The resident `debug-unreal-pie-with-python` Skill now owns
the permission, short-call, world-reacquisition, and cleanup workflow; Loomle
does not own a parallel PIE lifecycle state machine.

The 2026-08-09 Agent-guidance audit added the resident
`use-unreal-python` safety workflow and narrowed
`debug-unreal-pie-with-python` to an additive PIE specialization. A focused
partial-failure recovery test now proves that a failed Python execution can
leave an asset applied, reports `stateMayHaveChanged: true`, and can be
followed by an idempotent recovery call that reuses rather than duplicates the
asset. The test compiles and passes on the official UE 5.7 and UE 5.8 Launcher
installations for Mac arm64. Client, Skill-validation, site-build, and Fab
packaging tests also pass for the unified guidance chain.

The later Scene/PCG design reuses this tool's proven outer async lifecycle as
the basis of a shared internal execution kernel. That is a future internal
refactor gate for the typed PCG frontend.

The 2026-08-10 Scene/PCG boundary decision adds one planned, coordinated Python
result extension: runner-injected `sal.object(UObject)` can explicitly submit a
returned UObject for read-only projection through already published SAL Domain
projectors. This extension is not implemented or released yet. It preserves
the existing `run`/`poll` lifecycle, ids, Game-Thread safe-entry rules, logs,
traceback, concurrency, and all ordinary JSON-result behavior, but it requires
a versioned optional Bridge-owned `sal` field in terminal success results.
Skill guidance will be updated only after the implementation and live workflow
are validated.

## Decision

`python` is Loomle's high-privilege escape hatch for Unreal Editor behavior
that is already well served by UE Python but is not yet expressed through a
structured Loomle interface.

It runs full embedded Unreal Python, including `import unreal`. It is not a
second SAL orchestration language, and it does not reproduce UE 5.8's
restricted Programmatic Toolset. An agent uses SAL when Loomle already exposes
the required semantics and uses Python when the structured interface boundary
does not cover the necessary native workflow.

The public contract is deliberately small:

```text
python(operation: "run" | "poll")
```

- `run` supplies inline source that defines `run()` and normally returns its
  structured result directly.
- `poll` exists only when an earlier `run` exceeded Loomle's short inline
  completion window and returned an `executionId` plus an exact continuation.

Future typed async frontends may reuse an internal kernel for execution-record
transitions, exact continuation, poll, retention/expiry, runtime loss,
`stateMayHaveChanged`, and no-replay behavior. They do not call `python.run`,
accept Python execution ids, or inherit Python script/result payloads.

There is no caller-provided purpose or reason string. Natural-language intent
is not machine-verifiable, does not constrain what the script can do, and
would duplicate information already available to the agent and reviewing
user. The source and its actual effects remain authoritative.

`sal.object()` is not Python-based SAL orchestration. It requests a bounded
terminal read projection of one explicitly selected UObject. It never runs
`sal_query` or `sal_patch`, never chooses a Domain from user-supplied text, and
never grants the returned script a structured mutation capability.

## Agent Guidance Layering

The tool contract and resident Agent Skills form one progressive guidance
chain rather than three parallel sources of workflow policy:

```text
python tool description
-> use-unreal-python
-> debug-unreal-pie-with-python when the task enters PIE
```

The public `python` description owns only permanent capability facts and
routing: Python is a fallback, `run` is unrestricted and mutating, an exposed
continuation must be polled exactly, and the relevant resident Skills must be
loaded. It does not carry API-discovery, idempotency, recovery, persistence, or
PIE procedure.

`use-unreal-python` owns the general fallback workflow:

- live status and project-context checks;
- structured-interface selection before Python;
- live Unreal Python API and property discovery;
- small idempotent calls with stable target rediscovery;
- structured evidence, explicit persistence, and independent verification;
- exact continuation handling and conservative partial-state recovery.

`debug-unreal-pie-with-python` is an additive specialization. It owns only PIE
permission, session ownership, asynchronous start/stop requests, Game World
selection, cross-frame call boundaries, multi-world ambiguity, and cleanup. It
must direct the agent to load and follow `use-unreal-python` first instead of
duplicating its general safety policy.

This boundary lets the base policy evolve once while keeping PIE's distinct UE
lifecycle semantics discoverable only for runtime debugging tasks.

## Intent and Boundary

The fallback lets an agent complete a concrete UE Editor task without waiting
for Loomle to model every mature Python-supported domain.

Appropriate uses include:

- creating or bulk-populating a `UDataTable` through its existing RowStruct;
- creating and editing an instance of an existing `UDataAsset` subclass;
- using a project or third-party plugin's documented Unreal Python API;
- invoking an Editor subsystem or native operation that has no SAL surface;
- performing project-specific batch repair or migration through UE's object
  model.

It should not be used:

- to reimplement ordinary SAL query or patch composition;
- as an automatic retry after a structured operation fails;
- to bypass a validation error returned by the owning Loomle interface;
- to claim dry-run, rollback, atomicity, idempotency, or safe cancellation;
- as agent-local Python. It executes inside the bound Unreal Editor process.

Repeated use in one stable workflow is product evidence for a structured
Loomle interface only when that interface would materially add native
identities, validation, dry-run, diff, revision, or Editor-specific diagnostic
semantics. Python remains a permanent complement for mature open-ended UE
domains rather than a temporary implementation defect that SAL must absorb in
full.

## UE Source Basis

The implementation target is UE 5.7. Relevant source is:

- `Engine/Plugins/Experimental/PythonScriptPlugin/PythonScriptPlugin.uplugin`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/IPythonScriptPlugin.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Public/PythonScriptTypes.h`
- `Engine/Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PythonScriptPlugin.cpp`

`IPythonScriptPlugin` is the supported native boundary. It reports whether
Python is configured, available, and initialized; it can request runtime
enablement; and it executes an `FPythonCommandEx`.

`FPythonCommandEx` supplies several useful native behaviors:

- `ExecuteFile` runs multiline source or a `.py` file;
- `Private` file scope provides a fresh locals/globals dictionary based on
  UE's default Python globals;
- `Unattended` suppresses some interactive UI but cannot guarantee that an
  invoked API will never display or wait for UI;
- `LogOutput` captures ordered Python `Info`, `Warning`, and `Error` entries;
- failure places a Python exception trace in `CommandResult`.

`Private` scope is namespace isolation, not a security sandbox. Imported
modules, UObject state, packages, files, processes, network activity, module
caches, and other process-global state remain reachable. UE evaluates the
script inside the Editor process while holding the embedded interpreter's GIL.

UE 5.8's Programmatic Toolset is useful precedent for a different goal. It
runs agent-authored Python in the same embedded interpreter but restricts
imports and builtins, prevents direct `unreal` access, and injects an
`execute_tool` function that marshals registered tools to the Editor thread.
That design safely optimizes structured tool orchestration. Loomle does not
adopt its capability restriction because this fallback exists specifically to
reach UE APIs that have no registered structured tool.

Loomle does adopt three useful principles from that design:

- one named `run()` entry point;
- an explicitly JSON-compatible returned dictionary;
- asynchronous request completion must not imply that UE work is safely
  cancellable.

## Public MCP Tool

### Name and annotations

The public tool is named `python`. Its annotations are conservative because
MCP annotations apply to the whole tool rather than to an individual
operation:

```json
{
  "readOnlyHint": false,
  "destructiveHint": true,
  "idempotentHint": false,
  "openWorldHint": true
}
```

`poll` is read-only, but the combined tool must retain the destructive and
open-world classification required by `run`.

The permanent description should be concise and route the agent to resident
workflow guidance:

> Run full Python in the bound Unreal Editor when no structured Loomle
> interface covers the capability. Before run, load `use-unreal-python`; for
> PIE also load `debug-unreal-pie-with-python`. Follow a returned `poll`
> exactly; never replay. No built-in dry run, rollback, safe cancellation, or
> idempotency.

### Input schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "oneOf": [
    {
      "properties": {
        "operation": { "const": "run" },
        "script": {
          "type": "string",
          "minLength": 1,
          "maxLength": 262144,
          "description": "Inline Unreal Editor Python defining one synchronous run() entry point."
        }
      },
      "required": ["operation", "script"],
      "additionalProperties": false
    },
    {
      "properties": {
        "operation": { "const": "poll" },
        "executionId": {
          "type": "string",
          "minLength": 1,
          "description": "Opaque handle returned by an earlier running result."
        }
      },
      "required": ["operation", "executionId"],
      "additionalProperties": false
    }
  ]
}
```

`operation` is required. Explicit `run` prevents a malformed status-like call
from being interpreted as executable source, while explicit `poll` makes the
continuation self-describing.

The first version has no `purpose`, `reason`, `mode`, `file`, `args`, `cwd`,
`environment`, `dryRun`, `timeout`, `transaction`, `expectedRevision`, result
schema, cancellation, or caller-selected execution id.

The caller never supplies a source-file path. Loomle owns all temporary paths.

## Script Contract

A script defines one synchronous, no-argument `run()`:

```python
import unreal

def run():
    asset = unreal.load_asset("/Game/Data/DA_Weapon")
    asset.set_editor_property("damage", 25.0)
    return {
        "assetPath": asset.get_path_name(),
        "changedProperties": ["damage"],
        "saved": False,
    }
```

The complete source is executed in a fresh per-call namespace and then Loomle
calls `run()` from that namespace. Top-level imports and statements are part of
the execution and may have side effects. The fresh namespace does not isolate
imported modules or other process-global state.

The entry point must be callable, synchronous, and accept no arguments.
`async def`, generators, and a callable requiring arguments are invalid.

`run()` must return a top-level `dict`. Its complete value must recursively
contain only:

- `None`;
- Boolean values;
- integers in JavaScript's exactly representable safe range
  `[-(2^53-1), 2^53-1]` and finite floating-point numbers;
- strings;
- lists of compatible values;
- dictionaries with string keys and compatible values.

The planned projection extension adds one explicit value-only exception: an
opaque marker returned by the runner-injected `sal.object(UObject)` helper.
The marker is replaced by a JSON projection placeholder before the public
`result` is serialized. It cannot be used as a dictionary key.

Cycles, integers outside the safe range, tuples, sets, bytes, NaN, infinity,
unwrapped UObject wrappers, reflected structs, and other Python values are
rejected.
The safe-integer rule prevents Python's arbitrary-precision integers from
silently changing value when they cross MCP JSON and the TypeScript Client.
Loomle does not guess how to serialize a UE object or scan a return tree for
objects to expose. The script normally projects it into stable, useful facts
such as a Class, GUID, name, or ordinary properties. When the caller also wants
a registered SAL projector to examine that exact UObject, it must opt in with
`sal.object()`.

For example, this is invalid:

```python
return {"asset": asset}
```

This is valid:

```python
return {
    "assetPath": asset.get_path_name(),
    "classPath": asset.get_class().get_path_name(),
}
```

This is also valid once the projection extension ships:

```python
def run():
    actor = ...
    components = ...
    return {
        "mode": "sie",
        "actor": sal.object(actor),
        "components": [sal.object(component) for component in components],
    }
```

The agent defines the domain-specific result shape in its own code. Loomle
validates only the stable outer contract and JSON compatibility; it does not
require a duplicate caller-provided result schema.

## SAL Object Projection

### Injected helper

Every Python execution namespace receives a runner-owned `sal` value. The v1
surface has exactly one member:

```text
sal.object(value: unreal.Object) -> opaque marker
```

No import is needed or supported. The member is read-only, although ordinary
Python can shadow the local name and thereby lose access to it. There are no
`domain`, `target`, `path`, `ref`, `view`, `patch`, `handoff`, or operation
arguments. A script cannot assert that an object is supported or handwrite its
canonical Target.

Markers may appear in dictionary values, list elements, and nested values. A
marker that is created but does not appear in the returned tree is discarded.
Reusing one marker records all result paths. Repeated markers for the same
native UObject are deduplicated to one candidate during terminalization.
Loomle never walks properties, Actor Components, references, or the UObject
graph automatically.

Calling `sal.object()` with a non-UObject, placing a marker in a dictionary key,
or exceeding the candidate/occurrence bounds makes the Python result invalid.
A valid UObject that later becomes unavailable is a projection outcome rather
than an invalid Python return.

### Public result relationship

The script-owned `result` remains JSON. Each reachable marker becomes a compact
location placeholder such as:

```json
{
  "mode": "sie",
  "actor": { "$salObject": "o0" }
}
```

The trusted projection data is not inserted into the script-owned object. It is
returned in an optional top-level `sal` member generated only by the Bridge:

```json
{
  "complete": true,
  "projections": [
    {
      "id": "o0",
      "resultPaths": [["actor"]],
      "status": "projected",
      "views": [
        {
          "relation": "authored_source",
          "schemaModule": "level",
          "subject": {
            "targetContext": "exact_target",
            "target": {
              "alias": "levelTarget",
              "target": {
                "kind": "target",
                "domain": "level",
                "asset": "/Game/Maps/Forest.Forest",
                "type": "/Script/Engine.World"
              }
            },
            "object": {
              "statements": [
                {
                  "target": { "kind": "local", "name": "actor" },
                  "value": {
                    "kind": "stable_ref",
                    "identityPath": ["11111111-1111-1111-1111-111111111111"],
                    "semanticTag": "actor"
                  }
                }
              ]
            },
            "diagnostics": []
          },
          "query": {
            "tool": "sal_query",
            "arguments": {
              "text": "levelTarget = target {\n  domain: level,\n  asset: \"/Game/Maps/Forest.Forest\",\n  type: \"/Script/Engine.World\"\n}\nquery levelTarget\n@11111111-1111-1111-1111-111111111111\nwith schema"
            }
          }
        }
      ],
      "diagnostics": []
    }
  ]
}
```

The `subject` above is a normalized SAL exact Query result with Object Text;
the release schema reuses that existing closed definition rather than
accepting arbitrary objects, mutation results, Domain-root results, or
unresolved results. `query` is a required Bridge-generated read-only next
request that must itself be valid canonical SAL. It is a
convenience, not a continuation, handoff, lease, or authority token. A
projection never generates a Patch. `schemaModule` identifies the static card
the agent can inspect before choosing any mutation.

The script may return an ordinary field named `sal` inside `result`; it remains
untrusted user data and cannot spoof the Bridge-owned top-level `sal` member.
Likewise, a script-authored dictionary resembling `{ "$salObject": "o0" }`
has no projection meaning unless the runner registered the corresponding
marker and the Bridge emitted its top-level record.

### Projection outcomes and views

Projection status and successful source relationship are separate dimensions:

```text
status   = projected | unsupported | ambiguous | stale | failed
relation = exact | authored_source
```

- `exact` means that the observed UObject itself has the canonical published
  SAL view.
- `authored_source` means that a PIE/SIE or other live duplicate maps uniquely
  to a persistent authored source. Every later SAL request addresses that
  source, not the live duplicate.
- `unsupported` means that the object is valid but no currently published SAL
  projector owns a faithful view. A planned-but-unpublished Domain does not
  count as support.
- `ambiguous` means that native evidence cannot prove one source identity.
- `stale` means that the object became invalid between marker creation and
  terminal projection.
- `failed` means that a registered projector or Bridge integrity check failed.

One candidate may produce several `views`, for example generic Asset and a
specialized authored Domain. That is not ambiguity. The Bridge invokes every
applicable published projector and does not infer one preferred Domain merely
from native Class.

Unsupported, ambiguous, and stale candidates do not fail the Python execution;
they are complete classified results. A projector integrity failure preserves
the already completed script result, marks that candidate `failed`, emits a
diagnostic, and sets `sal.complete: false`. A Bridge-generated `subject` that
does not pass the shared exact-Query-result validator is never returned as a
usable Target.

### Trust, lifetime, and authority

The runner registers the actual execution-local UObject, not a caller-written
object path. After `run()` returns and while still on the Game Thread, the
Bridge performs bounded read-only projection before publishing the terminal
record. Projectors must not load or switch maps, load assets, dirty objects,
change selection/focus, start or stop PIE/SIE, generate PCG, save, or emit
notifications with authored effects.

The registry may hold candidates strongly only until terminalization. It then
releases all Python and UObject references. Fast success and polled success
return the same fixed JSON and normalized SAL records; `poll` never resolves or
projects the UObject again. Retention stores no UObject pointer.

A later SAL request always re-resolves its canonical Target and StableRef. The
projection is evidence and routing help, not a snapshot guarantee, bearer
credential, runtime handle, or permission to mutate. Transient Worlds, runtime
tasks, generated resources, Data Views, selection, cameras, and other objects
without faithful SAL identity remain ordinary Python facts or belong to a
separate typed frontend.

## Result Model

The canonical MCP payload is `structuredContent`. Logs never replace the
returned object. The existing `content` JSON mirror of the complete Python
envelope remains unchanged for ordinary results and remains present when a
projection annex exists. For each successful projection view, the Client then
appends two deterministic text blocks: first an ordinary metadata block with
projection id, result paths, and relation; then a separate block containing
only that view's canonical SAL Result Text. Metadata never prefixes or alters
the canonical block. Projections follow numeric id order, paths follow return-
tree traversal order, and views follow published Domain catalog order.

The heterogeneous Python result has no one active Target and must not be
formatted as `result exact_target` or rewritten into a new mixed Object Text
syntax. In particular, the JSON placeholder stays exactly
`{ "$salObject": "o0" }`; presentation never changes its field name or wraps
it in a semantic tag. Each successful projection view's `subject`, by
contrast, is an independently valid normalized exact Query result and may be
formatted as its own canonical SAL Result Text.

The public output schema below is the Python frontend's closed concretization
of the shared outer async lifecycle, not a schema that every future frontend
inherits verbatim. `continuation.tool: "python"`, Python error phases, logs,
and traceback are Python-only. A typed PCG frontend may reuse the kernel's
outer states, continuation discipline, retention, runtime-loss, uncertainty,
and no-replay rules only through its own closed typed schema and namespaced
ids.

The Python public output schema is:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["status", "stateMayHaveChanged"],
  "properties": {
    "status": {
      "type": "string",
      "enum": ["running", "succeeded", "failed", "lost"]
    },
    "executionId": { "type": "string", "minLength": 1 },
    "stateMayHaveChanged": { "type": "boolean" },
    "result": {
      "type": "object",
      "additionalProperties": true
    },
    "sal": { "$ref": "#/$defs/SalProjectionAnnex" },
    "error": {
      "type": "object",
      "required": ["code", "phase", "message", "retryable"],
      "properties": {
        "code": { "type": "string", "minLength": 1 },
        "phase": {
          "type": "string",
          "enum": ["validation", "staging", "execution", "result", "runtime"]
        },
        "message": { "type": "string", "minLength": 1 },
        "traceback": { "type": "string" },
        "retryable": { "type": "boolean" }
      },
      "additionalProperties": false
    },
    "logs": {
      "type": "array",
      "maxItems": 1000,
      "items": {
        "type": "object",
        "required": ["type", "output"],
        "properties": {
          "type": {
            "type": "string",
            "enum": ["info", "warning", "error"]
          },
          "output": { "type": "string" }
        },
        "additionalProperties": false
      }
    },
    "logsTruncated": { "type": "boolean" },
    "durationMs": { "type": "integer", "minimum": 0 },
    "elapsedMs": { "type": "integer", "minimum": 0 },
    "continuation": {
      "type": "object",
      "required": ["tool", "arguments", "pollAfterMs"],
      "properties": {
        "tool": { "const": "python" },
        "arguments": {
          "type": "object",
          "required": ["operation", "executionId"],
          "properties": {
            "operation": { "const": "poll" },
            "executionId": { "type": "string", "minLength": 1 }
          },
          "additionalProperties": false
        },
        "pollAfterMs": { "type": "integer", "minimum": 0 }
      },
      "additionalProperties": false
    }
  },
  "allOf": [
    {
      "if": {
        "properties": { "status": { "const": "running" } },
        "required": ["status"]
      },
      "then": {
        "required": ["executionId", "elapsedMs", "continuation"],
        "properties": { "stateMayHaveChanged": { "const": true } },
        "not": {
          "anyOf": [
            { "required": ["result"] },
            { "required": ["sal"] },
            { "required": ["error"] },
            { "required": ["logs"] },
            { "required": ["logsTruncated"] },
            { "required": ["durationMs"] }
          ]
        }
      }
    },
    {
      "if": {
        "properties": { "status": { "const": "succeeded" } },
        "required": ["status"]
      },
      "then": {
        "required": ["result", "logs", "logsTruncated", "durationMs"],
        "properties": { "stateMayHaveChanged": { "const": true } },
        "not": {
          "anyOf": [
            { "required": ["error"] },
            { "required": ["elapsedMs"] },
            { "required": ["continuation"] }
          ]
        }
      }
    },
    {
      "if": {
        "properties": { "status": { "const": "failed" } },
        "required": ["status"]
      },
      "then": {
        "required": ["error"],
        "not": {
          "anyOf": [
            { "required": ["result"] },
            { "required": ["sal"] },
            { "required": ["elapsedMs"] },
            { "required": ["continuation"] }
          ]
        }
      }
    },
    {
      "if": {
        "properties": { "status": { "const": "lost" } },
        "required": ["status"]
      },
      "then": {
        "required": ["executionId", "error"],
        "properties": { "stateMayHaveChanged": { "const": true } },
        "not": {
          "anyOf": [
            { "required": ["result"] },
            { "required": ["sal"] },
            { "required": ["logs"] },
            { "required": ["logsTruncated"] },
            { "required": ["durationMs"] },
            { "required": ["elapsedMs"] },
            { "required": ["continuation"] }
          ]
        }
      }
    }
  ],
  "$defs": {
    "ResultPath": {
      "type": "array",
      "minItems": 1,
      "items": {
        "oneOf": [
          { "type": "string" },
          { "type": "integer", "minimum": 0 }
        ]
      }
    },
    "SalReadbackQuery": {
      "type": "object",
      "required": ["tool", "arguments"],
      "properties": {
        "tool": { "const": "sal_query" },
        "arguments": {
          "type": "object",
          "required": ["text"],
          "properties": {
            "text": { "type": "string", "minLength": 1 }
          },
          "additionalProperties": false
        }
      },
      "additionalProperties": false
    },
    "SalProjectionView": {
      "type": "object",
      "required": ["relation", "schemaModule", "subject", "query"],
      "properties": {
        "relation": { "enum": ["exact", "authored_source"] },
        "schemaModule": { "type": "string", "minLength": 1 },
        "subject": {
          "allOf": [
            { "$ref": "./sal-object.schema.json#/$defs/ExactQueryResult" },
            { "required": ["object"] }
          ]
        },
        "query": { "$ref": "#/$defs/SalReadbackQuery" }
      },
      "additionalProperties": false
    },
    "SalProjection": {
      "type": "object",
      "required": ["id", "resultPaths", "status", "views", "diagnostics"],
      "properties": {
        "id": { "type": "string", "pattern": "^o[0-9]+$" },
        "resultPaths": {
          "type": "array",
          "minItems": 1,
          "maxItems": 128,
          "items": { "$ref": "#/$defs/ResultPath" }
        },
        "status": {
          "enum": ["projected", "unsupported", "ambiguous", "stale", "failed"]
        },
        "views": {
          "type": "array",
          "maxItems": 16,
          "items": { "$ref": "#/$defs/SalProjectionView" }
        },
        "diagnostics": {
          "type": "array",
          "items": {
            "$ref": "./sal-object.schema.json#/$defs/Diagnostic"
          }
        }
      },
      "allOf": [
        {
          "if": {
            "properties": { "status": { "const": "projected" } },
            "required": ["status"]
          },
          "then": { "properties": { "views": { "minItems": 1 } } },
          "else": {
            "properties": {
              "views": { "maxItems": 0 },
              "diagnostics": { "minItems": 1 }
            }
          }
        }
      ],
      "additionalProperties": false
    },
    "SalProjectionAnnex": {
      "type": "object",
      "required": ["complete", "projections"],
      "properties": {
        "complete": { "type": "boolean" },
        "projections": {
          "type": "array",
          "minItems": 1,
          "maxItems": 32,
          "items": { "$ref": "#/$defs/SalProjection" }
        }
      },
      "additionalProperties": false
    }
  },
  "additionalProperties": false
}
```

`SalProjectionAnnex` is a closed Python-output definition. It requires
`complete` and bounded `projections`. Each projection requires `id`,
`resultPaths`, `status`, `views`, and `diagnostics`; each view requires
`relation`, `schemaModule`, a `subject` validated as an exact read-only Query
result with Object Text, and a canonical read-only `query`. The source schema
uses a package-local reference to `sal-object.schema.json`; the build resolves
and inlines those definitions into the advertised MCP `outputSchema`. A host
never needs network access, and the Client/Bridge protocol version pins the
exact bundled SAL schema revision.

`schemaModule` must match the projected subject's main Target Domain and its
published static card. Parsing `query.arguments.text` must yield a read-only
exact Query for that same canonical Target and the same projected object. It
may request schema alongside the object, but it cannot broaden to a collection
or Domain root. A view that cannot supply all of this is not `projected`.

The JSON Schema expresses per-record shape. The Client and Bridge additionally
enforce cross-record integrity: projection ids are unique; result paths are
unique and point to the matching placeholder; there are no orphan or extra
placeholders; and the sum of all marker occurrences is at most 128. The
`complete` field is `false` exactly when at least one projection has status
`failed`; ordinary `unsupported`, `ambiguous`, and `stale` classifications are
complete.

The status-specific invariants are normative:

- `running` requires `executionId`, `elapsedMs`, and `continuation`; it has no
  `result`, `sal`, `error`, `logs`, or `durationMs`;
- `succeeded` requires `result`, `logs`, `logsTruncated`, and `durationMs`; it
  has no `error`, `elapsedMs`, or continuation; `sal` is present only when at
  least one reachable `sal.object()` marker was returned;
- `failed` requires `error`; an executed failure also returns terminal logs and
  duration, while a pre-execution failure may omit them;
- `failed` and `lost` never contain `sal`; `lost` requires `executionId` and
  `error` and has no result;
- terminal results returned through `poll` include `executionId`; fast terminal
  `run` results do not.

The MCP tool result sets `isError=true` for `failed` and `lost`, but their
structured payload remains available. `running` and `succeeded` do not set
`isError`.

### Fast success

Most scripts should finish inside the inline completion window and return
directly:

```json
{
  "status": "succeeded",
  "stateMayHaveChanged": true,
  "result": {
    "assetPath": "/Game/Data/DT_Weapons.DT_Weapons",
    "rowsCreated": 42,
    "saved": true
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 184
}
```

A fast terminal result does not expose an `executionId`. Loomle may use an
internal record while the call is running, but normal agent workflows do not
need to see a job abstraction.

When the script returns `sal.object(actor)`, the same fast success may include:

```json
{
  "status": "succeeded",
  "stateMayHaveChanged": true,
  "result": {
    "mode": "sie",
    "actor": { "$salObject": "o0" }
  },
  "sal": {
    "complete": true,
    "projections": [
      {
        "id": "o0",
        "resultPaths": [["actor"]],
        "status": "projected",
        "views": [
          {
            "relation": "authored_source",
            "schemaModule": "level",
            "subject": {
              "targetContext": "exact_target",
              "target": {
                "alias": "levelTarget",
                "target": {
                  "kind": "target",
                  "domain": "level",
                  "asset": "/Game/Maps/Forest.Forest",
                  "type": "/Script/Engine.World"
                }
              },
              "object": {
                "statements": [
                  {
                    "target": { "kind": "local", "name": "actor" },
                    "value": {
                      "kind": "stable_ref",
                      "identityPath": ["11111111-1111-1111-1111-111111111111"],
                      "semanticTag": "actor"
                    }
                  }
                ]
              },
              "diagnostics": []
            },
            "query": {
              "tool": "sal_query",
              "arguments": {
                "text": "levelTarget = target {\n  domain: level,\n  asset: \"/Game/Maps/Forest.Forest\",\n  type: \"/Script/Engine.World\"\n}\nquery levelTarget\n@11111111-1111-1111-1111-111111111111\nwith schema"
              }
            }
          }
        ],
        "diagnostics": []
      }
    ]
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 12
}
```

### Running continuation

If an admitted script is still executing after Loomle's short inline
completion window, `run` returns a non-error running result:

```json
{
  "status": "running",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "elapsedMs": 1007,
  "continuation": {
    "tool": "python",
    "arguments": {
      "operation": "poll",
      "executionId": "py_01K..."
    },
    "pollAfterMs": 1000
  }
}
```

The MCP text mirror must explicitly say that execution is still running, the
script must not be replayed, and the exact continuation should be called.

This outcome is not a timeout and does not set MCP `isError`. It means only
that the request has detached from an execution that already started.

### Poll while running

The agent calls `poll` only after receiving a continuation:

```json
{
  "operation": "poll",
  "executionId": "py_01K..."
}
```

If execution is not terminal, `poll` returns the same running shape with an
updated `elapsedMs` and continuation. `poll` is a snapshot read and returns
quickly; it does not wait for another long interval.

### Polled success

When the detached execution completes successfully, `poll` returns:

```json
{
  "status": "succeeded",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "result": {
    "assetPath": "/Game/Data/DT_Weapons.DT_Weapons",
    "rowsCreated": 42,
    "saved": true
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 2841
}
```

The terminal `poll` result retains `executionId` so the response remains
self-identifying. Repeating the same `poll` during its retention period returns
the same terminal outcome and does not re-execute Python. If it contains a
projection annex, every placeholder, status, view, diagnostic, and subject is
the terminal snapshot formed once after `run()` returned; polling never
reprojects the UObject.

### Executed failure

A Python exception or invalid returned value after execution starts produces:

```json
{
  "status": "failed",
  "stateMayHaveChanged": true,
  "error": {
    "code": "runtime.python_execution_failed",
    "phase": "execution",
    "message": "Object has no editor property named damage",
    "traceback": "Traceback (most recent call last): ...",
    "retryable": false
  },
  "logs": [],
  "logsTruncated": false,
  "durationMs": 23
}
```

A detached failure also contains its `executionId`. Executed failures set MCP
`isError=true` without discarding `structuredContent`.

`stateMayHaveChanged` is conservative:

- validation or staging failure before the script begins uses `false`;
- top-level execution, `run()`, serialization failure after `run()`, and all
  uncertain detached outcomes use `true`;
- successful execution uses `true` because Loomle cannot prove that arbitrary
  Python was read-only.

No failure with `stateMayHaveChanged=true` may invite an automatic retry.

### Lost execution

If the owning Editor exits, crashes, or is replaced before Loomle observes a
terminal outcome, `poll` returns an error result:

```json
{
  "status": "lost",
  "executionId": "py_01K...",
  "stateMayHaveChanged": true,
  "error": {
    "code": "runtime.python_execution_lost",
    "phase": "runtime",
    "message": "The Editor runtime that owned this execution is no longer available.",
    "retryable": false
  }
}
```

`lost` does not mean rollback and does not prove whether the script completed.

### Output bounds

The serialized script-owned `result` plus optional Bridge-owned `sal`
projection annex is bounded to 1 MiB of UTF-8 encoded JSON. Outer lifecycle
fields and logs do not count toward that 1 MiB result bound; logs retain their
separate limit below. The result/annex pair is never silently truncated because
truncation would change the agent-defined shape or detach a marker from its
projection. An oversized pair fails with `runtime.python_result_too_large`
after execution and therefore reports `stateMayHaveChanged=true`.

V1 accepts at most 32 unique reachable UObject candidates and 128 total marker
occurrences. Repeated references to the same candidate reuse one projection id
and list all bounded `resultPaths`. The implementation also applies the
existing nesting/depth defenses before native candidate registration. It never
truncates candidates or views to make the payload fit.

Terminal log output preserves UE's ordered `info`, `warning`, and `error`
entries. It is bounded to 1,000 entries and 256 KiB of combined UTF-8 text.
Deterministic head-and-tail truncation sets `logsTruncated=true` and does not
change execution success.

The native `FPythonCommandEx` log array becomes available when its synchronous
call finishes. The first version therefore returns logs with terminal results;
`poll` is not a live log-streaming API. Captured Python output is not a complete
copy of UE's global Output Log, and asynchronous output after `run()` returns
is outside the execution result.

## Shared Kernel And Python Adapter Boundary

The future shared internal kernel owns only frontend-neutral lifecycle:

- namespaced opaque execution-id allocation and exact runtime routing;
- synchronized outer `running`/`succeeded`/`failed`/`lost` transitions;
- exact continuation/poll bookkeeping;
- bounded retention, expiry, runtime loss, `stateMayHaveChanged`, and no
  automatic replay.

The Python adapter continues to own:

- the one-active-Python-execution policy and Core Ticker pending slot;
- source staging, interpreter initialization, and safe Game Thread entry;
- Python result validation, logs, traceback, and Python-specific diagnostics;
- injected `sal.object()` marker handling, candidate registration, terminal SAL
  projection, and the optional projection annex;
- the versioned `python.run`/`python.poll` public and private schemas.

The existing `Loomle::Python::FPythonExecutionService` may be internally
factored so its frontend-neutral record lifecycle uses the shared kernel. That
refactor must not independently change timing semantics, concurrency policy,
ordinary JSON results, or packaged behavior. The separately versioned
`sal.object()` extension changes only terminal successful output when a marker
is reachable. A PCG adapter uses a separate namespaced frontend and never
reaches native PCG by submitting Python.

## Execution Lifecycle

### Admission and fast completion

`run` has two distinct timing boundaries:

1. **Game Thread admission budget.** The pending execution must atomically
   enter `started` from Loomle's persistent Core Ticker callback within the
   existing short admission budget. If it does not, Loomle cancels it before
   execution and returns `runtime.editor_unresponsive`.
2. **Inline completion window.** After `started`, Loomle waits only a short
   internal interval, initially approximately one second, for a terminal
   result. If the script is still running, Loomle returns its continuation.

The inline completion window is not an execution timeout, not a user option,
and not a service-level promise about task duration. Its only purpose is to
keep the common fast path to one MCP call while ensuring an unexpectedly slow
script promptly yields control back to the agent.

The boundary is race-safe: the execution record has one synchronized terminal
transition. Loomle returns either the observed terminal result or a handle to
that same continuing execution, never both independent outcomes.

### Safe Game Thread entry

Running on the Game Thread is necessary but not sufficient for unrestricted
Unreal Python. Loomle must also enter Python from a UE call stack that permits
synchronous engine APIs to pump Game Thread work.

In particular, Loomle must never start Python from
`AsyncTask(ENamedThreads::GameThread, ...)`, a TaskGraph callback, package or
asset loading, package saving, or garbage collection. Interchange synchronous
waits may process the Game Thread TaskGraph queue. Starting Python from that
same queue and then importing an asset can recursively process the named-thread
queue and trigger UE's TaskGraph recursion guard.

The Bridge therefore owns one persistent zero-delay
`FTSTicker::GetCoreTicker()` callback. `python.run` stages one execution in a
thread-safe pending slot, and only that callback may atomically admit and call
`ExecPythonCommandEx`. This applies even when the submitting caller already
runs on the Game Thread: thread identity does not prove that its call stack is
safe. UE's Interchange task system explicitly identifies an Engine tick and the
Core Ticker as safe contexts for synchronous waits.

The pending slot is not a general queue. It belongs to the one active execution
record and is cleared when the ticker claims it, admission is cancelled, or the
Editor begins shutdown.

### Detached does not mean background UE execution

The Python call itself remains on the Game Thread because the script has full
direct `unreal` access. Returning an `executionId` detaches the MCP request; it
does not move arbitrary UObject work onto a safe worker thread.

A long or blocked script can therefore freeze Editor UI and prevent other
UE-backed Loomle operations from entering the Game Thread. This limitation is
fundamental to unrestricted Unreal Python. Loomle must not describe detached
execution as safe parallelism.

### Poll path

`poll` reads a thread-safe execution record on the Bridge listener/worker path.
It must not dispatch to the Game Thread and must remain callable while the Game
Thread is occupied by the script.

The Client binds the returned opaque execution handle to the exact runtime
that produced it. Polling must target that runtime rather than re-resolving the
project to a replacement Editor. A changed or vanished runtime produces
`lost`, never a lookup against another Editor.

An exposed execution remains available while running. After it becomes
terminal, the Bridge retains the bounded result for at least 30 minutes while
the same Editor runtime remains alive. The implementation may discard the
record sooner only after a terminal result has been successfully returned and
the documented replay grace period has elapsed. Polling an expired handle
returns `runtime.python_execution_expired` and never re-runs the script.

### Concurrency

One Editor runtime admits at most one Python fallback execution at a time. A
second `run` while one is active fails before execution with
`runtime.python_busy` and returns the active `executionId` only if that id was
already exposed.

Loomle does not queue a hidden sequence of Python scripts. A queued script
could otherwise begin mutating UE after the requesting agent had moved on.

## Private Bridge Protocol

The one public tool maps to two private operations:

```text
python.run
python.poll
```

Private separation is required because their execution paths differ:

- `python.run` performs project/runtime preflight, stages source, enters Game
  Thread admission from the persistent Core Ticker, and starts the execution;
- `python.poll` performs an exact-runtime, non-Game-Thread record lookup.

Both use the existing JSON-RPC transport. Implementation increments the
current Client-Bridge protocol version and advertises both operations through
`rpc.capabilities`.

These remain the Python frontend's private routes. The shared kernel is an
internal service, not a new public RPC namespace, and another typed frontend
must define and advertise its own private operation schema.

The `executionId` is opaque and unique to one Editor runtime. It is not an
idempotency key, caller-selected request id, permission token, or durable
cross-restart job identity.

## Python Runner and Result Transport

UE's `ExecuteFile` mode normally returns `None` in `CommandResult`; it does not
directly return the Python value produced by an arbitrary `run()` function.
Loomle therefore owns a small staged runner protocol rather than treating
native log output as the result.

For each execution, the Bridge creates unique files below a Loomle-owned
project `Saved` directory:

- the agent source file;
- a Loomle runner file;
- a result JSON path.

The runner executes the source in a fresh dictionary, injects the execution-local
`sal` helper, validates and calls `run()`, recursively validates its returned
value, and serializes it with strict JSON rules including finite numbers. The
Bridge invokes the runner through `FPythonCommandEx` using:

```text
ExecutionMode = ExecuteFile
FileExecutionScope = Private
Flags = Unattended
```

The projection extension adds an execution-local native candidate registry.
During sentinel-aware result traversal, each reachable marker registers the
actual UObject through a private Bridge binding and receives an opaque ordinal.
That traversal also records the ordinal and every exact result path in an
authoritative native registry table. The transport file contains JSON
placeholders, candidate ordinals, and an untrusted mirror of that occurrence
table; it never uses an object path to reconstruct the candidate. The registry rejects a wrong
execution, invalid UObject, over-limit candidate, duplicate ordinal, duplicate
path, orphan placeholder, or any mismatch between the native table and the
returned JSON. The Bridge never discovers trusted occurrences by scanning for
caller-authored dictionaries that merely resemble a placeholder. It briefly
retains each unique candidate until terminalization.

The runner writes only its result transport document to the result path. The
Bridge reads and independently validates that document. Still on the Game
Thread after `ExecPythonCommandEx` returns, it verifies the untrusted mirror
against the authoritative native occurrence table, every exact path, every
placeholder, the global
128-occurrence limit, and every reachable registered candidate; it then
invokes the published read-only Domain projectors, validates each resulting
exact Query subject, and assembles the top-level `sal` annex. It then releases
every candidate and helper-owned Python/UObject reference before publishing
the terminal execution record.

The retained execution record contains only bounded JSON, normalized SAL data,
native logs, and outcome metadata. `python.poll` never touches the candidate
registry or Game Thread. Failure at any staging, validation, projection, or
terminal-formatting boundary releases all candidates. The Bridge removes all
staging files on ordinary success and failure.

The source path gives Python tracebacks a useful filename. Paths containing
spaces must be correctly quoted for UE's native `.py` command parser.

This staging protocol is not a sandbox. The executed source has full process
permissions and can inspect, modify, move, or delete staging files. Cleanup is
best effort, and a process crash may leave files behind. Agents must not place
credentials or other secrets in scripts. Loomle never echoes or deliberately
logs the complete source, although a Python traceback may contain relevant
source lines.

The service must live in a focused Python execution component. It must not
restore Loomle 0.6's direct-tool runtime, monkey-patch `unreal`, install
process-global signal handlers, or depend on shared console globals.

## Python Availability And Explicit Editor/PIE/SIE Control

The Bridge declares `PythonScriptPlugin` as a plugin/module dependency and
requests Python enablement during Editor initialization, outside an agent
execution. It tracks `OnPythonInitialized` and `OnPythonShutdown` and verifies
`IsPythonConfigured`, `IsPythonAvailable`, and `IsPythonInitialized` before
admission.

A tool call never busy-waits for interpreter initialization on the Game
Thread. Not ready and unavailable states return distinct errors.

`run` remains available while PIE or SIE is active. Loomle does not choose an
Editor World or Play World and does not own a second play-session lifecycle
state machine. The script must explicitly obtain the world required by the
task through UE's native Python APIs.

PIE/SIE start and stop requests are asynchronous Game Thread state transitions.
One Python execution can submit the exact engine-supported request, but it must
return before UE can advance that request on later Editor ticks. An agent
therefore controls PIE or SIE with multiple short `python.run` calls:

1. inspect the current play state and request start when necessary;
2. return immediately, then use a new `python.run` call to confirm the exact
   requested play/simulate mode and reacquire the resulting World;
3. run short observation or mutation scripts, reacquiring the world and every
   UObject on each call;
4. request end play in a separate call and later confirm that PIE/SIE stopped.

Possess/eject, selection, viewport camera, session-only visibility, Actor
loading/pinning, and similar transient Editor controls also belong to explicit
short Python calls with native postcondition readback. They are not SAL Patch,
do not gain authored transaction/save semantics, and have no parallel SAL
Session Domain. A later readback call may return selected persistent or live
duplicate UObjects through `sal.object()` so the Bridge can project exact or
`authored_source` SAL views; that projection does not represent the transient
World itself. The resident Skills will carry the exact version-appropriate
procedures after the structured Domain family and projection extension are
implemented and validated.

`python.poll` is only the continuation for one already-running Python
execution. It must not be used to wait for PIE/SIE to start, stop, change
possess/eject state, or advance a frame. A Python script must not sleep or
busy-wait for play-session state because it occupies the Game Thread and
prevents the transition or gameplay tick it is waiting for.

Starting PIE or SIE and changing possess/eject state changes the user's active
Editor session. Agent workflow guidance must ask for permission unless the
current instruction already explicitly authorizes running or debugging that
mode. The agent should normally stop a session it started after completing the
requested debugging, unless the user asks to leave it running.

## Dry Run, Transactions, and Side Effects

Full Python cannot implement Loomle's mutation dry-run contract. Source cannot
be reliably reduced to parse, resolve, validate, and plan phases before it
runs. Runtime reflection and ordinary control flow can choose operations
dynamically.

Effects that an Editor transaction cannot generally reverse include:

- saving or deleting packages and files;
- changing config, console variables, subsystem, or process-global state;
- spawning processes or making network requests;
- invoking UE APIs that do not call `Modify`;
- starting asynchronous work that outlives `run()`.

The Bridge does not wrap arbitrary code in a Loomle-owned transaction.
`FScopedTransaction::Cancel()` would remove a transaction record but would not
generally restore effects already applied. The result never reports SAL
mutation fields such as `valid`, `planned`, `applied`, `diff`, or revisions.

The script may choose a UE transaction or explicit save operation when that is
correct for its specific domain, but Loomle makes no generic guarantee about
undoability, persistence, or rollback.

## Cancellation, Failure, and Replay

There is no safe generic way to preempt unrestricted Python while it may be
executing native UE code on the Game Thread. Injecting an exception,
terminating a thread, or shutting down the interpreter can corrupt Editor
state.

Consequently:

- `python` has no cancel operation;
- the inline completion window never attempts to stop execution;
- `poll` observes but does not control the execution;
- caller abort after Game Thread admission abandons only the wait;
- the Client and Bridge never automatically retry or replay a script;
- Editor termination may be the only recovery from an infinite loop, with
  possible loss of unsaved work.

The normal continuation path prevents a slow script from reaching the outer
transport timeout. It cannot eliminate every uncertain outcome. A connection
loss, MCP host cancellation, Client crash, or Editor crash before the running
continuation is delivered may leave the agent without an `executionId` even
though Python started. That case reports outcome uncertainty when possible and
must never invite blind replay.

Admission cancellation remains safe only before the task reaches `started`.
After `started`, every failure is treated as potentially mutating.

## Errors

The stable planned errors are:

| Code | Meaning | Retry guidance |
| --- | --- | --- |
| `tool.invalid_arguments` | invalid operation, missing script/id, oversized input, or unknown field | fix the request |
| `runtime.python_unavailable` | target build or plugin has no usable Python | do not retry unchanged |
| `runtime.python_initializing` | Python was requested but is not ready | retry after readiness changes |
| `runtime.python_source_staging_failed` | staging failed before Python began | repair filesystem state |
| `runtime.python_busy` | another fallback execution is active | follow its exposed continuation or wait |
| `runtime.python_execution_failed` | source, entry point, or Python/UE execution failed | inspect traceback and current state |
| `runtime.python_invalid_result` | `run()` did not return a compatible dictionary | inspect current state; fix source |
| `runtime.python_result_too_large` | the structured result exceeded its bound | inspect current state; return a smaller projection |
| `runtime.python_execution_not_found` | id is unknown to the owning live runtime | verify the exact continuation |
| `runtime.python_execution_expired` | retained terminal result has expired | do not replay automatically |
| `runtime.python_execution_lost` | the owning runtime vanished before a terminal outcome was observed | inspect project state; do not replay automatically |
| `runtime.editor_unresponsive` | Game Thread did not admit the task | retry only after Editor responsiveness returns |
| `runtime.editor_shutting_down` | Editor is draining | retry after restart and state inspection |

An invalid `sal.object()` argument, marker in an illegal result position,
candidate-limit breach, or forged/unregistered placeholder is
`runtime.python_invalid_result` with phase `result`. Valid per-candidate
`unsupported`, `ambiguous`, and `stale` outcomes are not outer Python errors;
they use registered projection diagnostics inside the Bridge-owned annex. A
projector/integrity failure likewise preserves the script's successful result,
marks the affected projection `failed`, and sets `sal.complete: false`.

Project binding, multiple-Editor, startup, protocol, and transport errors retain
their existing codes. Python-specific formatting must override any generic
“retryable timeout” suggestion after execution may have started.

## Implementation Scope

The first implementation changes:

- the Client public tool definition, routing, validation, structured result
  formatting, and tests;
- Bridge capabilities and the `python.run`/`python.poll` RPC paths;
- a focused staged Python runner and thread-safe Python execution-record
  service;
- plugin/module dependency declarations and initialization tracking;
- the generated private protocol version;
- diagnostic catalog entries;
- runtime liveness behavior so `poll` bypasses Game Thread readiness while
  preserving exact runtime identity;
- current tool-count, dry-run-policy, coverage, packaging, and release-test
  documentation.

The planned `sal.object()` extension additionally changes:

- the generated runner to inject the helper and encode explicit markers;
- a private execution-local native UObject candidate registry;
- a read-only projector registry backed only by published SAL Domains;
- terminal Game-Thread projection, shared exact-Query-result validation, and
  deterministic release of every candidate;
- the Client/Bridge closed success schema with optional top-level `sal`;
- succeeded-result text formatting that preserves the existing complete JSON
  mirror and appends a metadata block plus a separate canonical Result Text
  block for each successful projection view;
- protocol version, diagnostics, tests, and packaged acceptance.

It does not add:

- `exec` or `eval` public modes;
- arbitrary caller-provided script paths;
- caller-provided purpose, schema, timeout, or execution id;
- cancellation, priority, parallel execution, or a public arbitrary job
  system; the frontend-neutral internal lifecycle kernel is not such a public
  system;
- live log streaming;
- automatic UObject scanning or serialization; only explicit `sal.object()`
  candidates are projected;
- cross-Editor restart recovery;
- fake dry-run, transaction, or rollback claims;
- Python-based orchestration of SAL; generated readback queries are returned as
  data and execute only through a later explicit `sal_query` call.

## Verification

### Client tests

Tests must prove:

- exactly eight public tools are listed and `python` has the conservative
  annotations above;
- the `run` and `poll` input branches reject missing, mixed, unknown, empty,
  and oversized fields;
- `run` uses the currently bound healthy runtime;
- `poll` follows the exact runtime bound to its returned execution id and does
  not jump to a replacement Editor;
- fast success and failure expose structured content without an execution id;
- running results expose the exact `python`/`poll` continuation and are not MCP
  errors;
- terminal polled failures retain error, logs, traceback, and
  `stateMayHaveChanged` detail;
- an ordinary succeeded JSON result omits top-level `sal` and remains byte-for-
  byte structurally compatible;
- a succeeded result with markers validates the closed annex, imported SAL
  exact Query result, canonical readback query, and status-specific field rules;
- running, failed, and lost results reject `sal`; script-owned `result.sal` and
  forged `$salObject` dictionaries cannot spoof a Bridge projection;
- Client text preserves the complete Python-envelope JSON mirror, then formats
  each successful view as an ordinary metadata block followed by an independent
  canonical Result Text block, never one multi-Target result;
- cancellation or transport failure never causes automatic replay.

### UE Automation

Native tests must cover:

- capabilities advertise `python.run` and `python.poll`;
- a script can import `unreal`, define `run()`, and return a nested JSON object;
- empty dictionaries, Unicode, lists, nulls, Booleans, and finite numbers
  round-trip exactly;
- non-string keys, cycles, out-of-range integers, unwrapped UObject values,
  tuples, NaN, and infinity fail as invalid results;
- `sal.object()` accepts an exact UObject and rejects non-UObject arguments,
  marker keys, forged placeholders, wrong-execution registry entries, and
  candidate/occurrence overflow;
- nested markers, lists, repeated marker paths, and repeated calls for the same
  UObject deduplicate deterministically;
- published projectors can return multiple valid Domain views; unsupported,
  ambiguous, and stale candidates preserve outer success; projector integrity
  failure sets `complete: false` without returning an invalid Target;
- projection performs no load, map switch, dirtying, selection/focus, PIE/SIE
  transition, PCG generation, notification mutation, or save;
- fast and polled terminal paths return the same fixed annex, and repeated poll
  never reprojects;
- every candidate strong reference is released before terminal publication;
  retained records survive GC using only JSON and normalized SAL data;
- missing, parameterized, async, and generator `run()` definitions fail;
- syntax and runtime errors preserve useful tracebacks and prior native logs;
- source and runner paths containing spaces execute correctly;
- staging files are removed after ordinary success and failure;
- cleanup failure does not replace the execution outcome;
- the result size limit fails without producing malformed JSON;
- the combined `result + sal` 1 MiB bound fails without truncating markers,
  views, paths, or diagnostics;
- log truncation is deterministic and does not change success;
- Python submitted by an RPC worker starts from the Core Ticker rather than a
  TaskGraph Game Thread callback;
- synchronous `AssetTools.ImportAssetTasks` can import a transient image
  without recursively entering the Game Thread TaskGraph queue;
- Game Thread admission cancellation prevents later execution;
- a sub-window script returns directly without an exposed id;
- a script exceeding the inline window returns one id and later publishes the
  same terminal result;
- `poll` remains responsive from the worker path while the Game Thread script
  is active;
- repeated terminal polls do not re-execute the script;
- a second active `run` fails with `runtime.python_busy` and is never queued;
- Editor shutdown changes an unfinished exposed execution to `lost`;
- Python executes through the same safe Core Ticker entry while PIE is active;
- unavailable Python, initialization, and shutdown fail at their specified
  boundaries;
- no result claims dry run, rollback, idempotency, or safe cancellation.

Tests must use transient objects or copied fixtures and clean them explicitly.
Ordinary suites must not execute an infinite loop. Any destructive watchdog
test must own a disposable Editor process and its cleanup.

### Later Scene/PCG integration gates

Before the typed PCG frontend ships, integration tests must prove:

- extracting the shared kernel preserves the exact public Python schema and
  all fast-terminal/continuation behavior;
- a PCG-namespaced execution id is rejected by `python.poll` without probing a
  PCG adapter or another runtime;
- SIE start/stop and PIE start/stop use separate short Python calls with later
  native-state confirmation; `python.poll` is never used to wait for either
  Editor transition;
- possess/eject, selection, viewport camera, session-only visibility, and
  Actor loading/pinning controls use explicit short Python calls with native
  postcondition readback and are never invoked by a SAL Query/Patch;
- an Editor original Component projects as an exact published source view, a
  uniquely proven PIE/SIE duplicate projects only as `authored_source`, and a
  runtime-only/generated object produces no persistent Target;
- the typed PCG frontend obtains its own World selector and
  `pcgWorldTicket`; it never treats `sal.object()` or a projected Target as a
  live World ticket.

### Packaged end-to-end

The exact release archive must:

1. list the `python` tool with `run` and `poll` input branches;
2. execute a fast non-mutating script that imports `unreal` and returns a
   deterministic structured object;
3. surface one controlled Python exception with its traceback;
4. execute one deliberately delayed script, receive a continuation, and poll
   its final structured result;
5. execute one explicit `sal.object()` projection and verify its canonical
   independent SAL Result Text and readback query;
6. prove the Editor remains responsive after all terminal executions;
7. leave no Loomle staging files or candidate references after normal
   completion.

Release audit must also prove the Python plugin dependency is present and the
packaged Bridge still contains only the intended Loomle module set.

## Acceptance

The design is implemented only when:

- the public name, operations, schemas, annotations, results, and errors match
  this document;
- full `import unreal` execution works in the bound UE 5.7 Editor;
- `run()` returns a real structured object rather than using logs as data;
- ordinary JSON-only scripts remain compatible, while explicit `sal.object()`
  markers produce only Bridge-validated published SAL views;
- the common fast path completes in one MCP call;
- a slower admitted execution promptly returns a usable `poll` continuation;
- polling stays independent of the blocked Game Thread and never changes
  runtime identity;
- code is never advertised as sandboxed, transactional, dry-runnable,
  safely cancellable, or automatically retryable;
- current documentation continues to prefer structured Loomle interfaces for
  the semantics they cover;
- no Python projection is described or accepted as a live-object lease,
  handoff, World Target, Patch authorization, or PCG World ticket;
- focused Client tests, UE Automation, and packaged end-to-end validation pass
  on every supported platform.
