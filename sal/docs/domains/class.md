# Class Domain

## Scope

Class Domain exposes UE `UClass` Reflection and effective Class Defaults. It
covers:

- Class identity and hierarchy;
- implemented interfaces;
- effective Properties and Functions;
- Function Parameter Properties;
- Reflection Metadata;
- CDO, Sparse Class Data, and Config-backed effective values;
- navigation to Blueprint or native C++ sources.

Reflection declarations remain read-only. Supported Class Defaults may be
changed only when UE provides durable Blueprint source ownership.

Class Domain does not expose live instances, recursive default subobjects,
Component Templates, subclass enumeration, implementer search, or C++ editing.

## Native Identity

The Target is the exact native Class Path:

```sal
actorClass = target {
  domain: class,
  path: "/Script/Engine.Actor"
}

doorClass = target {
  domain: class,
  path: "/Game/BP_Door.BP_Door_C"
}
```

`UClass` and `UFunction` own UObject Paths; `FProperty` owns an
owner-qualified `FFieldPath`. They do not own a universal persistent Guid.
`UObject::GetUniqueID()` is process-local, and creating an
`FUniqueObjectGuid` on demand would mutate state.

Class Domain therefore exposes no contained StableRef in the current contract.
Function and Property names are exact scoped Query selectors; returned native
Paths are ordinary navigation data.

## Class Object Data

```sal
{
  path: "/Script/Engine.Actor",
  type: "/Script/CoreUObject.Class",
  SuperClass: "/Script/CoreUObject.Object",
  ClassConfigName: "Engine",
  ClassFlags: "<EClassFlags native text>",
  MetaData: {
    BlueprintType: "true",
    ModuleRelativePath: "Classes/GameFramework/Actor.h"
  },
  Interfaces: [
    {
      Class: "/Script/Engine.NavAgentInterface",
      PointerOffset: 0,
      bImplementedByK2: false
    }
  ]
}
```

Blueprint Generated Classes may include:

```sal
{
  path: "/Game/BP_Door.BP_Door_C",
  type: "/Script/Engine.BlueprintGeneratedClass",
  SuperClass: "/Script/Engine.Actor",
  ClassGeneratedBy: "/Game/BP_Door.BP_Door",
  ClassFlags: "<EClassFlags native text>"
}
```

`ClassUnique`, reflection linked lists, caches, memory layout, and CDO pointer
are implementation state and are omitted.

## Query

```sal
summary
properties ["text"]
property <name>
functions ["text"]
function <name>
defaults ["text"]
default <name>
```

`summary` returns compact Class data and counts for effective Properties,
Functions, Defaults, and local Default overrides.

Plural operations include inherited effective state and use cursor pagination.
Enumeration preserves UE order within each declaring owner. Search covers
native/authored names and relevant effective Metadata. The returned `path`
always records the actual declaring owner.

Only `defaults` accepts:

```sal
where overridden = true
```

Class defines no Domain-specific `order by`. Exact Property, Function, and
Default reads may append `with schema`; summary and collections may not.

## Property And Function Data

```sal
health = {
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty",
  PropertyFlags: "<EPropertyFlags native text>",
  MetaData: {
    Category: "Stats",
    ClampMin: "0.0"
  }
}
```

Native Property type text carries Enum, element, key, value, Class, and Struct
relationships without a SAL type system. Meaningful fields such as `ArrayDim`,
`PropertyFlags`, `RepNotifyFunc`, and Blueprint replication condition remain
native.

An exact Function is followed by its Parameter and return-value Properties in
native declaration order:

```sal
takeDamage = {
  path: "/Script/Engine.Actor:TakeDamage",
  type: "/Script/CoreUObject.Function",
  FunctionFlags: "<EFunctionFlags native text>",
  MetaData: { Category: "Game|Damage" }
}

baseDamage = {
  path: "/Script/Engine.Actor:TakeDamage:BaseDamage",
  type: "FloatProperty",
  PropertyFlags: "CPF_Parm"
}
```

Parameters are ordinary `FProperty` data. Native flags preserve input, output,
reference, const, and return semantics; Class does not invent a Parameter
object.

Metadata remains an ordinary map. Exact schema reports source and writability
per key; generated Reflection is not mutated in place.

## Class Defaults

Defaults expose effective top-level editable Properties plus visible read-only
Config and Sparse values. They exclude internal, transient, deprecated,
template-disabled, Component Template, and default-subobject state.

Values use complete native UE `ExportText` strings:

```sal
result exact_target
target doorClass = target {
  domain: class,
  path: "/Game/BP_Door.BP_Door_C"
}
objects
health = {
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
}
doorClass.Health = "150.000000"
# value: local override
# source: /Game/BP_Door.BP_Door
```

A native fixed array (`ArrayDim > 1`) is an SAL array with one native string
per fixed element. A dynamic `FArrayProperty` remains one complete string.
Struct members and container elements stay inside the native value; the first
contract has no nested Default value path.

Inherited and Sparse examples remain the same binding shape with comments:

```sal
doorClass.Health = "100.000000"
# value: inherited from /Game/BP_DoorBase.BP_DoorBase_C

doorClass.SomeSparseValue = "<native value text>"
# value: local override
# storage: sparse class data
# struct: /Script/Game.DoorSparseClassData
```

Config values are effective read-only state:

```sal
engineClass.NearClipPlane = "10.000000"
# value: effective config
# config: Engine, section: /Script/Engine.Engine, key: NearClipPlane
```

Class Domain does not choose which physical config layer to edit.

Property names outside member-path grammar remain readable through quoted
object data and exact-name comments but are not patchable until a lossless
member-path form exists.

## Defaults Patch

Only Blueprint Generated Classes with durable source ownership may patch
ordinary or Sparse Defaults:

```sal
patch doorClass [dry run]
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

`set` imports the entire native value and establishes a local override.
`reset` removes the override and resumes inheritance, or restores the
initialized default of a Property introduced by this Class.

Preflight:

1. resolves the exact Class and Property;
2. verifies membership in the Defaults surface;
3. applies template editability and `CanEditChange`;
4. evaluates known edit conditions and object restrictions;
5. parses every value into initialized temporary native storage;
6. requires complete input consumption;
7. plans CDO or Sparse ownership and archetype propagation.

Sparse mutation must allocate data for the bound Class and never write through
an inherited Sparse pointer.

Live apply uses one transaction, property notifications, archetype
propagation, Blueprint modification, and Package dirtying. Dry run shares the
same resolve/validate/plan path and stops before Sparse allocation,
transactions, notifications, or dirtying.

Every changing live Patch requires an available top-level editor transaction
and verifies that its scoped transaction is outstanding before CDO mutation or
Sparse allocation. Otherwise it returns
`capability.transaction_unavailable` with `applied: false`; it must not fall
back to an untracked memory write. Dry run and an all-no-op live Patch do not
require a transaction.

Successful live readback exports refreshed exact current Default text once for
each final affected Property. When one Property appears several times, its
single output group is positioned by that Property's last Patch operation;
the structured plan still preserves every input operation in original order.
This makes final Object Text deterministic without hiding authored order:

```sal
doorClass.Health = "100.000000"
# applied: reset
# value: inherited from /Game/BP_DoorBase.BP_DoorBase_C
```

A successful dry run reports current native truth plus the proposed plan. It
does not substitute the planned value into Object Text or imply a local
override was created:

```sal
doorClass.Health = "100.000000"
# current: inherited from /Game/BP_DoorBase.BP_DoorBase_C
# planned: set "150.000000"
# valid: true
# applied: false
```

UE 5.7 cannot durably serialize an explicit Blueprint CDO override equal to
its inherited value. Such a `set` is rejected; use `reset` to inherit or set a
distinct value. Repeating an identical local value and resetting an already
inherited value are no-ops.

Native Classes, Config values, Component Templates, default subobjects,
Reflection declarations, and Metadata are read-only. Defaults Patch has no
creation, Palette, nested value mutation, or `invoke`.

## Save And Compile Handoff

```sal
patch doorClass
save
```

Class save resolves `ClassGeneratedBy` and persists only that source
Blueprint's Package. A native or transient Class without durable source cannot
save.

Class defines no `compile`. When compilation is needed, result navigation
returns an independent Blueprint Target:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related sourceBlueprint = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
handoff compile to sourceBlueprint
```

## Result And Adapter Boundary

Results use the shared explicit Target table and ordered Object Text. Class,
Property, and Function data are ObjectExpr; none is a Target or StableRef.

The Class adapter owns:

- native Class/Function/Property Path resolution;
- effective inheritance and Metadata;
- CDO, Sparse, Config, and override provenance;
- native import/export and Defaults validation;
- durable Blueprint ownership;
- save and Blueprint handoff.

Core never infers Class Domain from a `/Script/...` string or an object's
native `type`.
