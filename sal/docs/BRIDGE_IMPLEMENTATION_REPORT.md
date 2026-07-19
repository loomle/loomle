# SAL Bridge Implementation Report

## Result

Loomle Bridge now implements the normalized SAL executor contract for UE 5.7.
The SAL executor RPC surface is `sal.query` and `sal.patch`; the separate
read-only discovery RPC is `editor.context`. Static `sal.schema` text remains
Client-side. The former LGL runtime, grouped result models, and `lgl.*` RPC
methods have been removed.

SAL Text remains entirely SDK-owned:

```txt
SAL Text
  -> TypeScript parse and normalization
  -> normalized JSON
  -> UE-backed Bridge execution
  -> ordered Object Text JSON
  -> TypeScript formatting
```

The C++ Bridge does not introduce a second SAL parser or any new public syntax.

## Implemented Surface

| Interface | Query | Patch |
| --- | --- | --- |
| Asset | Asset Registry collection and exact reads, filters, ordering, paging, tags, schema | Save |
| Blueprint | Summary, structure collections, exact objects, inheritance, Palette, schema | Settings, variables, dispatchers, graphs, components, interfaces, function overrides, compile, save |
| Class | Summary, properties, functions, metadata, inherited views, schema | Native property defaults, fixed arrays, sparse class data, metadata, set/reset |
| Graph | Summary, nodes, exact Node/Pin, exec/data flow, context, Palette, schema | Palette creation, connect/disconnect/break, add/insert/remove/move/set/reset, invoke, dynamic Pins, Timeline tracks and keys |
| Widget | Combined summary and Palette, tree, widgets, exact Widget, schema | Add/move/remove/wrap/replace/rename/duplicate, Widget and Slot fields, event creation |
| References | Exact declaration resolution; local Blueprint/Graph/Widget authored use-sites; incremental project scan, cursor paging, and completeness diagnostics | Read-only |
| Editor Context | Exact current surface, owner, and at most one selected target as ordinary Object Text | Read-only |

All Query and Patch readback uses the same ordered `ObjectText.statements`
model. Patch adds the shared mutation envelope, ordered plan, resolved
references, diff, and diagnostics.

## Native UE Mapping

The implementation follows UE 5.7 source ownership rather than maintaining a
parallel semantic model. The main native paths reviewed and used include:

- Asset Registry and package save APIs;
- `FBlueprintEditorUtils`, Blueprint compilation, compiler diagnostics, SCS,
  Subobject Data, and loaded Blueprint hierarchy traversal;
- `FBlueprintAssetHandler`, `GetAllAssets(false)`, `FMemberReference`, and
  `FGraphReference` for exact Blueprint container and declaration identity;
- `UEdGraphSchema_K2`, K2 Action Menu spawners, Graph utilities, dynamic K2
  Nodes, Timeline Node/Template and curve APIs;
- Reflection property import/export, metadata, inherited Class Defaults, sparse
  class data, and UE transactions;
- `UWidgetBlueprint`, `UWidgetTree`, Widget Blueprint editor utilities, Panel
  Slots, Named Slots, navigation/reference maintenance, generated classes,
  animations, extensions, and delegate Graph integration;
- native Reference extractors for SCS Components, RepNotify functions, Widget
  bindings and source paths, Component Bound Events, Create Delegate, Macro
  instances, and Class Defaults.

Constructors, types, property names, paths, enum values, and native value text
remain UE-native. SAL supplies structure, identity, references, operations, and
ordered readable text; it does not rename UE's type system.

## Decisions Made During Implementation

No public SAL syntax was added. Implementation evidence did require the
following contract clarifications and SDK hardening:

1. A complete returned binding may be copied back as a Target. The resolver
   consumes only the constructor's locator fields and ignores descriptive
   readback state.
2. Blueprint mutation requires the exact Asset Path plus BlueprintGuid. IDs for
   Graphs, Nodes, Pins, Widgets, and other members remain owner-scoped.
3. Query results and mutation results are distinct normalized envelopes;
   `sal.query` cannot return mutation state, and `sal.patch` must return it.
4. A composed target selects exactly one interface-owned Patch planner. The
   Bridge rejects mixed Blueprint/Widget authored edits instead of splitting
   one request into partially committed mutations.
5. Dry run executes the same resolve, validation, provisional-state, and plan
   path as apply. Graph, Blueprint, and Widget preflight use isolated transient
   native object graphs; live apply uses one transaction and reports rollback
   failure truthfully.
6. Palette cursors are opaque and bound to their Target, operation, filters,
   detail, ordering, and interface source.
7. C++ identifier handling now exactly matches the SDK's ASCII grammar. UE
   names outside that grammar remain lossless strings instead of becoming
   invalid SAL names.
8. Stable reference tokens reject whitespace and dot characters. If a future
   UE identity requires punctuation, quoted stable IDs should be designed
   explicitly rather than silently extending the grammar.
9. Fixed native arrays are returned as one SAL array of UE-native value text.
   This preserves the Reflection property's real shape without inventing index
   objects.
10. A Class default explicitly set equal to its inherited value is rejected
    when UE 5.7 cannot preserve that distinction durably. The Bridge does not
    claim an override that UE would later collapse.
11. Reflected UE fields whose native names cannot be SAL member paths, or
    collide with structural fields, are never renamed. Query and schema retain
    their exact native name/value in Comments and Patch reports them
    unavailable.
12. Editor Context resolves ownership from exact tabs and native editor APIs.
    It does not infer a world-centric Asset Editor from the Level Editor's
    shared TabManager, and it revalidates recorded editor pointers before use.
13. Reference queries preserve canonical UE declaration identity internally
    and return existing authored objects through ordinary Object Text. Project
    scans freeze their roots, Blueprint handler set, container snapshot, and
    authored generation behind the shared cursor; unsupported or unresolved
    native facts fail explicitly instead of producing a false complete zero.
14. Embedded Blueprints remain addressed through their top-level asset
    container plus Blueprint GUID. This lets the same Target and readback model
    cover ordinary Blueprint assets, World Level Scripts, and other registered
    Blueprint-bearing containers without adding public syntax.

## Safety and Failure Semantics

- Normalized requests are structurally validated again at the RPC boundary.
- Target identity is verified before interface dispatch.
- Exact schema discovery is produced from the same native constraints used by
  execution.
- Patch statements are resolved and planned in authored order.
- Existing objects use stable typed references; provisional objects use only
  request-local aliases.
- Apply paths use UE transactions, notifications, reconstruction, propagation,
  dirtying, and readback appropriate to their native owner.
- A successful rollback restores the prior dirty state and reports
  `applied=false`. A failed rollback reports the possible live mutation with
  `applied=true` and keeps the asset dirty.
- Every emitted diagnostic code is registered in the shared catalog.

## Verification

The final verification for this implementation consists of:

- generated TypeScript contract consistency;
- JSON Schema valid/invalid fixtures;
- diagnostic catalog/source consistency;
- parser and formatter round trips;
- examples and SDK facade behavior;
- generic, Asset, Blueprint, and Widget memory-executor contract tests;
- a clean UE 5.7 `BuildPlugin` build and package for Mac `arm64`.

The installed UE 5.7 distribution used for verification contains arm64-only
Editor dylibs. A default `arm64+x64` BuildPlugin pass compiled every SAL source
for both architectures and linked the arm64 plugin, but x64 cannot link against
an Engine distribution whose Core, Engine, and editor dylibs have no x64
slice. The final package therefore uses the distribution's supported
`-Architecture_Mac=arm64` target. A universal package still requires a UE 5.7
installation that includes x64 Engine binaries.

Per the requested scope, no live Unreal integration test was run.

The 0.7 Client and Editor Context increment was additionally verified on
2026-07-18 with the complete SAL TypeScript test suite, all 28 Client unit
tests, `git diff --check`, and an incremental UE 5.7 Editor module build that
compiled and linked `UnrealEditor-LoomleBridge.dylib` successfully. This is a
compile and contract audit, not a live Editor interaction test.

The factual Reference Query increment was verified on 2026-07-19 with the
complete SAL TypeScript suite, all 32 Client unit tests, all five generated
interface documents, and a clean UE 5.7 Mac arm64 `BuildPlugin` run that
compiled and linked the full Unity build. No plugin was installed or replaced,
and no live Editor integration test was run during this increment.

## Deliberate Boundaries

These are explicit current boundaries rather than hidden simulated support:

- Graph's implemented native backend is Blueprint K2. Material, PCG, and other
  Graph families can compose the same SAL core through their own backends.
- Compile dry run validates the request and target but cannot predict actual
  compiler diagnostics; save dry run cannot predict source-control or
  filesystem failure.
- Class schema safely rejects complex private Details-panel `EditCondition`
  expressions it cannot evaluate. Custom Details visibility/sorting is not
  reproduced as a second UI model.
- Widget Animation, Widget Navigation, legacy property binding, and MVVM are
  not direct SAL authoring surfaces. Widget lifecycle operations still inspect
  and maintain the native references they can affect.
- Some cursor fingerprints serialize normalized fields in their stable SDK
  order. A future arbitrary third-party normalized JSON producer should use a
  canonical field-order-independent fingerprint.

The next phase is live integration testing against representative UE assets,
not another protocol redesign.
