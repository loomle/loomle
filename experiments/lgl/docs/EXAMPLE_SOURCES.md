# Example Sources

These examples are not exact serialized Blueprint exports. They are LGL sketches
derived from real Blueprint tutorial patterns to test whether the language feels
comfortable across common graph shapes.

## Sources

- Epic Developer Community, "Setting Up Input on an Actor":
  https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-input-on-an-actor-in-unreal-engine
  Covers enable input on BeginPlay, enable/disable input with overlap events,
  and key input connected to Print String.
- Epic Developer Community, "Blueprints Quick Start Guide":
  https://dev.epicgames.com/documentation/unreal-engine/quick-start-guide-for-blueprints-visual-scripting-in-unreal-engine
  Covers a launchpad overlap flow that casts the overlapping actor and calls
  Launch Character.
- Epic Developer Community, "Using a Single Line Trace (Raycast) by Channel":
  https://dev.epicgames.com/documentation/unreal-engine/using-a-single-line-trace-raycast-by-channel-in-unreal-engine
  Covers Event Tick, camera vector math, Line Trace By Channel, hit result
  handling, and Print String.
- Epic Developer Community, "Spawn Actor from Class":
  https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Game/SpawnActorfromClass
  Documents node pins and spawn actor semantics for class, transform, owner, and
  return value.
- Unreal Community Wiki, "Blueprint Automated Door Tutorial":
  https://unrealcommunity.wiki/blueprint-automated-door-tutorial-hopfihfi
  Covers overlap events, Timeline play/reverse, interpolation, and relative
  rotation.
- Epic Developer Community tutorial, "Blueprint - Open/Close Door and Platform":
  https://dev.epicgames.com/community/learning/tutorials/X7OY/unreal-engine-blueprint-open-close-door-and-platform
  Covers door/platform patterns using overlaps, Timeline, Lerp, and Set Actor
  Location or Set Relative Rotation.

## Deliberate Simplifications

- Pin names use stable-looking canonical names such as `Exec`, `Then`,
  `ReturnValue`, and `Condition`. Real UE readback must bind these to actual
  `UEdGraphPin` names.
- Component references such as `Trigger`, `Box`, `Mesh`, and `Muzzle` are
  represented as identifiers until schema binding exists.
- Timeline is one complex Graph Node. Its Template, Tracks, and internal Curve
  Keys are nested native Node state rather than independent LGL objects.
- Complex values are built through explicit nodes such as `MakeVector` and
  `MakeRotator` because the current parser intentionally avoids nested function
  literals.
