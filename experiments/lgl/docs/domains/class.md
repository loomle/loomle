# Class Domain

## Scope

The class domain exposes UE `UClass` Reflection and effective Class Defaults as
an agent-readable view. It covers Class identity and hierarchy, implemented
interfaces, effective Properties, effective Functions, Parameters, Reflection
Metadata, Class Default Object values, Sparse Class Data values, and navigation
back to Blueprint or native C++ sources.

Reflection declarations remain read-only. Class Defaults may be changed only
when UE provides a durable Blueprint source. The domain does not expose the CDO
as a separate object, recursively expand default subobjects or Component
Templates, inspect live instances, enumerate subclasses or implementers, or
edit native C++ source.

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
universal Reflection ids. The class domain therefore does not support
`<object>@<id>`.

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

Class orientation uses the shared `summary` primary operation:

```lgl
query actorClass
summary
```

The adapter returns the compact Class object, including effective Class
Metadata and Interfaces, followed by collection counts:

```lgl
# properties: 42 effective
# functions: 86 effective
# defaults: 18 effective
# default overrides: 4 local
```

`defaults` counts the effective Class Defaults collection, including inherited,
Sparse, and read-only Config values. `default overrides` counts only durable
local CDO and Sparse differences owned by the current Class; Config hierarchy
overrides are not included. Summary does not expand any collection.
`SuperClass` gives the exact path needed to bind and inspect the parent Class
separately.

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

Sparse Class Data Properties participate in the same effective `properties`
collection. Their `path` remains the native `FFieldPath` of the owning Sparse
`UScriptStruct`, not a fabricated Class-owned path. Storage provenance is
reported by comments when it matters; the class domain does not introduce a
Sparse Property object.

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

## Class Defaults

Class Defaults are effective values attached to the bound Class. The CDO is an
implementation container, not a public LGL object and not a stable identity:

```lgl
query doorClass
defaults ["text"]

query doorClass
default <name>
```

The collection follows UE Class Defaults semantics rather than exposing every
field in CDO memory. It includes effective top-level `CPF_Edit` Properties,
including inherited and Sparse Properties, plus visible read-only values such
as `EditConst` and Config-backed defaults. It excludes internal non-editable
state, transient and deprecated state, template-disabled Properties, inline
edit-condition toggles, hidden categories, Component Template fields, and
default-subobject internals. The `properties` collection remains the discovery
surface for Reflection fields outside this Defaults collection.

Default values use the owning Property's native UE `ExportText` form wrapped in
an LGL string. LGL does not translate the value into a second type system. The
first design addresses only top-level Properties; Struct members and container
elements are read and written as part of the complete native Property value.
It does not add nested value paths.

An exact read returns the required Class and compact Property bindings, then
the value and source comments in reading order:

```lgl
doorClass = class(
  path: "/Game/BP_Door.BP_Door_C",
  type: "/Script/Engine.BlueprintGeneratedClass"
)

health = property(
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
)

doorClass.Health = "150.000000"
# value: local override
# source: /Game/BP_Door.BP_Door
```

An inherited result names the exact parent Class:

```lgl
doorClass.Health = "100.000000"
# value: inherited from /Game/BP_DoorBase.BP_DoorBase_C
```

Sparse storage remains transparent to the query shape:

```lgl
doorClass.SomeSparseValue = "<native value text>"
# value: local override
# storage: sparse class data
# struct: /Script/Game.DoorSparseClassData
```

The adapter must read Sparse values from the bound Class. A getter may return
the parent's shared Sparse data when the current Class has no local allocation;
that inherited address must never be treated as writable current-Class state.

Config Properties return the effective value already loaded into the CDO and
their logical config coordinate:

```lgl
engineClass.NearClipPlane = "10.000000"
# value: effective config
# config: Engine, section: /Script/Engine.Engine, key: NearClipPlane
```

The effective value may combine engine, plugin, project, platform, user,
generated, command-line, or runtime config layers. Class Defaults do not infer
which physical layer should be edited. Config values are read-only through
this domain; direct config-file editing is outside this design.

`with schema` keeps the same object and value text and adds comments for the
Property's native type and Metadata, constraints, source, writability, reset
behavior, Sparse Struct when applicable, and Config coordinate when
applicable. It does not introduce a schema result object.

Enumeration without search text follows UE Class Defaults category and display
order. Ordinary CDO and Sparse values remain interleaved. Category comments may
separate groups:

```lgl
# category: Replication
doorClass.NetUpdateFrequency = "100.000000"
doorClass.MinNetUpdateFrequency = "2.000000"
```

Search covers native name, `DisplayName`, `Category`, and `ToolTip`, orders by
adapter relevance, and uses the complete Property Path as a stable tie-breaker.
Enumeration uses the complete Path only as a final deterministic tie-breaker.
Both forms use shared cursor pagination.

The only Defaults filter is an explicit condition:

```lgl
query doorClass
defaults
where overridden = true
```

An override is a durable value owned by the current Class relative to its CDO
or Sparse archetype. A Property first introduced by the current Class is local
by definition. Config Properties do not participate because their ownership
belongs to the config hierarchy rather than Class archetype serialization.

## Query

Class queries use six primary operations:

```lgl
query <class>
properties ["text"]

query <class>
property <name>

query <class>
functions ["text"]

query <class>
function <name>

query <class>
defaults ["text"]

query <class>
default <name>
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
`default` resolves the same local `FName` only within the effective Defaults
collection. Zero matches and invalid ambiguity produce diagnostics rather than
path, type, or storage inference.

The first design adds no Class-specific `order by`. Only `defaults` supports
the exact `where overridden = true` condition above. Flag filtering needs a
separately reviewed bit-flag expression rather than pretending flags are
ordinary scalar strings.

## Defaults Patch

Defaults mutation reuses the shared Patch envelope and the existing `set`
operation. The Class domain additionally defines `reset` because assigning the
parent's current value is not equivalent to resuming inheritance:

```lgl
patch doorClass
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

`set` imports the complete native value and establishes a local override on the
bound Class. `reset` restores the parent archetype value and removes the local
override so later parent changes continue to propagate. For a Property first
introduced by the current Class, reset restores UE's initialized Property
default; the Property remains local by definition.

Only Blueprint Generated Classes provide a durable ordinary or Sparse Defaults
mutation source. Native `/Script/...` Classes are read-only even though their
CDO memory is technically mutable. Config Properties are also rejected because
the Patch does not identify a physical config layer. Component Templates and
default subobjects remain outside this Patch.

Sparse mutation must call `GetOrCreateSparseClassData()` for the bound Class,
use UE Property Access change propagation, mark the generated Sparse data as
serializable, and dirty the source Blueprint. It must never write through an
inherited Sparse data pointer.

The whole Patch is preflighted before mutation. The adapter must resolve the
Class and every Property, verify membership in the Defaults collection, apply
UE template editability and `CanEditChange` rules, evaluate known edit
conditions and object-reference restrictions, parse each value into initialized
temporary storage with the owning `FProperty`, and require the parser to
consume the complete input. Any failure rejects the whole Patch.

Real application uses one UE transaction, property pre/post-change
notifications, archetype propagation, and an explicit
`FBlueprintEditorUtils::MarkBlueprintAsModified` call. `dry run` shares parse,
resolve, validation, and planning, then stops before creating Sparse data,
opening a transaction, sending notifications, or dirtying the Blueprint.

Successful application returns refreshed exact Default text for every affected
Property in Patch order:

```lgl
doorClass.Health = "100.000000"
# applied: reset
# value: inherited from /Game/BP_DoorBase.BP_DoorBase_C
```

A successful dry run returns current truth and the plan without presenting the
planned value as applied state:

```lgl
doorClass.Health = "100.000000"
# current: inherited from /Game/BP_DoorBase.BP_DoorBase_C
# planned: set "150.000000"
# valid: true
# applied: false
```

Change detection includes override state as well as value. Setting an inherited
Property to its current effective value still creates a local override and is
not a no-op. Repeating an identical local set, or resetting an already inherited
Property, is a no-op and must not dirty the Blueprint or create an empty
transaction.

Structured mutation results follow the shared Mutation Dry Run Contract. The
domain does not add a Defaults-specific result, diff, or revision syntax, and
must not expose revision controls until the Bridge enforces them.

## Normalized JSON

### Target And Requests

The Class target is its canonical UObject Path. A document-local binding is
required in LGL text, but its alias is not sent as identity:

```ts
interface ClassTarget {
  domain: "class";
  path: string;
}
```

For example, this binding uses the shared `Binding`, `LocalRef`, and `Call`
shapes:

```json
{
  "target": {"kind": "local", "name": "doorClass"},
  "value": {
    "kind": "call",
    "callee": "class",
    "args": {"path": "/Game/BP_Door.BP_Door_C"}
  }
}
```

When that binding is used by a read or Patch, normalization resolves it to:

```json
{"domain": "class", "path": "/Game/BP_Door.BP_Door_C"}
```

Summary uses the shared query envelope:

```json
{
  "kind": "query",
  "target": {"domain": "class", "path": "/Game/BP_Door.BP_Door_C"},
  "operation": {"kind": "summary"}
}
```

The seven Class operations use the shared required `Query.operation` field:

```ts
type ClassQueryOperation =
  | {kind: "summary"}
  | {kind: "properties"; text?: string}
  | {kind: "property"; name: string}
  | {kind: "functions"; text?: string}
  | {kind: "function"; name: string}
  | {kind: "defaults"; text?: string}
  | {kind: "default"; name: string};

interface ClassQuery extends Query {
  target: ClassTarget;
  operation: ClassQueryOperation;
}
```

An exact schema query normalizes without retaining the local alias:

```json
{
  "kind": "query",
  "target": {
    "domain": "class",
    "path": "/Game/BP_Door.BP_Door_C"
  },
  "operation": {"kind": "default", "name": "Health"},
  "with": ["schema"]
}
```

The confirmed override filter reuses the shared Condition shape:

```json
{
  "kind": "query",
  "target": {
    "domain": "class",
    "path": "/Game/BP_Door.BP_Door_C"
  },
  "operation": {"kind": "defaults", "text": "health"},
  "where": {
    "kind": "eq",
    "field": {"path": ["overridden"]},
    "value": true
  },
  "page": {"limit": 50}
}
```

Capability validation enforces the text contract after structural validation:
`schema` is allowed only on singular exact operations; `where` is allowed only
as `overridden = true` on `defaults`; `page` is allowed only on plural
operations; and Class operations do not accept `orderBy`. Unsupported
combinations are diagnostics, not ignored fields.

### Defaults Patch

Class Defaults reuse the shared Patch envelope. Their target path is a narrow
one-segment specialization of the shared `FieldPath`, relative to the already
resolved Class. It cannot introduce a nested value path:

```ts
interface ClassPropertyPath {
  path: [string];
}

type ClassPatchOp =
  | {kind: "set"; target: ClassPropertyPath; value: string}
  | {kind: "reset"; target: ClassPropertyPath};

interface ClassPatch extends Patch {
  target: ClassTarget;
  bindings: [];
  ops: ClassPatchOp[];
}
```

The owner alias in `set doorClass.Health` is checked against the Patch target
while parsing and then removed from the operation payload:

```json
{
  "kind": "patch",
  "target": {
    "domain": "class",
    "path": "/Game/BP_Door.BP_Door_C"
  },
  "dryRun": false,
  "bindings": [],
  "ops": [
    {
      "kind": "set",
      "target": {"path": ["Health"]},
      "value": "150.000000"
    },
    {
      "kind": "reset",
      "target": {"path": ["NetUpdateFrequency"]}
    }
  ]
}
```

`value` is always the complete native UE text string, not a translated JSON
number, boolean, Struct, container, or object. The first Class Defaults Patch
has no creation bindings, so `bindings` must be empty. The Bridge resolves each
current local Property name to its exact `FFieldPath` during preflight.

### Ordered Results

Query and mutation return the same Class object model:

```ts
interface ClassResult {
  kind: "class_result";
  statements: ClassResultStatement[];
}

interface ClassBinding {
  target: LocalRef;
  value: {
    kind: "call";
    callee: "class";
    args: ClassCallArgs;
  };
}

interface ClassCallArgs {
  path: string;
  type: string;
  [nativeField: string]: Expr;
}

interface PropertyBinding {
  target: LocalRef;
  value: {
    kind: "call";
    callee: "property";
    args: PropertyCallArgs;
  };
}

interface PropertyCallArgs {
  path: string;
  type: string;
  [nativeField: string]: Expr;
}

interface FunctionBinding {
  target: LocalRef;
  value: {
    kind: "call";
    callee: "function";
    args: FunctionCallArgs;
  };
}

interface FunctionCallArgs {
  path: string;
  type: string;
  [nativeField: string]: Expr;
}

interface DefaultValueBinding {
  target: MemberRef;
  value: string;
}

type ClassResultStatement =
  | ClassBinding
  | PropertyBinding
  | FunctionBinding
  | DefaultValueBinding
  | Comment;

interface ClassObjectResult extends Result {
  object?: ClassResult;
}

interface ClassMutationResult extends MutationResult {
  object?: ClassResult;
}
```

These closed variants are constrained uses of the shared `LocalRef`,
`MemberRef`, `Call`, `Expr`, and comment primitives:

- `ClassBinding`: local target plus `class(path: ..., nativeFields...)` Call.
- `PropertyBinding`: local target plus `property(path: ..., type: ...,
  nativeFields...)` Call.
- `FunctionBinding`: local target plus `function(path: ..., type: ...,
  nativeFields...)` Call.
- `DefaultValueBinding`: member target plus one native value string.
- `Comment`: `{kind: "comment", text: string}` containing one non-empty line
  without the `# ` prefix.

One exact Default result is therefore one ordered JSON sequence:

```json
{
  "kind": "class_result",
  "statements": [
    {
      "target": {"kind": "local", "name": "doorClass"},
      "value": {
        "kind": "call",
        "callee": "class",
        "args": {
          "path": "/Game/BP_Door.BP_Door_C",
          "type": "/Script/Engine.BlueprintGeneratedClass"
        }
      }
    },
    {
      "target": {"kind": "local", "name": "health"},
      "value": {
        "kind": "call",
        "callee": "property",
        "args": {
          "path": "/Script/Game.DoorBase:Health",
          "type": "FloatProperty"
        }
      }
    },
    {
      "target": {
        "kind": "member",
        "object": "doorClass",
        "member": "Health"
      },
      "value": "150.000000"
    },
    {"kind": "comment", "text": "value: local override"},
    {"kind": "comment", "text": "source: /Game/BP_Door.BP_Door"}
  ]
}
```

The `statements` array is the only serialized reading order. Summary count
comments, Function Parameter Properties, category comments, ordinary and
Sparse Defaults, schema comments, and Patch-order refreshed values all remain
interleaved. Class results never add parallel `classes`, `properties`,
`functions`, `defaults`, or `comments` arrays.

Every `ClassResult` is a self-contained LGL document. It contains exactly one
Class binding, which must precede any statement that uses the Class alias, and
every other referenced alias must likewise be bound earlier in the same array.
An exact `default`
result must place the matching Property binding before its Default value
binding. A plural `defaults` result may omit per-Property bindings to remain
compact, but it still begins with the required Class binding. A Patch result
uses one Class binding followed by one Property/value/comment group per
affected operation in Patch order.

Mutation responses put this same `ClassResult` in the ordinary `object` field.
The shared `MutationResult` extends the normal Result with execution fields such
as `dryRun`, `valid`, `applied`, `planned`, and `diff`; it does not wrap or
replace the Class object. Consequently the same formatter produces query,
successful mutation, no-op, and dry-run LGL text.

### Round Trip Rules

Normalized JSON round-trips to canonical LGL semantics, not original lexical
spelling. Whitespace, blank lines, the caller's local alias, and constructor
argument order are not identity. A formatter chooses deterministic aliases and
canonical field order, then uses those aliases consistently throughout the
document. Named native fields retain their exact names and values.

Statement order and comment placement are semantic and must round-trip exactly.
The formatter must walk `statements` once without regrouping. The current core
text parser discards comments. The current schema also lacks standalone
`Summary`, `ClassTarget`, `ClassResult`, `Comment`, Class query and Patch
operations, and the extended `MutationResult`; current parsers and formatters
still use the old find-centric and grouped-array models. All are explicit
implementation gaps for the later schema/parser/formatter migration.

These JSON shapes introduce no `class_result(...)`, CDO object, Default object,
Sparse object, nested value path, or other Agent-facing LGL syntax.

## Adapter And Mutation Boundary

The adapter may resolve Class, Function, and Property Paths; walk effective
inheritance; read Reflection flags and Metadata; identify CDO and Sparse
overrides; read effective Config values; and trace Blueprint-generated objects
back to authored sources.

Generated Reflection declarations and Metadata remain read-only and must not be
mutated in place. Confirmed Defaults Patch operations write the durable
Blueprint CDO or Sparse source through UE's native edit paths. Other
Blueprint-owned declaration edits belong to their authored Blueprint or Graph
objects. Native declarations and native ordinary defaults require source-code
editing and recompiling.

The current TypeScript schema, parser, formatter, fixtures, and adapters do not
implement this Class contract yet. They must migrate only after this documented
target is accepted; Bridge implementation remains a later phase.
