# PIE Python Patterns

Use these patterns through Loomle's public `python` tool. Each source block
defines one synchronous, no-argument `run()` and returns a JSON-compatible
dictionary.

## Inspect state

```python
import unreal

def run():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = unreal_editor.get_game_world()
    return {
        "inPie": level_editor.is_in_play_in_editor(),
        "worldPath": world.get_path_name() if world else None,
    }
```

Interpret the result conservatively:

- `inPie: false`, `worldPath: null`: stopped;
- `inPie: true`, non-null `worldPath`: ready for runtime inspection;
- any other combination: a transition or unsupported session shape; let UE
  tick and inspect again with a new call.

## Request play

Use only after the permission rule in `SKILL.md` is satisfied.

```python
import unreal

def run():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level_editor.is_in_play_in_editor():
        return {"request": "not_needed", "observedState": "playing"}
    level_editor.editor_request_begin_play()
    return {"request": "submitted", "observedState": "stopped"}
```

Do not wait in this script. Confirm readiness later with the state inspection
pattern. To request Simulate In Editor instead, call
`level_editor.editor_play_simulate()` and apply the same later confirmation.

## Inspect runtime Actors

```python
import unreal

def run():
    world = unreal.get_editor_subsystem(
        unreal.UnrealEditorSubsystem
    ).get_game_world()
    if not world:
        return {"ready": False, "actors": []}

    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    selected = [actor for actor in actors if actor.actor_has_tag("DebugTarget")]
    return {
        "ready": True,
        "actors": [
            {
                "name": actor.get_name(),
                "path": actor.get_path_name(),
                "classPath": actor.get_class().get_path_name(),
                "location": {
                    "x": actor.get_actor_location().x,
                    "y": actor.get_actor_location().y,
                    "z": actor.get_actor_location().z,
                },
            }
            for actor in selected
        ],
    }
```

Prefer a specific class, tag, exact path, or other bounded predicate instead of
returning the entire world. Project UObject values into ordinary JSON facts.

## Act, then observe later

When behavior requires gameplay time, use one call to set up or invoke the
action and a later call to observe the result. Do not loop inside Python waiting
for the result.

For project APIs, resolve the runtime object again in each call and invoke only
functions known to be exposed to Unreal Python. Return enough identity and
before/after state for the user to verify what happened.

## Request stop

Only stop a pre-existing session when the user asked for that action. Normally
stop a session that this workflow started.

```python
import unreal

def run():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.is_in_play_in_editor():
        return {"request": "not_needed", "observedState": "stopped"}
    level_editor.editor_request_end_play()
    return {"request": "submitted", "observedState": "playing"}
```

Discard all prior PIE UObject knowledge after submitting this request. Confirm
shutdown with a new state inspection call; do not use an object obtained before
the stop request.

## Handle Loomle continuations

If any initial `python` call returns `status: "running"`, call the exact
continuation supplied by Loomle:

```text
python({ operation: "poll", executionId: "<returned-id>" })
```

This collects the terminal result of that one Python execution. It does not
allow PIE or gameplay to tick while the Python execution is still running.
