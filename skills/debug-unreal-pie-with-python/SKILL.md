---
name: debug-unreal-pie-with-python
description: Start, inspect, exercise, and stop an Unreal Engine Play In Editor session through Loomle's Python fallback. Use when reproducing gameplay behavior, querying runtime Actors or components, invoking project gameplay APIs, checking state across frames, or debugging PIE behavior that has no structured Loomle SAL interface.
---

# Debug Unreal PIE with Python

Use Loomle's public `python` tool to drive UE's native PIE APIs. Treat Python as
an escape hatch: prefer a structured Loomle interface when one already covers
the requested operation.

Read [pie-python-patterns.md](references/pie-python-patterns.md) before starting
PIE or writing a runtime inspection script.

## Obtain permission before starting

Starting PIE changes the user's active Editor session. Ask the user for
permission before requesting PIE unless their current instruction already
explicitly asks to run, test, reproduce, or debug behavior in PIE. Do not treat
permission to edit an asset as permission to start PIE.

Record whether this workflow started the session. Do not stop a PIE session
that was already running unless the user asks you to stop it.

## Follow the lifecycle as separate calls

1. Inspect PIE state with a short `python` `run` call.
2. If PIE is stopped and permission exists, submit
   `LevelEditorSubsystem.editor_request_begin_play()` and return immediately.
3. Use a new `python` `run` call to confirm both that PIE is active and that
   `UnrealEditorSubsystem.get_game_world()` returns a valid world.
4. Execute each setup, action, or observation as a separate short call. Let UE
   tick between calls whenever the behavior depends on gameplay frames.
5. Reacquire the Game World and every Actor, component, or subsystem on every
   call. Return paths, names, GUIDs, primitive properties, and other
   JSON-compatible facts instead of UObject wrappers.
6. When finished, request end play if this workflow started PIE, unless the
   user explicitly asks to leave it running. Confirm shutdown with a later
   call.

Start and stop are asynchronous requests owned by UE. A successful Python call
only proves that the request was submitted. Never claim that PIE started until
a later call observes a Game World, and never claim that it stopped until a
later call observes no active PIE session and no Game World.

## Keep every execution short

- Never call `sleep`, busy-wait, or loop until PIE state changes.
- Never wait for a frame, timer, latent action, streaming operation, or async
  load inside one Python execution.
- Never retain a PIE UObject across calls or after requesting end play.
- If `python.run` returns `status: "running"`, follow its exact `python.poll`
  continuation only to collect that same execution. Do not use `poll` to wait
  for PIE state, and do not replay the original script.
- Prefer bounded queries and compact result projections. Large world scans and
  long Python executions stall gameplay because Python occupies the Game
  Thread.

## Preserve the user's project

PIE mutations are normally transient, but Python can still save assets, write
files, change config, spawn processes, or invoke Editor APIs. Inspect the exact
script and obtain any additional authorization required for those persistent
effects. Do not save packages merely to complete a runtime observation.

Treat multi-client, dedicated-server, New Process, and Standalone sessions as
ambiguous unless the task supplies an exact native strategy for selecting the
correct world. The basic workflow is for one in-process PIE world.

## Report the outcome

State whether PIE was already running or started by this workflow, what runtime
objects and facts were observed, whether behavior was checked across separate
frames, whether cleanup was confirmed, and any uncertainty caused by startup,
shutdown, Python, or Editor failure.
