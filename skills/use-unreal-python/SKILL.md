---
name: use-unreal-python
description: Use Loomle's unrestricted Unreal Python fallback safely when no structured Loomle interface covers the required Unreal Editor capability. Apply live context checks, capability selection, API introspection, idempotent mutations, exact continuation polling, partial-state recovery, explicit persistence, and independent result verification.
---

# Use Unreal Python

Treat Loomle's public `python` tool as an unrestricted escape hatch inside the
bound Unreal Editor, not as a replacement for SAL or an agent-local runtime.

## Read the relevant guidance

- Always read [capability-and-api-discovery.md](references/capability-and-api-discovery.md)
  before the first Python call.
- Read [idempotent-mutation-and-verification.md](references/idempotent-mutation-and-verification.md)
  before any mutating Python call.
- Read [continuation-and-recovery.md](references/continuation-and-recovery.md)
  whenever a call returns `running`, `failed`, or `lost`, a transport outcome
  is uncertain, or the Editor restarts.
- For a PIE or Simulate workflow, also load and follow the resident
  `debug-unreal-pie-with-python` Skill before requesting a lifecycle change.

## Follow the fallback workflow

1. Call `status`. Confirm the intended project is bound and its Bridge is
   ready. Use `project` to establish an unambiguous binding when necessary.
2. Check `sal_schema`, `sal_query`, `sal_patch`, `editor`, and applicable
   domain Skills. Use Python only for the specific capability they do not
   provide, and state that gap when it matters to the user.
3. Probe the live engine for the required Python type, method, signature, or
   property before mutation. Do not rely only on remembered API names.
4. Inspect the exact script and obtain authorization for its effects. Python
   has process-level access and can save assets, write files, alter config,
   launch processes, or reach the network.
5. Run one bounded operation or tightly related batch. Resolve targets from
   stable paths or identifiers inside that call, inspect current state, and
   make the operation idempotent wherever UE permits.
6. Return a JSON-compatible dictionary containing structured evidence. Do not
   use logs or a returned UObject as proof.
7. Handle the reported execution status exactly. Poll only the supplied
   continuation; never replay an execution merely because it is slow or its
   outcome is uncertain.
8. Re-read affected state independently. Save explicitly only when persistence
   is required, then reload or query again to verify durable state.

## Keep structured interfaces authoritative

- Do not use Python to bypass a SAL validation or resolution error.
- Do not construct, connect, compile, or format ordinary K2 Graph content with
  Python when the owning Graph, Blueprint, and formatter workflows cover it.
- Hand Blueprint compilation, graph formatting, and other domain-owned
  verification back to their structured interface when available.
- Treat repeated Python use as evidence for a new structured interface only
  when native identity, validation, dry run, diff, revision, or specialized
  diagnostics would materially improve the workflow.

## Report the outcome

State why Python was necessary, which live API was confirmed, which targets
were created versus reused or changed versus already satisfied, whether state
was saved and independently verified, and any remaining applied/not-applied/
unknown effects after failure or recovery.
