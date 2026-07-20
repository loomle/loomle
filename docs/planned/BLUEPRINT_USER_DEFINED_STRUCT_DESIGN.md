# Blueprint UserDefinedStruct Design

> **Status: planned SAL migration.** The UE 5.7 source analysis and semantic
> constraints in this document remain useful, but UserDefinedStruct is not part
> of the current 0.7 SAL interface catalog or the four-tool TypeScript Client.
> The `asset_*`, `blueprint.struct.*`, Rust, and Python contracts below describe
> the retired 0.6 implementation and are retained only as migration input. They
> are not current public APIs.

## Intent

Loomle should let agents create, inspect, and edit Blueprint
`UUserDefinedStruct` assets without leaving the UE semantic model. These assets
are common Blueprint-facing data contracts for UI data, DataTables, and
Blueprint logic. The first version should cover asset creation and field-level
editing through UE's user-defined struct editor utilities, not by manually
rewriting `UScriptStruct` internals.

Issue #166 implemented a first version on the retired 0.6 direct-tool surface.
The feature still requires a new SAL interface and TypeScript Client migration
before it can be exposed in 0.7.

## UE Source Model

Relevant UE 5.7 source paths:

- `Editor/UnrealEd/Public/Kismet2/StructureEditorUtils.h`
- `Editor/UnrealEd/Private/Kismet2/StructureEditorUtils.cpp`
- `Editor/UnrealEd/Classes/UserDefinedStructure/UserDefinedStructEditorData.h`
- `Editor/UnrealEd/Private/UserDefinedStructEditorData.cpp`
- `Runtime/CoreUObject/Public/StructUtils/UserDefinedStruct.h`
- `Editor/Kismet/Private/UserDefinedStructureEditor.cpp`

The canonical editor API is `FStructureEditorUtils`:

- `CreateUserDefinedStruct`
- `CompileStructure`
- `ChangeTooltip`
- `AddVariable`
- `RemoveVariable`
- `RenameVariable`
- `ChangeVariableType`
- `ChangeVariableDefaultValue`
- `MoveVariable`
- `ChangeVariableTooltip`
- `SetMetaData`
- `GetVarDesc`
- `GetVarDescByGuid`
- `GetPropertyByGuid`
- `CanHaveAMemberVariableOfType`
- `IsStructureValid`

Important UE behavior:

- `UUserDefinedStruct` stores editor-facing field definitions in
  `UUserDefinedStructEditorData::VariablesDescriptions`.
- Each field is a `FStructVariableDescription` with `VarGuid`, `VarName`,
  `FriendlyName`, `DefaultValue`, `ToolTip`, `MetaData`, and a serialized
  `FEdGraphPinType`.
- `VarGuid` is the stable field identity. `VarName` is a generated internal
  property name containing the guid suffix. `FriendlyName` is the user-facing
  field name.
- UE creation creates a struct with a default Boolean member variable. UE also
  refuses to remove the final field.
- `OnStructureChanged` marks the struct dirty, compiles it, marks the package
  dirty, and emits the struct change notification.
- `ChangeVariableDefaultValue` validates serialized string defaults through
  K2/default-property parsing. Some object-like values are allowed by path even
  where simple K2 validation would reject them.
- Recursive struct members and unsupported pin categories are rejected by
  `CanHaveAMemberVariableOfType` and `IsStructureValid`.

Loomle should use these APIs rather than editing `EditorData` directly except
for read-only serialization.

## Legacy 0.6 Tool Boundary

Public asset tools should expose `userDefinedStruct` as an asset kind:

- `asset_create` with `kind: "userDefinedStruct"`
- `asset_inspect` with `kind: "userDefinedStruct"`
- `asset_edit` with `kind: "userDefinedStruct"` for field and tooltip edits

The bridge should use Blueprint-domain internal tools, mirroring current enum
support:

- `blueprint.struct.inspect`
- `blueprint.struct.edit`

The public `asset.*` layer remains the agent-facing entrypoint for asset kind
selection. The internal `blueprint.struct.*` tools keep the UE implementation
surface explicit and testable.

This feature should not add a generic struct graph model and should not make
`asset_edit` absorb unrelated Blueprint class, graph, Material, PCG, or Widget
editing operations.

## First-Version Scope

Supported:

- create a `UUserDefinedStruct` asset;
- inspect struct identity, status, tooltip, fields, field order, field types,
  defaults, tooltips, metadata, and validation state;
- add a field;
- remove a field when UE allows it;
- rename a field by guid or friendly name;
- change a field type;
- update a field default value;
- update struct tooltip;
- update field tooltip;
- set or remove field metadata;
- move a field relative to another field;
- compile/validate after edits through UE's struct compiler.

Out of scope for the first version:

- automatic migration of DataTables, Blueprint variables, or nodes that already
  reference the struct;
- bulk graph rewrites after type changes;
- nested editor UI operations;
- arbitrary property serialization beyond UE's accepted default-value strings;
- deleting all fields from a struct.

## Legacy 0.6 Public Schemas

### `asset_create`

Add `userDefinedStruct` to `kind`.

Additional fields for this kind:

```json
{
  "kind": "userDefinedStruct",
  "assetPath": "/Game/Data/ST_WorldSummary",
  "tooltip": "World summary shown in UI.",
  "fields": [
    {
      "name": "WorldId",
      "type": {
        "category": "name",
        "container": "none"
      },
      "defaultValue": "None"
    },
    {
      "name": "DisplayName",
      "type": {
        "category": "text",
        "container": "none"
      },
      "defaultValue": "NSLOCTEXT(\"Game\", \"WorldName\", \"World\")"
    },
    {
      "name": "Thumbnail",
      "type": {
        "category": "softobject",
        "object": "/Script/Engine.Texture2D",
        "container": "none"
      }
    }
  ]
}
```

`fields` should be required for first-version creation. Empty field arrays
should be rejected with `INVALID_ARGUMENT`, because UE creates a default member
and does not allow the final field to be removed. Requiring at least one field
avoids exposing a false "empty struct" model.

### `asset_inspect`

```json
{
  "kind": "userDefinedStruct",
  "assetPath": "/Game/Data/ST_WorldSummary"
}
```

The result should include:

```json
{
  "assetPath": "/Game/Data/ST_WorldSummary",
  "structPath": "/Game/Data/ST_WorldSummary.ST_WorldSummary",
  "name": "ST_WorldSummary",
  "isUserDefinedStruct": true,
  "guid": "8e3c...",
  "status": "UpToDate",
  "errorMessage": "",
  "tooltip": "World summary shown in UI.",
  "fields": [
    {
      "id": "field-guid",
      "name": "WorldId",
      "internalName": "WorldId_0_...",
      "type": {
        "category": "name",
        "subCategory": "",
        "object": "",
        "container": "none"
      },
      "cppType": "name",
      "defaultValue": "None",
      "tooltip": "",
      "metadata": {},
      "flags": {
        "editableOnInstance": true,
        "saveGame": false,
        "multiLineText": false,
        "makeEditWidget": false,
        "invalidMember": false
      }
    }
  ],
  "fieldCount": 1,
  "validation": {
    "ok": true,
    "code": "OK",
    "message": ""
  }
}
```

### `asset_edit`

For `kind=userDefinedStruct`, add operations:

- `setTooltip`
- `addField`
- `removeField`
- `renameField`
- `changeFieldType`
- `setFieldDefault`
- `setFieldTooltip`
- `setFieldMetadata`
- `moveField`

Each operation should use an `args` object. Field identity should prefer
`fieldId` and allow `name` only as a convenience resolver when the friendly
name is unique.

Examples:

```json
{
  "kind": "userDefinedStruct",
  "assetPath": "/Game/Data/ST_WorldSummary",
  "operation": "addField",
  "args": {
    "name": "WorldId",
    "type": {"category": "name"},
    "defaultValue": "None",
    "tooltip": "Stable world identifier."
  }
}
```

```json
{
  "kind": "userDefinedStruct",
  "assetPath": "/Game/Data/ST_WorldSummary",
  "operation": "changeFieldType",
  "args": {
    "fieldId": "field-guid",
    "type": {"category": "text"}
  }
}
```

```json
{
  "kind": "userDefinedStruct",
  "assetPath": "/Game/Data/ST_WorldSummary",
  "operation": "setFieldMetadata",
  "args": {
    "fieldId": "field-guid",
    "metadata": {
      "ClampMin": "0",
      "UIMin": "0"
    },
    "removeKeys": ["DeprecatedKey"]
  }
}
```

## Type Schema

The field type schema should map directly to `FEdGraphPinType`, using the same
shape Loomle already returns for Blueprint pins:

```json
{
  "category": "struct",
  "subCategory": "",
  "object": "/Script/CoreUObject.Vector",
  "container": "none",
  "valueType": {
    "category": "string",
    "subCategory": "",
    "object": ""
  }
}
```

Rules:

- `category` maps to `FEdGraphPinType::PinCategory`.
- `subCategory` maps to `PinSubCategory`.
- `object` maps to `PinSubCategoryObject` for struct, enum, object, class,
  interface, soft object, and soft class types.
- `container` maps to `EPinContainerType` and should accept `none`, `array`,
  `set`, and `map`.
- `valueType` is required for maps and maps to `PinValueType`.
- reference and weak-pointer field types are not supported because
  `FStructVariableDescription::SetPinType` rejects them.

Input should accept both UE-style lower-case K2 categories such as `bool`,
`name`, `text`, `struct`, `object`, `softobject`, `class`, and friendly aliases
such as `Bool`, `Name`, `Text`, `Struct`, `Object`, `SoftObject`, `Class`.
The bridge should normalize aliases before constructing `FEdGraphPinType`.

Before applying add/change operations, the bridge must call
`FStructureEditorUtils::CanHaveAMemberVariableOfType` and return its message on
failure.

## Legacy Bridge Tools

### `blueprint.struct.inspect`

Input:

```json
{
  "assetPath": "/Game/Data/ST_WorldSummary"
}
```

Behavior:

- load `UUserDefinedStruct` by normalized asset path;
- serialize `UUserDefinedStruct` identity and status;
- serialize `FStructureEditorUtils::GetVarDesc`;
- include property-backed information from `GetPropertyByGuid` when available;
- run `FStructureEditorUtils::IsStructureValid` and include validation result.

### `blueprint.struct.edit`

Input:

```json
{
  "assetPath": "/Game/Data/ST_WorldSummary",
  "operation": "addField",
  "args": {}
}
```

Behavior:

- validate lifecycle using the same editor mutation guard used by graph/tree
  mutation tools;
- load the target `UUserDefinedStruct`;
- resolve field references by `fieldId`, or by unique friendly name when
  explicitly provided;
- call the matching `FStructureEditorUtils` operation;
- return `previousRevision` and `newRevision`;
- return the inspected struct after edit;
- return per-operation diagnostics when UE rejects a type, default value, name,
  or final-field removal.

`create` is implemented as a `blueprint.struct.edit` operation for parity with
`blueprint.enum.edit`. Public `asset_create kind=userDefinedStruct` transforms
to this internal path.

## Create Semantics

UE `CreateUserDefinedStruct` creates a default Boolean field. Loomle should
hide that implementation detail from the public create contract:

1. Validate that requested `fields` has at least one field.
2. Create the struct through `FStructureEditorUtils::CreateUserDefinedStruct`.
3. Rename and retarget the default field to the first requested field.
4. Add remaining requested fields in order.
5. Apply defaults, tooltips, and metadata.
6. Compile and inspect the final struct.

The implementation retargets the default field to the first requested field.
The public contract avoids pretending UE supports empty user-defined structs.

## Revision

The revision should be deterministic over:

- struct asset path;
- `UUserDefinedStruct::Guid`;
- status and error message;
- struct tooltip;
- ordered field list;
- field ids, friendly names, internal names;
- field type objects;
- defaults, tooltips, metadata, and exposed flags.

`asset_edit` should support `expectedRevision` and return
`REVISION_CONFLICT` when the current revision differs.

## Error Responses

Use existing error conventions where possible:

- `INVALID_ARGUMENT`: missing asset path, unsupported operation, malformed
  field type, duplicate friendly name, empty `fields` on create.
- `ASSET_NOT_FOUND`: target struct does not exist.
- `ALREADY_EXISTS`: create target already exists.
- `FIELD_NOT_FOUND`: field id or friendly name could not be resolved.
- `FIELD_NAME_CONFLICT`: friendly name is not unique.
- `FIELD_TYPE_UNSUPPORTED`: `CanHaveAMemberVariableOfType` rejects the type.
- `FIELD_DEFAULT_INVALID`: `ChangeVariableDefaultValue` rejects the value.
- `STRUCT_VALIDATION_FAILED`: `IsStructureValid` fails after compile.
- `REVISION_CONFLICT`: optimistic concurrency mismatch.
- `EDITOR_SHUTTING_DOWN`, `PIE_STARTING`, `PIE_ACTIVE`, `PIE_STOPPING`: unsafe
  editor lifecycle state.

Failure results should include enough context for the next agent action:

```json
{
  "code": "FIELD_TYPE_UNSUPPORTED",
  "message": "Incorrect type for a structure member variable.",
  "assetPath": "/Game/Data/ST_WorldSummary",
  "operation": "addField",
  "field": {
    "name": "SelfRef",
    "type": {"category": "struct", "object": "/Game/Data/ST_WorldSummary.ST_WorldSummary"}
  }
}
```

## Diagnostics

Mutation results should include:

- `applied`
- `changed`
- `operation`
- `assetPath`
- `previousRevision`
- `newRevision`
- `diagnostics`
- `struct`

Diagnostics should identify UE rejection points:

- field resolution;
- type conversion to `FEdGraphPinType`;
- UE type validation;
- default value validation;
- final struct validation.

Lifecycle-blocked edits should reuse the `mutationContext` shape from graph and
tree mutation guards.

## 0.7 Migration Work

1. Define the object model, query text, patch text, and exact schema discovery
   in a dedicated SAL interface card without restoring the generic 0.6 asset
   edit envelope.
2. Reuse the existing UE-facing adapter logic where it still follows
   `FStructureEditorUtils`; remove any dependency on retired Rust or Python
   clients.
3. Add TypeScript Client and Bridge coverage for the new SAL route.
4. Add positive coverage for create, inspect, add-field, and readback.
5. Add negative coverage for duplicate names, unsupported recursive struct
   type, invalid default value, final-field removal, and revision conflict.

## Open Questions

- Whether to expose `blueprint.struct.inspect/edit` publicly in addition to
  `asset.*`. The recommended first version keeps them internal like enum.
- Whether `setFieldMetadata` should support only arbitrary key/value metadata
  first, or also named convenience flags for SaveGame, editable-on-instance,
  multi-line text, and make-edit-widget.
- Whether create should accept no fields in the future by preserving UE's
  default Boolean field. The first version should reject empty fields because
  that is clearer for agents.
- Whether map containers should be first-version writeable. Reading maps is
  straightforward from `FEdGraphPinType`; writing should only ship once value
  type parsing is tested.

## Audit Criteria

Before implementation is considered complete:

- tool schemas make `fieldId` and friendly name resolution explicit;
- type schema round-trips with UE `FEdGraphPinType`;
- create does not leak UE's default Boolean field into requested output;
- inspect returns stable field ids and user-facing names;
- edit operations use `FStructureEditorUtils`;
- errors tell the agent whether to retry with a different field id, name, type,
  default value, or editor state;
- tests cover success and UE rejection paths;
- documentation is updated with any implementation-corrected assumptions.
