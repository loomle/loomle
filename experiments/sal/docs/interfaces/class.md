# class

Inspect UE Class Reflection and effective Class Defaults. Reflection remains
read-only; Blueprint Generated Class Defaults may be edited through their
durable Blueprint source. This interface assumes the resident SAL Core guide.

## Target

Bind the exact native Class Path:

```sal
actorClass = class(path: "/Script/Engine.Actor")
doorClass = class(path: "/Game/BP_Door.BP_Door_C")
```

The Path is the complete locator. Classes, Functions, and Properties have no
public typed stable id in this interface. Class has no Palette.

## Query

Every Query starts with `query doorClass` and chooses exactly one primary
operation from this surface:

```sal
summary
properties ["text"]
property <name>
functions ["text"]
function <name>
defaults ["text"]
default <name>
```

`summary` returns the compact Class, effective Metadata and Interfaces, its
`SuperClass`, and counts for effective Properties, Functions, Defaults, and
local Default overrides.

Plural operations return the effective Class view, including inherited
objects, and use cursor pagination with a default limit of 50. No
`with inherited` or Class-specific `order by` exists. Bind `SuperClass`
explicitly to inspect a hidden parent declaration. Only `defaults` accepts:

```sal
where overridden = true
```

Enumeration preserves each operation's UE order. Search covers native and
authored names plus relevant display Metadata, then orders by adapter relevance.

Singular operations resolve one exact effective local name. `property` and
`function` return the real declaring-owner Path. `default` resolves only within
the effective Defaults collection. Exact Property, Function, and Default reads
may use `with schema`; collections and `summary` may not.

```sal
query doorClass
property Health
with schema
```

An exact Function is followed by its Parameter and return-value Properties in
UE declaration order. Native `CPF_Parm`, `CPF_OutParm`, and `CPF_ReturnParm`
flags preserve their meaning; Class does not introduce a Parameter object.
Returned Class, Property, and Function state uses native fields and effective
`MetaData` maps.

## Defaults

An exact Default returns its compact Property before the value:

```sal
doorClass = class(path: "/Game/BP_Door.BP_Door_C")

health = property(
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
)

doorClass.Health = "150.000000"
# value: local override
# source: /Game/BP_Door.BP_Door
```

Default values are complete native UE `ExportText` strings. A native fixed
array (`ArrayDim > 1`) uses the existing SAL array Expr with exactly one native
string per fixed element; a dynamic `FArrayProperty` remains one native string.
Ordinary, inherited, Sparse, and read-only Config values share this surface.
Source and storage are comments, not new objects. The interface exposes
top-level Properties only; Struct members and dynamic-container elements remain
part of the complete native value.

## Patch

Only Blueprint Generated Classes with durable source ownership may edit
ordinary or Sparse Class Defaults:

```sal
patch doorClass [dry run]
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

`set` imports the complete native value and establishes a local override.
`reset` removes that override and resumes inheritance, or restores the
initialized default of a Property introduced by the current Class. Exact
Default `with schema` is authoritative for writability, constraints, source,
reset behavior, and Config or Sparse provenance.

For `ArrayDim == 1`, `set` accepts one native string. For a native fixed array,
it accepts exactly `ArrayDim` native strings in an SAL array and imports every
element in index order. UE 5.7 cannot durably serialize an explicit Blueprint
CDO override equal to its inherited value, so that `set` is rejected; use
`reset` to inherit or set a distinct value. Loomle does not enable UE's
experimental `FOverridableManager`.

Native `/Script/...` Classes, Config values, Component Templates, default
subobjects, Reflection declarations, and Metadata are read-only. Defaults Patch
does not accept nested value paths, creation bindings, Palette, or `invoke`.

## Save And Handoffs

Save a changed Blueprint Generated Class through a separate Core terminal
request:

```sal
patch doorClass
save
```

`save` resolves `ClassGeneratedBy` and persists only that source Blueprint's
dirty Package. Class defines no `compile`; compile the exact source through
`blueprint`. `ClassGeneratedBy` navigates to Blueprint, `SuperClass` to the
parent Class, Blueprint source comments to Blueprint or Graph, and native
source comments to C++.
