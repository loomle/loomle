# Editor Context Design

## Status

This document defines the confirmed first design and implemented Bridge
contract for Loomle's agent-facing Editor Context capability.

The public MCP tool name is `editor_context`. It uses an underscore rather than
`editor.context` because Claude tool definitions currently require names to
match `^[a-zA-Z0-9_-]{1,64}$`, even though newer MCP naming guidance also
permits dots and slashes. Loomle uses the smaller cross-client character set
for public MCP names. Internal RPC methods, C++ namespaces, and SDK methods do
not need to use the same separator.

## Intent

`editor_context` turns the user's last real interaction inside Unreal Editor
into the smallest exact SAL context from which an agent can continue:

1. The user focuses or selects something in UE.
2. The agent calls `editor_context`.
3. The result identifies the current surface, exact owner, and at most one
   selected target.
4. The agent copies those bindings or references into `sal_query` or
   `sal_patch` for detailed inspection or mutation.

Editor Context is interactive discovery. It is not an Editor status dump, a
second Query language, or a replacement object model for unsupported UE
domains.

## Public Tool Contract

The first version has no input fields:

```text
editor_context({})
```

The tool is read-only. It reads the connected Editor Runtime and returns one
text result containing ordered SAL Object Text and comments.

It does not return engine version, project path, runtime identity, window
title, PIE state, or connection candidates. Those belong to runtime status and
connection.

The Bridge-side result should reuse normalized ordered `ObjectResult`. The
Client formats it through the SAL formatter. Context introduces no
`context(...)` constructor, selection array, Context document type, or SAL
grammar.

## Result Principle

The result contains only:

- the complete owner locator needed by a later request;
- at most one exact selected object or relationship;
- native `type` or current name when it materially helps recognition;
- short comments identifying the UE surface and unavailable capabilities.

It does not expand unrelated fields, Pins, layout, schema, descendants, or
runtime metadata. The agent uses a following exact Query when it needs them.
A locator projection is ordinary Object Text and may omit descriptive state
that does not participate in resolution.

SAL member paths currently use ASCII identifiers. UE permits authored names
outside that grammar, including spaces and Unicode. Context never sanitizes
such a name into a false member path and does not add quoted-path syntax. It
returns the owner plus a local binding carrying the selected object's stable
`id`, then preserves the exact native name or path in an escaped comment.

## Single-Selection Contract

The first version does not support multi-selection.

| Native selection | Result |
| --- | --- |
| None | Return the exact current owner when one exists. |
| Exactly one | Return the owner and that exact target. |
| More than one | Return the owner and a count comment; return no selected target. |

An unordered UE selection must never be reduced to an arbitrary first item.

```sal
# Blueprint Editor / Graph

door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

eventGraph = graph(
  asset: door,
  id: "graph-guid"
)

# selected: multiple (3)
# selection target unavailable: multiple selection is not supported
```

The first design therefore has no cursor, selection snapshot, pagination,
`selection@id`, or implicit bulk Patch. Multi-selection and generic bulk
mutation are one future design problem and must be added together if a
concrete workflow justifies them.

## Context Tracker

The Bridge maintains a lightweight Context Tracker so the result does not
depend on whether UE still has operating-system focus when the agent calls.
When Slate still has a keyboard-focused Widget, every call first rebuilds its
current Focus Path and treats that current structural evidence as newer than
the retained record. Startup follows the same rule instead of beginning from
only UE's last active minor Tab.

The tracker listens to real Slate focus changes and Dock Tab activation. It
records only the last UE interaction source:

- Context Provider identity;
- exact surface or Tab identity;
- weak native owner references.

It does not cache serialized object state or a selected-object snapshot. On
each call, the Provider reads current native selection again and validates its
owners. A selection change therefore needs no second global tracker or
selection-event subscription. Closed tabs, reconstructed objects, and invalid
references are reported; the tracker does not silently choose another open
editor.

UE deliberately excludes Major Tabs from `FGlobalTabmanager`'s active minor
Tab pointer. A real Major-Tab foreground event is therefore retained and is
not overwritten by an older minor Tab merely because those pointers differ.
Likewise, a pathless Tab activation event is only a fallback. It must not
replace a richer Focus-Path record when it repeats the same Tab or merely
foregrounds that Editor's owner Major Tab. A different minor Tab is a real
surface change even when its content does not take keyboard focus.

A pathless event is accepted only when the event Tab is foreground in its Tab
Well and belongs to Slate's active top-level regular window. A local Tab Well
in a background window can emit the same foreground event during programmatic
restoration and is not user interaction evidence. When the Tab belongs to an
Asset Editor, its associated TabManager must also have the same registered
foreground owner Major Tab. Focus-Path records do not require UE to retain
operating-system focus; this active-window guard applies only while admitting
a new pathless event.

### Surface Resolution

Surface identity comes from structural state:

- Slate Focus Path;
- Dock `TabId`;
- Asset Editor `EditorName` associated with the exact Dock Tab;
- Slate `FTagMetaData`;
- editor-specific public selection state.

A foreground `SDockTab` found in the current Focus Path is the primary host
signal; an inactive Tab's retained keyboard focus is stale evidence. Some
auxiliary editor surfaces place keyboard focus in a separate normal docking
window whose generated path contains no `SDockTab`. In that case Context may
recover the host through UE 5.7's native docking relationship only when all of
the following are true:

- the focused window is a normal Slate window;
- it is visible, not minimized, and is not the owner Major Tab's root window;
- `FGlobalTabmanager::GetSubTabManagerForWindow()` returns the manager owned by
  a foreground Major Tab, and that manager owns a Docking Area for the exact
  auxiliary window;
- exactly one open Asset Editor has an `AssociatedTabManager` exactly equal to
  that manager;
- that Editor owns exactly one edited Asset.

The recovered owner Major Tab becomes the retained structural locator. The
owner/root window is deliberately excluded because UE 5.7 returns the first
foreground matching child TabManager there and does not prove which root
subtree owns the Focus Path. A world-centric Editor, a shared manager,
multiple matching Editors, or multiple edited Assets likewise remains
unavailable. Context never substitutes window titles, last-activation
timestamps, or the first open Editor. When this window-level recovery is
needed, a short ordinary comment may preserve the focused leaf Widget type,
for example `focus: SMultiLineEditableText`; the leaf is diagnostic context
and never an object locator.

Localized window titles, visible asset names, widget text, and heuristic
scoring are not identity.

Transient Popup and Menu surfaces inherit the Provider that opened them. A
tooltip does not become current context. A Modal Dialog instead suppresses the
previous context so an agent cannot mutate a stale target while the user is
responding to a save, import, or confirmation dialog.

Modal suppression is dynamic: opening the dialog does not overwrite the exact
pre-Modal record. Closing it therefore restores the retained Content Browser,
Details View, or Major-Tab interaction instead of guessing from UE's last
active minor Tab.

```sal
###
surface: modal dialog
selection: unavailable
previous context: suppressed
###
```

## Provider Model

Context extraction uses a Bridge-internal Provider registry, not one
hard-coded window switch. A Provider must:

1. recognize an exact UE interaction surface;
2. read native owner and selection through public UE APIs;
3. project supported objects into ordinary SAL Object Text or return a
   faithful unsupported description.

Initial priority is:

1. Modal Dialog
2. Blueprint Graph
3. My Blueprint
4. Blueprint Components
5. Class Settings and Class Defaults
6. Widget Designer
7. Content Browser
8. Level Editor
9. Generic Details
10. Generic Asset Editor
11. Unknown Surface

The most specific Provider on the real focus path wins. The first version does
not expose Provider registration as a public plugin API. A future Bridge
module can add a Provider without changing SAL grammar or the normalized
result contract.

## Blueprint Graph

Once the exact Blueprint Editor host is structurally established, the
Provider uses:

- `FBlueprintEditor::GetUISelectionState()`;
- `FBlueprintEditor::GetFocusedGraph()`;
- `FBlueprintEditor::GetSelectedNodes()`.

It does not require the focused leaf to be one of a fixed set of Blueprint
panels. `GetUISelectionState()` determines Graph, My Blueprint, Components,
Class Settings, or Class Defaults, and the corresponding native selection API
determines the selected object. This lets a search box, Details value editor,
or multiline text field retain its owning Blueprint context without scanning
the active window for `SGraphEditor` widgets or scoring multiple Graph
Editors. The focused Graph and selected objects are still validated against
the Blueprint returned by `GetBlueprintObj()`.

No Node selection returns the Blueprint and focused Graph:

```sal
# Blueprint Editor / Graph

door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

eventGraph = graph(
  asset: door,
  id: "graph-guid"
)

# selected: none
```

One selected Node adds exact identity:

```sal
beginPlay = node(
  graph: eventGraph,
  id: "node-guid",
  type: "/Script/BlueprintGraph.K2Node_Event"
)
```

When a Graph binding contains both `id` and `name`, resolution uses `id` as
identity and treats `name` as a recognition check. Current names and native
types may therefore remain in returned Object Text without making the locator
unusable or allowing a name to override a mismatched id.

The Provider does not return a selected Pin. `GetGraphPinForMenu()` is a
transient context-menu Pin, not persistent Pin selection.

Other Graph families may reuse the native mechanics, but an unsupported Graph
family returns its Asset owner and native Graph or Node information without
pretending that the K2 Graph interface applies.

## My Blueprint

`SMyBlueprint` uses single-selection mode and exposes the selected action
family. Existing authored objects map by UE meaning:

| My Blueprint action | Context target |
| --- | --- |
| Member Variable | Blueprint `variable` |
| Event Dispatcher | Blueprint `dispatcher` |
| Function, Macro, Event Graph, Interface Graph | `graph` |
| Existing Event or Input Action Node | owning `graph` and authored `node` |
| User Defined Enum or Struct | `asset`, when independently locatable |
| Category or empty action selection | Blueprint owner plus an unavailable-selection comment |
| Local Variable | Function Graph owner plus unsupported native description |

```sal
# Blueprint Editor / My Blueprint

door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

door.Health = variable(
  id: "variable-guid"
)
```

Functions and macros remain Graph objects:

```sal
openDoor = graph(
  asset: door,
  id: "graph-guid",
  name: OpenDoor,
  type: GT_Function
)
```

Local Variables are scoped to a Function, not to the Blueprint declaration
collection. They must not be exposed as Blueprint `variable@id`:

```sal
openDoor = graph(
  asset: door,
  id: "graph-guid"
)

###
selected: local variable
name: Damage
scope: OpenDoor
interface: unavailable
###
```

An Event or Input Action is returned as a Node only when its action resolves
to an existing authored Node in an owning Graph. A template action is not an
existing object.

UE 5.7's public `SMyBlueprint::SelectionIsCategory()` is implemented as the
inverse of "has a selected action", so it cannot distinguish a selected
category from no action selection. Context reports that state as unavailable
instead of guessing `category` or `none`.

## Blueprint Components

The Provider reads
`FBlueprintEditor::GetSelectedSubobjectEditorTreeNodes()` and distinguishes
native ownership.

Only an `USCS_Node` owned by the current Blueprint's
`USimpleConstructionScript` is a current Blueprint `component`. Its SAL `id`
is `USCS_Node::VariableGuid`. The result returns the shortest SCS ancestor
chain:

```sal
# Blueprint Editor / Components

door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)

door.Root = component(
  id: "root-guid",
  type: "/Script/Engine.SceneComponent"
)

door.Root.Mesh = component(
  id: "mesh-guid",
  type: "/Script/Engine.StaticMeshComponent"
)
```

An inherited Blueprint Component is not returned as owned by the child
Blueprint. That could make a later Patch modify the parent Blueprint when the
visible intent concerned an inherited override.

```sal
###
selected: inherited component
name: Mesh
type: "/Script/Engine.StaticMeshComponent"
ownerClass: "/Game/Base/BP_BaseDoor.BP_BaseDoor_C"
interface: unavailable
###
```

A C++ Native Component is also unsupported until inherited/native override
semantics receive their own design. The tree's Actor Root is not a Component;
selecting it returns only the Blueprint owner and `selected: actor root`.

## Class Settings And Class Defaults

These Blueprint Editor states have different exact SAL targets and do not
introduce panel objects.

Class Settings edits authored `UBlueprint` state:

```sal
# Blueprint Editor / Class Settings

door = blueprint(
  asset: "/Game/BP_Door.BP_Door",
  id: "blueprint-guid"
)
```

Class Defaults edits effective values on the generated Class:

```sal
# Blueprint Editor / Class Defaults
# source: /Game/BP_Door.BP_Door

doorClass = class(
  path: "/Game/BP_Door.BP_Door_C"
)
```

The CDO is not a public object. If no valid `GeneratedClass` exists, the
Provider returns the Blueprint and reports Class Defaults unavailable. It does
not substitute `SkeletonGeneratedClass`.

Details property rows have no stable general selection contract. The first
version returns the Blueprint or Class owner and does not infer a field from a
focused value widget.

## Widget Designer

After the exact Widget Blueprint Editor host is structurally established, the
Provider accepts UE 5.7 Designer mode (`DesignerName`) and reads its native
selection regardless of which auxiliary leaf Widget currently has keyboard
focus. Graph mode is handled by the Blueprint Provider instead, and Preview
mode does not inherit a stale Designer selection. Animation and Navigation are
auxiliary panels inside UE's Designer application mode rather than separate
modes. Focusing them may therefore retain the exact Widget Blueprint owner and
current native Widget selection, but Context does not claim to discover an
Animation or Navigation object until those domains are designed.

The Provider uses `FWidgetBlueprintEditor::GetSelectedWidgets()` and resolves
each `FWidgetReference` through `GetTemplate()`. The template is serialized
source state; `GetPreview()` is transient and never becomes identity.

Widget Details exposes Preview Widget instances through its internal Details
View. The Widget Provider therefore owns that Tab and resolves the current
editor selection back through `FWidgetReference`; Generic Details must not
serialize the Preview instance as the source Widget.

A source Widget's stable `id` is its entry in
`WidgetVariableNameToGuidMap`. Context does not create or repair missing or
duplicate Widget GUIDs.

One selected Widget returns the WidgetBlueprint owner and shortest ancestor
chain:

```sal
# Widget Designer

menu = blueprint(
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "blueprint-guid"
)

menu.Root = widget(
  id: "root-guid",
  type: "/Script/UMG.CanvasPanel"
)

menu.Root.Stack = widget(
  id: "stack-guid",
  type: "/Script/UMG.VerticalBox"
)

menu.Root.Stack.StartButton = widget(
  id: "button-guid",
  type: "/Script/UMG.Button"
)
```

Hover does not alter Context. Selecting the transient Preview Root returns the
WidgetBlueprint and reports `preview root` without an object target.

`GetSelectedNamedSlot()` identifies a relationship on a host, not a separate
Widget. Context returns the host and identifies the existing member path:

```sal
# selected: widget@host-guid.NamedSlots.Header
```

It introduces no Slot object or query surface.

An inherited Named Slot is represented by UE with a Slot name but no host
Widget reference. Context returns the WidgetBlueprint owner and an inherited
Named Slot description; the missing host is native state, not an invalid-owner
diagnostic.

Widget Details can also display a non-Widget editor model, including an MVVM
settings object. Context preserves that native one-object selection beside the
WidgetBlueprint owner and reports `interface: unavailable`; it does not call
the state `none` or imply MVVM support. A stale ordinary Named Slot host or
source Widget is an invalid-owner diagnostic, not an inherited or Preview
selection.

## Content Browser

The Provider recognizes an actual `SContentBrowser` ancestor or a native
Content Browser Dock Tab. A bare `SAssetView` is insufficient because UE also
embeds it in Asset Pickers and Asset Dialogs.

When focus is inside the Browser's Asset View, the tracker retains that exact
public `SAssetView` and the Provider calls its side-effect-free
`GetSelectedViewItems()` at read time. It does not activate or read a process-wide
"primary" Browser, so one Browser cannot leak another Browser's selection.

One selected Asset returns unloaded Asset Registry identity:

```sal
# Content Browser

door = asset(
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
)
```

One selected Folder does not create a `folder(...)` constructor:

```sal
# Content Browser

###
selected: folder
virtualPath: "/All/Game/Blueprints"
internalPath: "/Game/Blueprints"
interface: unavailable
###
```

With no Asset View selection, Context reports `selected: none`. Selection
count comes from the actual view items, so an item still being created or
renamed remains one unsupported temporary selection rather than disappearing.
If the focused Browser subview has no public side-effect-free selection API,
Context returns the Browser surface and explicitly reports selection
unavailable. Native Content Browser Data Source items are preserved as file,
folder, or special item identity; only a non-temporary file that losslessly
converts to valid `FAssetData` becomes an `asset(...)` binding.

## Level Editor

The Provider accepts only native Level Editor Viewport, Scene Outliner, and
Selection Details Tab IDs. A bare `SSceneOutliner` is not sufficient because
Actor Pickers, Component Pickers, and custom Outliners use the same Slate
type. The common `LevelEditorViewport` Slate tag is also insufficient because
UE assigns it as the default metadata for generic `SEditorViewport` instances.

The Provider starts from `UTypedElementSelectionSet`, not only legacy Actor
selection. This preserves Actors, Components, UObjects, BSP, geometry, and
other typed elements.

SAL currently has no general Level or Actor interface. One selected Actor
returns the saved authored World Asset that owns its exact Level when
available, rather than always substituting the Editor World's persistent Map.
For an edited Level Instance this is the source World from
`ULevelInstanceSubsystem`, not the temporary instance package. Zero, multiple,
or unsupported typed selections retain the Editor World owner. The Actor
result then preserves native identity:

```sal
# Level Editor

testMap = asset(
  path: "/Game/Maps/TestMap.TestMap",
  type: "/Script/Engine.World"
)

###
selected: actor
ref: actor@actor-guid
label: "Enemy_2"
path: "/Game/Maps/TestMap.TestMap:PersistentLevel.Enemy_2"
type: "/Script/Engine.StaticMeshActor"
interface: unavailable
scope: owning Level only
graphPaletteUse: requires the exact owning Level Blueprint and Graph target
###
```

`AActor::ActorGuid` is native persistent editor identity scoped to its authored
Level. It is not a project-global Actor locator. The existing Graph Palette
condition may consume `actor@id` only because its target Level Blueprint
establishes that Level scope; this does not imply a general Actor Query or
Patch interface.

For an instanced streaming Level, `ActorInstanceGuid` may distinguish the
visible instance while `ActorGuid` identifies the authored Actor. Context
advertises `actor@ActorGuid` as an available Graph Palette reference only when
the selected Actor resolves uniquely and its authored source World is an exact
registered Asset that can establish the Palette target. Otherwise it preserves
both available native GUIDs and marks the Palette reference unavailable rather
than selecting the wrong instance or exposing a temporary package.

An Actor instance Component is not a Blueprint SCS `component@id`:

```sal
###
selected: actor component
owner: actor@actor-guid
name: StaticMeshComponent0
path: "/Game/Maps/TestMap.TestMap:PersistentLevel.Enemy_2.StaticMeshComponent0"
type: "/Script/Engine.StaticMeshComponent"
interface: unavailable
###
```

The Provider reads Editor World, never transient PIE World. It does not
generate an invalid ActorGuid during a read. An unsaved Map has no Asset
Registry locator and is reported by native Package Path only.

## Generic Details

`IDetailsView` exposes selected Objects and Actors. When no specific Provider
owns the surface:

- one object with an active SAL interface becomes ordinary Object Text;
- one Asset becomes `asset(path, type)`;
- one unsupported UObject returns its nearest Asset owner plus native Path and
  Type comments;
- if that transient/editor-model object has no Asset in its Outer chain,
  Generic Details may retain the uniquely associated Toolkit Asset owner;
- no or multiple selected objects retain the unique edited Asset owner when
  the exact Asset Editor association is available;
- multiple objects follow the shared multi-selection rule;
- a focused property row does not become a selected Property.

## Generic Asset Editor

The Provider derives editor-to-asset ownership from the exact Toolkit owner
Tab, or from the guarded focused-window recovery described above, and
`UAssetEditorSubsystem`; it never uses window-title comparison or picks the
first editor from an ambiguous match.

For a world-centric Asset Editor, UE intentionally exposes no associated
TabManager. Its tabs share the Level Editor TabManager with native Viewport,
Outliner, Details, and other host tabs, so a unique hosted toolkit still does
not prove exact Tab ownership. Context does not infer that ownership from the
shared manager or edited Asset set. Without a toolkit-specific structural
owner signal, the Asset Editor association remains unavailable rather than
being guessed.

If the Editor owns one Asset, Context returns it. If one toolkit edits several
Assets and exposes no active-document API, Context reports ambiguous owner
candidates and returns no arbitrary target. This is not presented as native
multi-selection. Internal state without a public semantic selection API is
unavailable.

```sal
# Niagara System Editor

fire = asset(
  path: "/Game/FX/NS_Fire.NS_Fire",
  type: "/Script/Niagara.NiagaraSystem"
)

###
surface: NiagaraSystemEditor / SystemOverview
selection: unavailable
provider: unavailable
###
```

## Unknown Surface

An unrecognized surface remains discoverable even when it has no object:

```sal
###
surface: OutputLog
tab: OutputLog
selection: unavailable
###
```

Unknown Surface never falls back to a stale Asset, Graph, Actor, or Widget
selection from another panel.

## Unsupported Objects

Universal discovery does not mean universal mutation. When a selected native
object has no SAL interface, Context:

1. returns the nearest exact supported owner when one exists;
2. preserves native identity and type in an adjacent block comment;
3. states `interface: unavailable`.

It does not omit the selection, map it to a neighboring domain, or introduce a
universal `object(...)` constructor. UE selections may be UObjects,
Properties, Graph Pins, Folders, typed elements, relationships, or private
editor models; one invented constructor would promise semantics they do not
share.

## Diagnostics

No selection, unsupported selection, multiple selection, and unknown surface
are successful observations expressed through comments, not transport errors.

A diagnostic is appropriate when:

- no Editor Runtime is connected;
- the recorded surface or owner became invalid before read;
- a Provider fails native consistency checks;
- a supported object's required persistent identity is missing or duplicated;
- Bridge result validation fails.

Diagnostics guide the next action and never offer a guessed target.

## Deferred Scope

The first version intentionally defers:

- multi-selection and generic bulk Patch;
- persistent selection snapshots;
- selected Graph Pins;
- Details property-row identity;
- Blueprint local-variable operations;
- inherited and native Component override operations;
- general Level, Actor, and Actor Component interfaces;
- Folder operations;
- Widget animation, navigation, and MVVM surfaces;
- private semantic selection from unsupported third-party editors.

Each area requires its own UE workflow and interface design. Context may reveal
the gap but must not invent an approximate object.

## UE 5.7 Source Basis

The design relies on:

- `IAssetEditorInstance::GetAssociatedTabManager()` and
  `UAssetEditorSubsystem`;
- `FBlueprintEditor::GetUISelectionState()`, `GetFocusedGraph()`,
  `GetSelectedNodes()`, and `GetSelectedSubobjectEditorTreeNodes()`;
- `SMyBlueprint` typed selection accessors and single-selection action menu;
- `FWidgetBlueprintEditor::GetSelectedWidgets()` and
  `GetSelectedNamedSlot()`, plus inherited `IsModeCurrent()`;
- `FWidgetReference::GetTemplate()`;
- the exact public `SAssetView::GetSelectedViewItems()` and
  `FContentBrowserItem` conversion APIs;
- Level Editor `UTypedElementSelectionSet`;
- `AActor::GetActorGuid()` and `GetActorInstanceGuid()`;
- `IDetailsView::GetSelectedObjects()`.

These native APIs are the implementation source of truth. If implementation
shows a different ownership or lifetime rule, update the design before adding
an approximation.

## Acceptance Audit

Implementation is acceptable only if it demonstrates that:

- supported panel changes come from real interaction events, not titles;
- leaving UE preserves the last valid UE interaction source;
- closed or reconstructed objects are revalidated;
- zero, one, and multiple selection follow the shared contract;
- Graph Context never reports a context-menu Pin as selected;
- Widget Context never returns a Preview Widget;
- child Blueprint Context never returns an inherited Component as locally
  owned;
- Class Settings and Class Defaults return different exact targets;
- Content Browser discovery does not load an Asset or mistake an Asset Picker
  for the Content Browser;
- Level Context never confuses instance Components with SCS Components;
- an Actor reference is never treated as global outside its owning Level;
- Modal Dialog and Unknown Surface never expose a stale previous target;
- successful content is ordered Object Text and comments with no Context
  grammar.

### Implementation Audit — 2026-07-18

The implemented Provider registry, tracker, Object Result projection, internal
`editor.context` RPC, and public `editor_context` Client tool compile against
UE 5.7. The shared SAL contract tests and all 32 Client unit tests pass.

Source audit confirmed the important conservative boundaries: recorded Asset
Editor pointers are re-resolved through the recorded Dock Tab before use;
world-centric ownership is not inferred from the Level Editor's shared
TabManager; selected Widgets must be unique source Widgets in the current
Blueprint; and multiple selection never degrades to an arbitrary first item.
Each read also rebuilds the current keyboard Focus Path when one exists. A
focus path without an `SDockTab` can recover an Asset Editor only through UE's
foreground Major Tab and an exact auxiliary-window Docking Area, with exactly
one Editor and one edited Asset. Hidden, minimized, and owner/root windows are
not accepted for this recovery.

Per the confirmed implementation scope, this audit did not launch Unreal
Editor or run live interaction scenarios. The behavioral checklist above is
therefore source-audited and compiled, while live panel-by-panel verification
remains the next integration phase.

## Naming References

- [Claude tool name requirements](https://platform.claude.com/docs/en/agents-and-tools/tool-use/define-tools)
- [MCP SEP-986 tool naming guidance](https://modelcontextprotocol.io/seps/986-specify-format-for-tool-names)
