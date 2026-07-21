# Blueprint UserDefinedStruct

## Status

`UUserDefinedStruct` is not part of the current Loomle interface catalog. Asset
queries can discover its path, but SAL has no dedicated summary, field query,
Palette, or Patch surface for it yet. This document records UE 5.7 facts and the
questions that must be resolved before that surface is designed.

## Intent

Agents should eventually be able to inspect and edit Blueprint-authored struct
definitions without bypassing UE's compiler or inventing a parallel type
system. The first useful boundary is the struct definition itself: identity,
ordered fields, native field types, defaults, tooltips, metadata, validation,
and field lifecycle.

Automatic repair of every DataTable, Blueprint variable, and graph node that
uses a changed struct is a separate reference-migration problem.

## UE 5.7 Source Facts

Relevant source:

- `Editor/UnrealEd/Public/Kismet2/StructureEditorUtils.h`
- `Editor/UnrealEd/Private/Kismet2/StructureEditorUtils.cpp`
- `Editor/UnrealEd/Classes/UserDefinedStructure/UserDefinedStructEditorData.h`
- `Editor/UnrealEd/Private/UserDefinedStructEditorData.cpp`
- `Runtime/CoreUObject/Public/StructUtils/UserDefinedStruct.h`
- `Editor/Kismet/Private/UserDefinedStructureEditor.cpp`

`FStructureEditorUtils` is the editor mutation boundary. It provides creation,
compilation, tooltip changes, field add/remove/rename/retype/reorder, default
value changes, field metadata, and validation helpers. Loomle should call these
APIs rather than mutating editor data directly.

The authored field list is
`UUserDefinedStructEditorData::VariablesDescriptions`. Each
`FStructVariableDescription` carries:

- `VarGuid`, the stable authored field identity;
- `VarName`, UE's generated internal property name;
- `FriendlyName`, the user-facing name;
- `FEdGraphPinType`, the native Blueprint type description;
- serialized default value, tooltip, metadata, and field flags.

Additional constraints:

- creating a struct creates a default Boolean field;
- UE refuses to remove the final field;
- `OnStructureChanged` marks the asset dirty, compiles it, and emits the struct
  change notification;
- `ChangeVariableDefaultValue` validates UE's serialized default text;
- `CanHaveAMemberVariableOfType` and `IsStructureValid` reject recursive or
  otherwise unsupported definitions;
- `GetVarDescByGuid` and `GetPropertyByGuid` connect authored field identity to
  the compiled property when one exists.

Read-only serialization may inspect the editor data, but mutation and final
validation must stay on the native editor path.

## Current SAL Boundary

The Asset interface can locate the package and hand off its exact path. It does
not currently understand a struct definition as an exact SAL target. The
future interface must use SAL Object Text and UE's native type expression.

When designed, the interface must keep UE-native information intact:

- the struct's exact asset/object path and native Guid;
- fields in authored order, addressed by `VarGuid`;
- distinct internal and friendly names;
- native `FEdGraphPinType` text rather than Loomle type aliases;
- native default strings and metadata keys;
- compile status and validation diagnostics adjacent to the affected objects.

## Initial Capability Boundary

A first implementation should cover:

- exact struct summary and ordered field discovery;
- exact field reads with dynamic schema;
- Palette-backed struct creation;
- field add, remove, rename, type change, default change, tooltip and metadata
  edits, and authored reordering where UE supports them;
- struct compilation, validation, and package save;
- dry run through the same native resolve, validate, and plan path as apply.

It should not promise automatic consumer migration, bulk graph rewrites,
arbitrary property serialization, or an empty struct state that UE does not
support.

## Design Questions

The SAL design must be discussed before implementation:

1. Is an exact struct a specialization of the Asset interface or a separate
   static interface card reached from Asset discovery?
2. What neutral typed reference names the field while preserving `VarGuid`
   without introducing a Loomle business type?
3. How does the creation Palette describe UE's mandatory initial Boolean field
   without leaking a transient implementation detail into the final result?
4. Which edits are ordinary `set`, `reset`, `move`, and `remove`, and which
   native compound changes must appear as schema-discovered `invoke`
   operations?
5. Does `compile` belong to the exact struct target, and how are compile and
   save sequenced after authored changes?
6. How is a safe dry run implemented when UE compilation and change
   notification are part of the normal mutation path?
7. Which reference queries are required before a type or field removal can be
   judged safe?

## Acceptance Requirements

Before this capability is public:

- every writable path must use `FStructureEditorUtils` and report its native
  rejection reason;
- field ids and native types must round-trip without translation loss;
- create must reconcile UE's initial field with the requested authored result;
- final-field removal, recursive types, invalid defaults, and stale identity
  must fail before durable mutation;
- results must use ordinary ordered SAL Object Text and the shared mutation
  result contract;
- tests must cover creation, exact readback, each mutation family, compilation,
  and the important UE rejection paths.
