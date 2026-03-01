# Loomle MCP Bridge (UE5 Plugin)

This plugin exposes a local MCP-compatible JSON-RPC endpoint from UE5 Editor over local IPC.

## Transport

- Windows: Named Pipe `\\.\\pipe\\loomle-mcp`
- macOS/Linux: Unix socket `<Project>/Intermediate/loomle-mcp.sock`
- Framing: newline-delimited JSON (one JSON object per line)
- Scope: local machine only

## Implemented MCP methods

- `initialize`
- `tools/list`
- `tools/call`
- `notifications/initialized` (accepted as notification)

## Tools (Current)

- `get_context`
- `get_selection_transform`
- `editor_stream`
  - Required argument: `action` = `start` | `stop` | `status`
  - Notifications method: `notifications/editor_stream`
  - Event types: `selection_changed`, `actor_moved`, `map_opened`
  - Events are also appended to `Saved/editor_stream_events.jsonl`
- `execute_python`
  - Required argument: `code` (inline Python string)
  - Optional argument: `mode` (`exec` default, or `eval`)

## BlueprintGraphBridge (C++ API exposed to Python)

`BlueprintGraphBridge` is a `UBlueprintFunctionLibrary` exposed as `unreal.BlueprintGraphBridge`.
Use it through `execute_python` for programmable, Python-driven BP construction.

Exposed methods:

- `create_blueprint(asset_path, parent_class_path)`
- `add_component(blueprint_asset_path, component_class_path, component_name, parent_component_name)`
- `set_static_mesh_component_asset(blueprint_asset_path, component_name, mesh_asset_path)`
- `set_scene_component_relative_location(blueprint_asset_path, component_name, location)`
- `set_scene_component_relative_scale3d(blueprint_asset_path, component_name, scale3d)`
- `set_primitive_component_collision_enabled(blueprint_asset_path, component_name, collision_mode)`
- `set_box_component_extent(blueprint_asset_path, component_name, extent)`
- `set_primitive_component_generate_overlap_events(blueprint_asset_path, component_name, b_generate)`
- `add_event_node(blueprint_asset_path, event_name, event_class_path, node_pos_x, node_pos_y)`
- `add_cast_node(blueprint_asset_path, target_class_path, node_pos_x, node_pos_y)`
- `add_call_function_node(blueprint_asset_path, function_class_path, function_name, node_pos_x, node_pos_y)`
- `connect_pins(blueprint_asset_path, from_node_guid, from_pin_name, to_node_guid, to_pin_name)`
- `set_pin_default_value(blueprint_asset_path, node_guid, pin_name, value)`
- `compile_blueprint(blueprint_asset_path)`
- `spawn_blueprint_actor(blueprint_asset_path, location, rotation)`

## Quick Python bridge example (via `execute_python`)

```python
import unreal
B = unreal.BlueprintGraphBridge
asset = "/Game/Codex/BP_PyBridgePad_Visible"

obj_path, err = B.create_blueprint(asset, "/Script/Engine.Actor")
err = B.add_component(asset, "/Script/Engine.SceneComponent", "Root", "")
err = B.add_component(asset, "/Script/Engine.BoxComponent", "Trigger", "Root")
err = B.add_component(asset, "/Script/Engine.StaticMeshComponent", "PadMesh", "Root")

err = B.set_static_mesh_component_asset(asset, "PadMesh", "/Engine/BasicShapes/Cube.Cube")
err = B.set_scene_component_relative_scale3d(asset, "PadMesh", unreal.Vector(2.2, 2.2, 0.35))
err = B.set_scene_component_relative_location(asset, "PadMesh", unreal.Vector(0, 0, 10))

event_guid, err = B.add_event_node(asset, "ReceiveActorBeginOverlap", "/Script/Engine.Actor", -220, 0)
cast_guid, err = B.add_cast_node(asset, "/Script/Engine.Character", 120, 0)
launch_guid, err = B.add_call_function_node(asset, "/Script/Engine.Character", "LaunchCharacter", 520, 0)

err = B.connect_pins(asset, event_guid, "then", cast_guid, "execute")
err = B.connect_pins(asset, event_guid, "OtherActor", cast_guid, "Object")
err = B.connect_pins(asset, cast_guid, "cast_succeeded", launch_guid, "execute")
err = B.connect_pins(asset, cast_guid, "As Character", launch_guid, "self")
err = B.set_pin_default_value(asset, launch_guid, "LaunchVelocity", "(X=0.0,Y=0.0,Z=1700.0)")

err = B.compile_blueprint(asset)
```

## Quick test (macOS/Linux)

```bash
SOCK="/Users/xartest/Documents/UnrealProjects/Loomle/Intermediate/loomle-mcp.sock"
printf '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_context","arguments":{}}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":31,"method":"tools/call","params":{"name":"editor_stream","arguments":{"action":"start"}}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"execute_python","arguments":{"code":"import unreal\\nunreal.log(\\\"hello from mcp\\\")"}}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"execute_python","arguments":{"mode":"eval","code":"1+2+3"}}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":32,"method":"tools/call","params":{"name":"editor_stream","arguments":{"action":"status"}}}\n' | nc -U "$SOCK"
printf '{"jsonrpc":"2.0","id":33,"method":"tools/call","params":{"name":"editor_stream","arguments":{"action":"stop"}}}\n' | nc -U "$SOCK"
```

## Quick test (Windows PowerShell)

```powershell
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'loomle-mcp', [System.IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(3000)
$reader = New-Object System.IO.StreamReader($pipe)
$writer = New-Object System.IO.StreamWriter($pipe)
$writer.AutoFlush = $true

$writer.WriteLine('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}')
$reader.ReadLine()

$writer.WriteLine('{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}')
$reader.ReadLine()

$writer.WriteLine('{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_context","arguments":{}}}')
$reader.ReadLine()

$writer.WriteLine('{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"execute_python","arguments":{"code":"import unreal\nunreal.log(\"hello from mcp\")"}}}')
$reader.ReadLine()

$writer.WriteLine('{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"execute_python","arguments":{"mode":"eval","code":"1+2+3"}}}')
$reader.ReadLine()
```

## Notes

- Transport differs per OS, MCP payload is identical.
- On macOS, prefer launching editor with:
  - `open -na '/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app' --args '/Users/xartest/Documents/UnrealProjects/Loomle/Loomle.uproject'`
- Avoid detached direct binary launch (`.../MacOS/UnrealEditor ... & disown`) because it may auto-terminate unexpectedly.
