# SAL Reference Queries

## Purpose

`references` answers a factual question: where does authored UE state refer to
this exact declaration or member?

It does not perform fuzzy text search, infer references from display names, or
return speculative runtime relationships.

## Exact Subject

The subject is interpreted inside the active exact Target:

```sal
query door
references to @health-variable-guid
```

Member evidence may narrow one compound object:

```sal
query g
references to @call-node-guid.FunctionReference
```

The exact Target itself is structural, but it is a reference subject only when
the active Domain can prove native declaration identity:

```sal
query functionGraph
references to target

query interfaceFunctionGraph
references to target.InterfaceGuid
```

Bare `target` currently resolves only for callable Function or Macro Graph
Targets. The direct `target.InterfaceGuid` member resolves for a Graph with a
valid implemented-Interface declaration. Ordinary Event, Construction Script,
Collapsed, Dispatcher Signature, and Timeline Graphs, plus Asset, Blueprint,
Class, StateTree, and Widget Targets, return
`capability.reference_unavailable` with exact Target context.

Subjects normalize to:

```ts
type ExactRelationshipSubject =
  | TargetSelfRef
  | TargetSelfMemberRef
  | StableRef
  | StableMemberRef;
```

Semantic tags are allowed as erasable presentation but never choose the
declaration:

```sal
node @call-node-guid
```

If one object contains several independent declaration facts, an unqualified
object subject is ambiguous. The diagnostic returns exact member candidates.

## Scope

Without `in project`, scope is exactly the active Domain's local authored
container:

| Domain | Local scope |
| --- | --- |
| Asset | no local authored reference provider currently |
| Blueprint | the bound Blueprint's authored state |
| Class | no local authored reference provider currently |
| Graph | the bound Graph only |
| StateTree | the bound StateTree only |
| Widget | the bound WidgetBlueprint's authored state |

Local scope never silently ascends from Graph to Blueprint or expands to
another asset.

`in project` selects a bounded, project-owned authored index:

```sal
query door
references to @health-variable-guid in project
page limit 50
```

It is supported only when the Domain has a complete zero-load or bounded-load
provider. StateTree currently rejects `in project`; loading every StateTree
asset would not satisfy the completeness contract.

## Native Identity

Providers resolve native owner and identity before scanning uses. Examples:

- Blueprint member Variable: owning Blueprint plus `VarGuid`;
- function local: top-level Function Graph Guid plus local `VarGuid`;
- Graph Function or member reference: full native `FMemberReference`;
- SCS Component: owning Blueprint plus `USCS_Node::VariableGuid`;
- Widget: owning WidgetBlueprint plus
  `WidgetVariableNameToGuidMap` Guid;
- StateTree object: the Domain's canonical StableRef and member path.

Names, titles, localized labels, semantic tags, and current result aliases are
not provider identity.

If a native field retains a name but lacks resolvable owner identity, it is
unresolved evidence. It cannot be counted as an exact match.

## Providers

### Blueprint And Graph

Blueprint providers cover authored member and function-local Variables,
Dispatchers, SCS Components, callable Function and Macro declarations,
Blueprint Interface declarations, Custom Events, and other reviewed native
member-reference surfaces.

Graph Nodes expose declaration evidence through fields such as:

- `VariableReference`
- `FunctionReference`
- `MacroGraphReference`
- `DelegateReference`

A compound Node may expose more than one candidate:

```sal
references to @call-node-guid.FunctionReference
references to @call-node-guid.MemberVariableToCallOn
```

Function and Macro declaration Graphs can be reference subjects when UE gives
them declaration identity. Ordinary Event, Construction Script, Collapsed,
Dispatcher Signature, and Timeline Graphs do not become declarations merely
because they carry GraphGuids. A direct Graph `target.InterfaceGuid` subject is
the separate reviewed path for implemented-Interface declaration identity.

### Widget

Widget references use source Widget identity. Stored `FMemberReference`
evidence must resolve to a real generated or Skeleton-Class Property; providers
do not fall back to Widget name or `bIsVariable`.

Widget bindings can contain several facts: destination Widget, destination
Property or Delegate, source Function, and each native source-path segment.
Each match retains its native field or path evidence instead of collapsing the
binding into one guessed target.

### StateTree

StateTree references include:

- Transition and linked-State links;
- State links inside Node or Instance fields;
- explicit Property Bindings;
- automatic Context relationships currently derived by UE;
- required Event and Delegate endpoints;
- outer and nested Property Function Bindings.

Automatic Context relationships are factual derived uses, marked adjacent to
their returned Edge. They are not presented as authored removable Binding
records.

## Query Surface

Reference queries accept only cursor pagination:

```sal
references to <exact-subject> [in project]
page limit <count>
page after "<cursor>"
```

They do not accept `where`, `order by`, `with`, or `depth`. Providers own
stable native traversal order; project cursors retain provider position and
target evidence needed to resume safely.

The declaration itself is excluded from its own use-sites unless the native
authored fact is independently self-referential.

## Result Shape

Results are ordinary ordered Object Text. There is no Reference object or
grouped reference result:

```sal
result exact_target
target door = target {
  domain: blueprint,
  asset: "/Game/BP_Door.BP_Door",
  id: "11111111-1111-1111-1111-111111111111"
}
objects
getHealth = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_VariableGet",
  VariableReference: "<native FMemberReference text>"
}
# match: VariableReference
```

Project results include related canonical Targets needed to locate each page:

The following is a Result Text fragment, not a standalone Result Text document.

```sal
related otherGraph = target {
  domain: graph,
  asset: "/Game/BP_Enemy.BP_Enemy",
  blueprintId: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
  id: "cccccccc-cccc-cccc-cccc-cccccccccccc"
}
objects
match = {
  location: otherGraph::@dddddddd-dddd-dddd-dddd-dddddddddddd,
  member: "VariableReference"
}
```

The scoped StableRef is result-only normalized structure. A following request
copies `otherGraph` as its active Target and uses the unqualified StableRef.
Request text may redundantly use that same active alias as a qualifier, but the
parser lowers it away and rejects every foreign or unknown qualifier.

Every related Target must be canonical, used by Object Text or an explicit
handoff, and structurally deduplicated. A zero-load index entry without enough
verification data remains ordinary evidence:

```sal
{
  assetPath: "/Game/BP_Door.BP_Door",
  indexedNodeGuid: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  exactTargetAvailable: false
}
```

The formatter never invents `BlueprintGuid`, `GraphGuid`, native Class, or
another missing Target field.

## Completeness

A provider must distinguish:

- complete scan with zero matches;
- complete scan with matches;
- unavailable provider;
- incomplete native extractor;
- stale or insufficient index evidence;
- budget or result-size failure.

Unavailable or incomplete extraction returns a diagnostic, not a false
complete zero result.

Project providers may use Asset Registry and Find-in-Blueprints data only to
the extent that the index proves. They must not:

- load an unbounded project to simulate completeness;
- turn a display Graph name into a Graph Target;
- invent owner Guids absent from the index;
- merge same-named local and member declarations;
- claim unsupported native binding fields were scanned.

## Pagination And Revisions

Cursor state binds at least:

- Domain and active exact subject;
- project binding when project scope is used;
- provider and source position;
- stable ordering version;
- source/index revision evidence needed to detect invalidation.

A stale cursor fails explicitly. It does not restart from page one or continue
against a changed declaration.

Each page is independently readable: it carries the canonical main Target and
every related Target required by that page's Object Text.

## Diagnostics

Important failures include:

- `resolution.reference_not_found`
- `resolution.reference_ambiguous`
- `capability.reference_unavailable`
- `validation.reference_scan_incomplete`
- `validation.reference_scan_pending`
- `validation.invalid_cursor`
- `validation.result_too_large`

Ambiguity diagnostics include exact member-path candidates. Identity failure
never falls back to display-name search. Suggestions point to a fresh exact
object read, `with schema`, a local collection, or a canonical Target handoff.
