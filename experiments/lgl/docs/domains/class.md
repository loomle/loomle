# Class Reflection Domain

## Scope

The class domain exposes UE `UClass` Reflection as an agent-readable, initially
read-only view. It covers Class identity and hierarchy, implemented interfaces,
effective Properties, effective Functions, Parameters, Reflection Metadata,
and navigation back to Blueprint or native C++ sources.

It does not expose Class Default Object values, default subobjects, live
instances, subclasses, implementers, or source-code editing. CDO and default
subobject state require a separate design.

Class Reflection is distinct from the Blueprint domain. A `UBlueprint` is an
authored source asset with a persistent `BlueprintGuid`; its generated `UClass`
is compiled Reflection state with a Class Path and no intrinsic persistent
Guid.

## UE Identity Boundary

`UClass` and `UFunction` are UObjects. Their native locators are UObject Paths:

```text
/Script/Engine.Actor
/Game/BP_Door.BP_Door_C
/Script/Engine.Actor:TakeDamage
```

Neither type owns a universal persistent Guid. `UObject::GetUniqueID()` is an
in-memory index that may be reused, and creating an `FUniqueObjectGuid` on
demand mutates object annotation state and dirties the package. Neither is a
public LGL identity.

`FProperty` is an `FField`, not a UObject. Its native locator is an
owner-qualified `FFieldPath`:

```text
/Script/Engine.Actor:RootComponent
/Script/Engine.Actor:TakeDamage:BaseDamage
```

Blueprint-authored Properties and Functions may also be traceable to a
`VarGuid`, `GraphGuid`, or `NodeGuid`. Those are source provenance, not
universal Reflection ids. The class domain therefore does not support `find
@id`.

## Class Object

A Class must be explicitly bound before querying. A bare Path does not cause
the language or adapter to infer a Class target:

```lgl
actorClass = class(path: "/Script/Engine.Actor")
doorClass = class(path: "/Game/BP_Door.BP_Door_C")
```

Complete Class text uses the native Class Path and meaningful Reflection state:

```lgl
actorClass = class(
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
)
```

`type` is the actual Class object's native UE class path. A Blueprint Generated
Class therefore uses `/Script/Engine.BlueprintGeneratedClass` and may include
the editor-only source relation:

```lgl
doorClass = class(
  path: "/Game/BP_Door.BP_Door_C",
  type: "/Script/Engine.BlueprintGeneratedClass",
  SuperClass: "/Script/Engine.Actor",
  ClassGeneratedBy: "/Game/BP_Door.BP_Door",
  ClassFlags: "<EClassFlags native text>"
)
```

`ClassUnique`, `ClassCastFlags`, Reflection linked lists, function maps,
replication caches, layout state, and the CDO pointer are implementation state
and do not appear as authored Class fields.

## Summary

Class orientation uses the shared summary statement:

```lgl
summary actorClass
```

The adapter returns the compact Class object, including effective Class
Metadata and Interfaces, followed by collection counts:

```lgl
# properties: 42 effective
# functions: 86 effective
```

Summary does not expand Property or Function collections. `SuperClass` gives
the exact path needed to bind and inspect the parent Class separately.

## Property Objects

Property collection results use compact `property(...)` text:

```lgl
rootComponent = property(
  path: "/Script/Engine.Actor:RootComponent",
  type: "ObjectProperty(/Script/Engine.SceneComponent)"
)
```

The binding comes from the current `FName`; it is a local text alias rather than
identity. `path` is the complete `FFieldPath`. `type` preserves the native UE
Property type expression without an LGL-specific type system:

```lgl
type: "FloatProperty"
type: "EnumProperty(EWeaponType, ByteProperty)"
type: "ArrayProperty(ObjectProperty(/Script/Engine.Actor))"
```

Because the native type text already carries Enum, underlying, element, key,
value, Class, and Struct relations, exact Property text does not repeat them as
parallel translated fields.

An exact Property read adds meaningful native state and effective Metadata:

```lgl
damage = property(
  path: "/Script/Game.DamageComponent:Damage",
  type: "FloatProperty",
  PropertyFlags: "<EPropertyFlags native text>",
  MetaData: {
    Category: "Combat",
    ClampMax: "1000.0",
    ClampMin: "0.0",
    Units: "hp"
  }
)
```

Relevant non-default state may include `ArrayDim`, `PropertyFlags`,
`RepNotifyFunc`, and `BlueprintReplicationCondition`. Memory layout and link
state such as `ElementSize`, `Offset_Internal`, `IndexInOwner`, `RepIndex`, and
Property link pointers are excluded.

## Function Objects

Function collection results use compact `function(...)` text:

```lgl
takeDamage = function(
  path: "/Script/Engine.Actor:TakeDamage",
  type: "/Script/CoreUObject.Function"
)
```

An exact Function read adds `FunctionFlags`, effective Metadata, and the
Function's Parameter Properties. The result is one ordered document rather
than a Function object containing a parameter array:

```lgl
takeDamage = function(
  path: "/Script/Engine.Actor:TakeDamage",
  type: "/Script/CoreUObject.Function",
  FunctionFlags: "<EFunctionFlags native text>",
  MetaData: {Category: "Game|Damage"}
)

damagedActor = property(
  path: "/Script/Engine.Actor:TakeDamage:DamagedActor",
  type: "ObjectProperty(/Script/Engine.Actor)",
  PropertyFlags: "CPF_Parm"
)

baseDamage = property(
  path: "/Script/Engine.Actor:TakeDamage:BaseDamage",
  type: "FloatProperty",
  PropertyFlags: "CPF_Parm"
)

returnValue = property(
  path: "/Script/Engine.Actor:TakeDamage:ReturnValue",
  type: "FloatProperty",
  PropertyFlags: "CPF_Parm | CPF_OutParm | CPF_ReturnParm"
)
```

Parameters and return values are ordinary `FProperty` objects. Native flags
such as `CPF_Parm`, `CPF_OutParm`, `CPF_ReturnParm`, `CPF_ReferenceParm`, and
`CPF_ConstParm` preserve their meaning without a new Parameter object or
direction field. Statement order follows the UE Function declaration order.

Applicable non-default `RPCId` and `RPCResponseId` may be returned as native
Function fields; they are network service/response numbers, never LGL object
ids. `NumParms`, `ParmsSize`, `ReturnValueOffset`, native function pointers,
event-graph offsets, and internal caches are excluded as derived implementation
state.

An override comment identifies the parent Function Path. Blueprint-backed
Functions also receive navigation comments for the source Graph and entry or
event Node when those objects can be resolved.

## Metadata

`MetaData` is an ordinary inline map on exact Class, Property, and Function
text. It does not introduce a Metadata object or query operation. Values remain
the exact UE strings, including `"true"` and meaningful empty strings. Keys are
formatted in stable lexical order because UE's Metadata map has no semantic
iteration order.

Collection results omit Metadata to stay compact. Exact reads include all
effective Metadata because keys such as `WorldContext`, `DeterminesOutputType`,
`ClampMin`, `AllowedClasses`, `DisplayName`, and `ToolTip` contain behavior and
constraints not recoverable from type or flags.

Effective Reflection Metadata may combine locally authored, inherited, and
compiler-generated values. `with schema` comments identify source and
writability per key rather than declaring the whole map writable:

```lgl
query doorClass
property Health
with schema
```

For a Blueprint Variable, source fields may include
`FBPVariableDescription::FriendlyName`, `Category`, or `MetaDataArray`. For a
Blueprint Function, source fields normally belong to
`UK2Node_FunctionEntry::MetaData`; Custom Events use their corresponding source
Node. Native Metadata points back to its C++ declaration. Generated Reflection
objects remain read-only: edits must target the real Blueprint or C++ source so
they survive recompilation.

## Query

Class queries use four primary operations:

```lgl
query <class>
properties ["text"]

query <class>
property <name>

query <class>
functions ["text"]

query <class>
function <name>
```

`properties` and `functions` return the effective view of the bound Class,
including inherited objects. Callers do not request a separate `with inherited`
expansion. A derived override replaces the hidden parent Function in the
effective collection. Every result retains its actual declaring-owner Path; to
inspect a hidden parent version, bind the parent Class explicitly.

Plural operations enumerate without search text and search with it. Property
search covers native and authored names plus relevant `DisplayName`, `Category`,
and `ToolTip` Metadata. Function search additionally covers `Keywords`.
Enumeration walks from the bound Class through its parents while preserving UE
Reflection order within each declaring Class. Search uses adapter relevance,
then actual Path as a stable tie-breaker. Cursor pagination uses the shared
`page` clauses.

Singular operations resolve the current local `FName` through UE Class
Reflection. `function TakeDamage` follows the effective Class, interface, and
superclass resolution behavior; `property RootComponent` similarly accepts an
inherited Property. The returned object's full Path records the actual owner.
Zero matches and invalid ambiguity produce diagnostics rather than path or type
inference.

The first design does not add Class-specific `where` or `order by` behavior.
Flag filtering needs a separately reviewed bit-flag expression rather than
pretending flags are ordinary scalar strings.

## Adapter And Mutation Boundary

The adapter may resolve Class, Function, and Property Paths; walk effective
inheritance; read Reflection flags and Metadata; identify overrides; and trace
Blueprint-generated objects back to authored sources.

The class domain is read-only in this phase. It must not mutate Generated
Classes, generated Properties, generated Functions, CDOs, or Reflection
Metadata in place. Blueprint-owned edits belong to their authored Blueprint or
Graph objects. Native declarations require source-code editing and recompiling.

The normalized JSON representation of Class, Property, Function, and Class
query operations is intentionally not specified yet. It must be reviewed before
schema or bridge work; this text design does not silently introduce normalized
operation kinds.
