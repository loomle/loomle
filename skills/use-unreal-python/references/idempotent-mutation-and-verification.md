# Idempotent Mutation and Verification

## Design restartable calls

Make each mutating script safe to resume from current Editor state:

- resolve the target from a stable package path, object path, class path, GUID,
  or other durable identity inside every call;
- check whether an object already exists before creating it;
- compare current values before setting them;
- return an already-satisfied state as success;
- separate creation, stable rediscovery, configuration, saving, and readback
  when later steps would otherwise depend on temporary UObject identity;
- keep unrelated asset creation, graph editing, compilation, saving, and
  validation in different calls.

Idempotency is a property of the script's logic, not a guarantee supplied by
the `python` tool.

## Return structured evidence

Return a top-level dictionary composed only of JSON-compatible values. Include
facts that let another call or the user verify the outcome:

- canonical target paths and class paths;
- `created`, `reused`, `changed`, `alreadySatisfied`, `saved`, and `skipped`
  states where applicable;
- before and after values;
- warnings and bounded counts.

Never return a raw UObject wrapper. Native logs are diagnostics, not the
result. When a published SAL Domain can project a selected UObject,
`sal.object(value)` may explicitly request that additional read-only canonical
projection; still return the ordinary facts needed to interpret the operation.

## Resolve, compare, then mutate

Use one call to resolve stable targets and report current state. Use a later
call to rediscover those targets, compare current and intended values, and
apply only missing changes. Return durable identifiers for the next call,
never a cached UObject assumption.

```python
import unreal

def run():
    subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = subsystem.get_editor_world()
    return {
        "worldPath": world.get_path_name() if world else None,
        "available": world is not None,
    }
```

This example is read-only. The matching domain Skill owns the native identity
and mutation rules for assets, PIE objects, Graphs, Levels, and other
specialized targets.

## Persist and verify explicitly

Creation or property mutation in memory does not prove durable persistence.
When the task requires persistence:

1. use the owning structured interface's exact save operation when available;
2. otherwise save the exact native asset or package explicitly;
3. report the save result separately from the mutation result;
4. reload or independently query the target;
5. verify important identity, class, and property values;
6. use the owning structured interface for compilation or specialized checks.

Do not save unrelated dirty packages. A successful `run()` proves only what
its returned evidence and subsequent readback establish.
