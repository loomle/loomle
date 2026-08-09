# Continuation and Recovery

## Handle each execution status

| Status | Meaning | Required action |
| --- | --- | --- |
| `running` | The same Python execution has not published a terminal result | Wait the supplied interval and call the exact `python.poll` continuation. Never replay `run`. |
| `succeeded` | `run()` returned valid structured JSON | Independently read back important state; save separately when required. |
| `failed` | Validation, staging, Python execution, or result handling failed | Inspect `error`, logs, and `stateMayHaveChanged`; re-read affected state before deciding what remains. |
| `lost` | The owning runtime vanished before a retained terminal result was available | Call `status`, rebind only to the intended project, and reconstruct state from authoritative reads. Never assume rollback. |

An MCP timeout or disconnect may hide whether execution crossed the apply
boundary. Do not infer safety merely because no terminal result arrived.

## Poll exactly

When `python.run` returns `running`, use the returned continuation unchanged:

```text
python({ operation: "poll", executionId: "<returned-id>" })
```

Continue only while that same execution reports `running`. Do not resubmit the
source, invent another execution ID, or use polling to wait for unrelated UE
state such as a PIE transition or gameplay frame.

## Recover from uncertain mutation

For `failed`, `lost`, timeout, Editor restart, or any uncertain mutating call:

1. call `status` again and confirm the intended project/runtime;
2. discard cached UObject assumptions;
3. read every affected target through SAL or a read-only Python probe;
4. classify each intended effect as `applied`, `notApplied`, or `unknown`;
5. resolve unknown effects before mutation when possible;
6. run only missing idempotent steps;
7. verify the final state independently.

Do not replay a multi-step script because its last operation failed. Earlier
operations may already have changed Editor state.

## Partial-success example

Suppose a script creates `/Game/LoomleWork/M_AgentGenerated` and then raises an
exception while configuring it. The failed result may report
`stateMayHaveChanged: true`.

The recovery flow is:

1. read the exact object path without mutation;
2. classify creation as `applied` when the expected class is present;
3. skip creation instead of calling `create_asset` again;
4. run a separate idempotent configuration call that compares current values;
5. save only after configuration succeeds;
6. reload and verify the class and configured values;
7. report the original partial failure and the recovered final state.

If readback cannot distinguish the intended object from pre-existing state,
classify it as `unknown` and request direction instead of overwriting it.
