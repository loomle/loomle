---
name: debug-unreal-pie-with-python
description: Control and debug one in-process Unreal Engine Play In Editor or Simulate session through Loomle Python after applying the use-unreal-python safety workflow. Use when reproducing gameplay, querying runtime Actors or components, invoking gameplay APIs, checking state across frames, or managing PIE start and stop.
---

# Debug Unreal PIE with Python

Apply the resident `use-unreal-python` Skill first. This Skill adds only PIE
lifecycle, Game World, cross-frame, and cleanup semantics. Read
[pie-python-patterns.md](references/pie-python-patterns.md) before requesting a
play-session change or inspecting runtime objects.

## Obtain permission and track ownership

Starting PIE changes the user's active Editor session. Ask the user for
permission before requesting PIE unless their current instruction already
explicitly asks to run, test, reproduce, or debug behavior in PIE. Do not treat
permission to edit an asset as permission to start PIE.

Record whether this workflow started the session. Do not stop a session that
was already running unless the user asks you to stop it.

## Follow UE's asynchronous lifecycle

1. Inspect PIE state with one short Python call.
2. If stopped and authorized, submit
   `LevelEditorSubsystem.editor_request_begin_play()` or
   `editor_play_simulate()` and return immediately.
3. In a new call, require both `is_in_play_in_editor()` and a valid
   `UnrealEditorSubsystem.get_game_world()` before runtime work.
4. Reacquire the Game World and every Actor, component, or subsystem in every
   call. Never retain a PIE UObject after returning or requesting stop.
5. Split setup, gameplay action, frame-dependent progress, and observation into
   separate short calls so UE can tick between them.
6. If this workflow started PIE, submit `editor_request_end_play()` when done
   unless the user asks to leave it running. Confirm shutdown in a later call.

A successful start or stop script proves only that UE accepted the request.
Claim readiness only after a later call observes a Game World, and claim
shutdown only after a later call observes neither an active session nor a Game
World.

## Respect the Game Thread boundary

- Never sleep, busy-wait, or loop until PIE state, a timer, streaming, a latent
  action, or a gameplay frame changes.
- A still-running Python execution occupies the Game Thread. Its
  `python.poll` continuation collects that execution; it does not advance PIE.
- Prefer one in-process PIE world. Treat multi-client, dedicated-server, New
  Process, and Standalone sessions as ambiguous unless the task provides an
  exact native world-selection strategy.

## Report PIE-specific outcomes

State whether PIE was already running or started by this workflow, which Game
World was selected, what was observed across separate frames, whether the
session was intentionally left running, and whether shutdown was confirmed.
Use the base `use-unreal-python` workflow to report mutation, persistence,
verification, or uncertain recovery outcomes.
