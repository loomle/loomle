# PCG Graph Inspect

## Intent

`pcg.graph.inspect` is the primary read surface for PCG graph structure,
node-level pins, links, defaults, and known structured settings.

The product direction is to keep routine PCG readback inside semantic PCG
tools. `execute` is a fallback for residual gaps, not the normal companion for
every inspection.

## Current Boundary

`pcg.graph.inspect` can explain graph topology, common pins, default values, and
structured `effectiveSettings` for supported settings families. It is not yet a
complete mirror of every PCG Details-panel instance field.

When the task depends on exact values that are not surfaced in graph or node
inspect output, the caller should:

1. Use `pcg.graph.inspect` to identify the node.
2. Use `pcg.node.inspect` or catalog metadata to identify the settings class and
   expected fields.
3. Use `execute` to inspect the live settings object directly.
4. State that the value came from fallback readback.

## Selector Readback

PCG selector fields should follow UE's `FPCGAttributePropertySelector`
semantics. Loomle accepts the same compact strings that UE parses, but readback
must expose the parsed selector shape rather than forcing agents to infer it
from text.

For example, `Position.Z` is an attribute selector with name `Position` and one
accessor `Z`. It is not a point property selector unless the input uses UE's
property prefix, such as `$Position`. Structured selector readback should expose
at least:

- `kind`: `pcgSelector`
- `text`: UE's round-tripped selector string
- `selection`: `Attribute`, `Property`, or `ExtraProperty`
- `name`: the selected attribute or property name
- `domain`: the metadata domain without the leading `@`
- `attributeOrProperty`: the selected base without accessors
- `accessors`: the accessor chain after the first `.`
- `accessorPath`: the accessor chain joined with `.`
- `valid`: UE's selector validity check
- `cppType`: present when the selector is surfaced as a settings property

`UPCGFilterByAttributeSettings.TargetAttribute` is a high-value selector-backed
setting. Its `effectiveSettings.targetAttribute` should be the authoritative
place to verify filters such as `Position.Z` after mutation.

`pcg.node.inspect` is the detailed property discovery surface. In class mode,
selector-backed properties are returned with `valueKind: "pcgSelector"`,
`acceptedInput: ["string", "pcgSelector"]`, string-compatible `defaultValue`,
and structured `default` fields. In instance mode, the same property includes
the current selector object under `value`, plus `valueString`/`valueText` for
compatibility. Edits still accept compact UE strings, and `pcg.graph.edit`
`setPinDefault`/`setProperty` also accepts the structured selector object.

## Spawn Actor

`UPCGSpawnActorSettings` is a high-value structured settings target. Until its
full `effectiveSettings` coverage is complete, direct settings-object fallback
may be needed for:

- template actor identity
- template actor class identity
- spawn mode and generation options
- spawned actor property override descriptions
- data-layer settings
- HLOD settings

Agents must not infer spawned actor property override mappings from topology
alone.

## Audit Notes

- #128 is documented as a current fallback boundary.
- #130 remains the parent issue for expanding PCG graph inspect into the primary
  full-coverage node readback surface.
- #122 fixes the broader selector class by exposing UE's selector decomposition
  in graph snapshots, detailed node inspection, and structured edit inputs for
  values such as `Position.Z`.
