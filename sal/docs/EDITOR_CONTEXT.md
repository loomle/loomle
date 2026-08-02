# Editor Context Design

## Status And Intent

`editor_context({})` is a read-only observation of the user's latest meaningful
UE Editor interaction. It returns the smallest exact SAL Target and, when
available, one exact selected object or relationship.

It is not a second object model, a selection snapshot API, a generic Details
reflection API, or a source of implicit mutation scope.

## Public Contract

The first version has no input:

```text
editor_context({})
```

It does not return project binding, engine version, PIE state, runtime
connection candidates, or window titles. Those belong to session/project
tools.

The result uses the same SAL result envelope as Query:

- `exact_target` when a Domain Target can be opened and canonicalized;
- `unresolved_target` with `resolution.unresolved_target` when no exact
  Target exists.

Context adds no SAL grammar, object kind, or Target variant.

## Result Principle

The result contains only:

- the canonical active Target;
- at most one exact selected StableRef or selected relationship;
- minimal ordinary object data useful for recognition;
- short observation comments in Object Text;
- registered diagnostics, delivered in a later independent MCP text block.

It does not expand unrelated fields, schema, Pins, descendants, layout, or
runtime metadata. The agent performs a following exact Query.

Example:

```sal
result exact_target
target g = target {
  domain: graph,
  asset: "/Game/BP_Door.BP_Door",
  blueprintId: "11111111-1111-1111-1111-111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}
objects
selected = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}
# surface: Blueprint Editor / Graph
```

The tag is presentation only. The selected StableRef is
`@aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa` inside `g`.

## Single-Selection Contract

| Native selection | Result |
| --- | --- |
| None | exact current owner Target when one exists; no selected object |
| Exactly one | owner Target plus that exact supported object or relationship |
| More than one | owner Target plus count comment; no arbitrary selected object |

An unordered native selection is never reduced to its first item.
Multi-selection, pagination, persistent snapshots, and implicit bulk Patch are
deferred together.

## Context Tracker

The Bridge keeps a lightweight tracker of the last real UE interaction:

- recognized provider identity;
- exact surface or Dock Tab identity;
- weak native owner references.

It does not cache serialized object state or selection. Every call rereads the
provider's current native selection and revalidates ownership and identity.

When Slate has current keyboard focus, Context rebuilds the Focus Path and
prefers that structural evidence. A foreground Dock Tab is stronger than stale
focus retained by an inactive Tab.

Pathless activation is accepted only when the Tab is foreground in its Tab
Well and belongs to the active regular Slate window. Asset Editor association
must agree with its registered foreground owner Major Tab.

Standalone Asset Editor creation has one native ordering edge: UE may
foreground the `StandaloneToolkit` Major Tab before the editor has installed
its associated Tab Manager and registered itself with
`UAssetEditorSubsystem`. A context observation made in that interval cannot
yet prove the Asset Editor owner and may initially be Unknown.

When Context is read, that provisional record may be recognized again only
when the exact same tracked Tab is still valid, foreground, visible, and in
Slate's active regular window. The delayed recognition then uses the completed
Tab Manager and `UAssetEditorSubsystem` association. It never searches for a
different Tab, chooses the first open editor, or revives a background owner.
Failure to establish one unique association remains unresolved.

If exact window ownership proves one Blueprint Editor but UE has not yet
published a focused Graph document, Context returns that editor's exact
Blueprint Target through the generic Asset Editor mapping. A visible Graph name,
restored-document label, or conventional `EventGraph` name is not enough to
invent a Graph Target. Once UE reports an owned focused Graph, Context returns
the exact Graph Target without requiring a selection.

An editor Focus Path without an `SDockTab` can recover its owner from the
containing normal window only when UE's docking relationship proves:

- visible, non-minimized normal window;
- exactly one candidate Asset Editor whose registered Major Tab owns that root
  window, or whose associated sub-TabManager is UE's native match for that
  auxiliary Docking Area;
- that sub-TabManager is registered to the same foreground Major Tab;
- exactly one matching Asset Editor;
- exactly one edited Asset.

UE 5.7's `FGlobalTabmanager::GetSubTabManagerForWindow()` recognizes both the
Major Tab's parent window and auxiliary Docking Areas owned by its TabManager.
Context preserves that native distinction and applies a uniqueness check over
all open Asset Editors before accepting either path. The Major Tab parent
window is matched directly and is not rejected merely because it is the
editor's root window.

Window title, localized visible text, timestamps, heuristic scores, and “first
open editor” are never identity.

Transient menus inherit their opener. Tooltips do not become context. A Modal
Dialog suppresses the prior context dynamically:

```sal
result unresolved_target
no_objects
```

The later diagnostic content block is:

```sal
###
SAL diagnostics
ERROR resolution.unresolved_target: modal dialog suppresses previous context
###
```

Closing the dialog reveals the retained prior interaction rather than guessing
another surface.

## Provider Registry

“Provider” is a private Bridge implementation term, not a SAL Domain-like
public concept. A provider:

1. recognizes one exact editor surface;
2. reads owner and selection through native public APIs;
3. maps them to one of the six existing Domain Targets;
4. emits supported object data or faithful unsupported evidence.

Priority:

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

The most specific provider on the real Focus Path wins.

## Surface Mappings

### Blueprint Graph

The focused Graph becomes the main Graph Target. One selected Graph Node maps
to `@NodeGuid`. A selected Pin is deferred because Graph editors do not expose
one reliable persistent selected-Pin API across surfaces.

An `SGraphEditor` on the current Focus Path is direct structural evidence for
the Graph surface and takes precedence over a stale Blueprint UI selection
state. When no other Blueprint surface is explicitly selected,
`FBlueprintEditor::GetFocusedGraph()` also identifies the active Graph
document even when the Graph selection is empty. An empty Node selection is a
successful Graph observation: it returns the exact Graph Target with no
selected object rather than falling back to the Blueprint Target or an
unresolved surface.

The retained observation records that exact Graph document. Before projecting
a result, Context requires the editor to report the same focused Graph, still
owned by the same Blueprint. When structural evidence repaired an empty or
initialization-stale Blueprint UI state, that UI state must also remain
unchanged. A later Graph-document or explicit Blueprint-surface change therefore
invalidates the old observation instead of silently retargeting it.

Graph Target canonicalization requires Asset Path, owning `BlueprintGuid`, and
`GraphGuid`. The context never substitutes current Graph name for a missing
Guid.

### My Blueprint

The Blueprint is the main Blueprint Target. The selected action maps by native
meaning:

| My Blueprint action | Context result |
| --- | --- |
| Member Variable | Blueprint Variable StableRef |
| Event Dispatcher | Blueprint Dispatcher StableRef |
| Function, Macro, Event Graph, Interface Graph | related Graph Target |
| Existing Event or Input Action | owning Graph Target plus authored Node StableRef |
| User Defined Struct or Enum | Asset Target when independently locatable |
| Category or empty action selection | Blueprint Target plus unavailable-selection evidence |
| Local Variable | Function Graph Target plus unsupported native description |

A selected Graph may additionally produce a related Graph Target and navigation
handoff. The Blueprint object itself remains structural `target`, not a
synthetic ref.

A Local Variable is Function-scoped and never becomes a Blueprint Variable
StableRef. An Existing Event or Input Action becomes a Node only when the
action resolves to an existing authored Node; a template action is not an
object.

UE 5.7's public `SelectionIsCategory()` is the inverse of “has a selected
action”, so it cannot distinguish a category from no action. Context reports
the selection unavailable instead of guessing.

### Blueprint Components

Only locally authored SCS Components with a valid
`USCS_Node::VariableGuid` map to Blueprint StableRefs. Native, inherited,
preview, instance, and child-actor Components are not presented as locally
owned SCS objects.

### Class Settings And Defaults

Class Settings returns a Blueprint Target. Class Defaults returns the exact
generated Class Target:

```sal
target c = target {
  domain: class,
  path: "/Game/BP_Door.BP_Door_C"
}
```

When compilation is relevant, Class result may add a related Blueprint Target
and handoff.

If `GeneratedClass` is absent or invalid, Class Defaults returns the Blueprint
Target with an unavailable diagnostic. It never substitutes
`SkeletonGeneratedClass`.

### Widget Designer

The main Target is Widget Domain, not Blueprint Domain:

```sal
target menu = target {
  domain: widget,
  asset: "/Game/UI/WBP_Menu.WBP_Menu",
  id: "11111111-1111-1111-1111-111111111111"
}
```

One selected source Widget maps to `@WidgetGuid`. Preview Widgets, inherited
templates not authored by the current WidgetTree, and ambiguous selections are
not returned as local Widgets.

A selected Named Slot is a relationship member such as
`@host-guid.NamedSlots.Header`, not a Slot object.

### Content Browser

One selected non-temporary Asset becomes an exact Asset Target after native
Class verification:

```sal
target door = target {
  domain: asset,
  path: "/Game/Blueprints/BP_Door.BP_Door",
  type: "/Script/Engine.Blueprint"
}
```

Context reads the exact focused `SAssetView` and does not load the Asset. A
folder is unsupported evidence, not a Domain Target. No or multiple selection
uses the single-selection rules.

### Level Editor

SAL currently has no Level or Actor Domain. Context may return the exact owning
World as an Asset Target and preserve the selected Actor as ordinary evidence:

```sal
result exact_target
target world = target {
  domain: asset,
  path: "/Game/Maps/TestMap.TestMap",
  type: "/Script/Engine.World"
}
objects
{
  nativeKind: "actor",
  actorGuid: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  label: "Enemy_2",
  path: "/Game/Maps/TestMap.TestMap:PersistentLevel.Enemy_2",
  type: "/Script/Engine.StaticMeshActor",
  stableRefAvailable: false
}
```

`ActorGuid` is scoped to an authored Level but is not a StableRef in any of the
six current Domain identity environments. Context does not emit a fused actor
reference or imply a general Actor Query/Patch interface.

For Level Instances, native authored and instance Guids remain ordinary
evidence. A single selected Actor resolves to the saved authored World that
owns its exact Level. For an edited Level Instance this is the source World
reported by `ULevelInstanceSubsystem`, never the temporary instance package.
Zero, multiple, or unsupported selections retain the Editor World owner.
Editor Context reads Editor World, never transient PIE World.

The native `SLevelViewport` widget type is direct structural evidence for the
Level Editor even when its Focus Path contains no `SDockTab`. The broader
`SEditorViewport` type and generic `LevelEditorViewport` metadata remain
insufficient because asset editors and custom viewports reuse them.

An unsaved Editor World has no registered persistent Asset Target. Context
still reports the recognized Level Editor surface and current selection state,
but returns `unresolved_target` with a diagnostic that identifies the temporary
map package and suggests saving the map. It never turns the transient package
name into an Asset Target.

### Generic Details

Specific providers take precedence. Otherwise:

- one object with an existing Domain Target maps to that Target;
- one Asset maps to Asset Domain;
- one unsupported UObject returns its nearest exact supported owner Target
  plus ordinary native evidence;
- multiple objects return no arbitrary object;
- a focused property row does not become a selected Property.

The existence of `{...}` does not create generic mutation semantics for every
reflected object.

### Generic Asset Editor

Ownership comes only from exact Toolkit/Dock association and
`UAssetEditorSubsystem`. A toolkit editing one Asset returns the corresponding
Asset Target unless a more specific Domain provider applies.

Multiple edited Assets without an active-document API are ambiguous. A
world-centric toolkit sharing the Level Editor TabManager is not assigned by
guessing.

### Unknown Surface

An unrecognized surface produces unresolved context:

```sal
result unresolved_target
no_objects
```

Its later diagnostic content block is:

```sal
###
SAL diagnostics
ERROR resolution.unresolved_target: OutputLog has no SAL Target
###
```

It never falls back to a stale selection from another panel.

## Unsupported Native Objects

Universal observation does not mean universal interface support. For a
selected native object outside current Domains, Context:

1. returns the nearest exact supported owner Target when one exists;
2. preserves native identity and type as ordinary object data or comments;
3. states that StableRef and mutation are unavailable.

It does not invent a semantic tag, Target, StableRef, or neighboring Domain.

## Diagnostics

No selection and multiple selection are successful observations when an exact
owner Target exists. Diagnostics are required when:

- no Editor Runtime is connected;
- no exact Target can be established;
- retained surface or owner became invalid;
- required persistent identity is missing or duplicated;
- result validation fails.

Suggestions point to an exact Query on the returned Target and never invent
missing Target fields.

## Deferred Scope

- multi-selection and bulk Patch;
- selected Graph Pins;
- Details property-row identity;
- general Level, Actor, and Actor Component Domains;
- Folder operations;
- Widget animation, navigation, legacy binding, and MVVM;
- private semantic selection from unsupported third-party editors.

## UE 5.7 Source Basis

The implementation relies on public native sources including:

- `IAssetEditorInstance::GetAssociatedTabManager()` and
  `UAssetEditorSubsystem`;
- `FBlueprintEditor::GetUISelectionState()`, `GetFocusedGraph()`,
  `GetSelectedNodes()`, and `GetSelectedSubobjectEditorTreeNodes()`;
- `SMyBlueprint` typed selection accessors and single-selection action menu;
- `FWidgetBlueprintEditor::GetSelectedWidgets()`,
  `GetSelectedNamedSlot()`, and inherited `IsModeCurrent()`;
- `FWidgetReference::GetTemplate()`;
- exact `SAssetView::GetSelectedViewItems()` and Content Browser item
  conversion;
- Level Editor `UTypedElementSelectionSet`;
- `AActor::GetActorGuid()` and `GetActorInstanceGuid()`;
- `IDetailsView::GetSelectedObjects()`.

If implementation proves a different ownership or lifetime rule, update this
design before adding an approximation.

## Acceptance

Acceptance verifies:

- focus and Tab changes come from structural interaction evidence;
- leaving UE preserves the last valid UE source;
- closed/reconstructed owners are revalidated;
- zero, one, and multiple selection follow the shared rule;
- Graph never invents selected Pins;
- My Blueprint never promotes a Local Variable or template action to an
  authored Blueprint object;
- Widget never returns Preview Widgets;
- inherited Components never appear locally owned;
- Class Settings and Defaults return different Domains;
- invalid Class Defaults never substitute `SkeletonGeneratedClass`;
- Content Browser reads do not load assets or mistake Asset Pickers for the
  Browser;
- a Level Instance Actor resolves through its authored source World rather
  than a temporary instance package;
- Level Actor evidence never becomes a global StableRef;
- Modal and Unknown surfaces never expose stale Targets;
- every successful exact result contains a canonical Target table;
- no provider introduces public syntax or implicit Domain composition.

### Implementation Audit — 2026-07-31

The Provider registry, tracker, result projection, private `editor.context`
RPC, and public `editor_context` tool remain the implementation path covered by
this design.

A live UE 5.7 standalone Blueprint interaction exposed an ordering gap: the
visible `BP_LoomleE2E / EventGraph` surface returned Unknown
`StandaloneToolkit` context. UE source confirms that
`FAssetEditorToolkit::InitAssetEditor()` can foreground the Major Tab before
creating the associated Tab Manager and calling `NotifyAssetsOpened()`. The
confirmed correction is the same-Tab delayed recognition rule above, together
with structural `SGraphEditor` and valid focused-Graph handling for an empty
Node selection.

This correction adds no public call input, SAL syntax, result field, Target
variant, or implicit mutation scope. Implementation acceptance requires a live
standalone Blueprint test that starts Context tracking before the editor opens,
observes the pre-registration pathless Unknown record on that same
`StandaloneToolkit` Major Tab, opens an Event Graph with no selected Node and
no follow-up click, and returns the canonical exact Graph Target. It also
requires an old Graph observation to fail after the focused Graph document
changes. Background, ambiguous, closed, or unregistered owners must continue
to fail closed.

### Implementation Audit — 2026-08-01

A later real Blueprint interaction returned Unknown `SGraphPanel` even though
the focused Blueprint and EventGraph remained open. The result proves that the
Graph leaf was observed but no Asset Editor owner reached the Blueprint
provider. Review against UE 5.7 found that Loomle's window recovery accepted
only an auxiliary Docking Area and explicitly rejected the foreground Major
Tab's own parent window, while UE's native window-to-sub-TabManager mapping
supports both.

The correction is to evaluate all open Asset Editors by their associated
TabManager, registered foreground Major Tab, and exact window ownership, then
accept the owner only when one editor and one edited Asset remain. Acceptance
adds a rendered standalone Blueprint regression for root-window recovery and
retains negative coverage for ambiguous, background, unregistered, and
auxiliary-window ownership. No public syntax or result shape changes.

A follow-up live audit isolated two remaining cases. First, an unsaved map with
keyboard focus on a native `SLevelViewport` fell through to Unknown because the
Focus Path contained no viewport DockTab. Second, a restored standalone
Blueprint initially retained only its foreground `StandaloneToolkit` Major Tab;
before the user clicked the Graph, Context did not recover the registered Asset
Editor. Clicking the native `SGraphPanel` immediately produced the correct
Graph Target and selected Node, proving that Graph and selection projection
were already sound.

The correction recognizes exact `SLevelViewport` structure without weakening
generic viewport rejection, reports unsaved-map identity failure explicitly,
and lets a pathless foreground Major Tab reuse the same exact window-owner
recovery used by focused widgets. A recovered Blueprint Editor without a native
focused Graph falls back to its exact Blueprint Target. It does not infer a
Graph from names or restored visible labels.
