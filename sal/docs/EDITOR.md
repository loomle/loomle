# Editor Tool Design

## Status And Intent

`editor` observes or controls the UE Editor presentation of an exact Blueprint
or Blueprint Graph. It is a small semantic interface over UE's native Blueprint
Editor and document APIs, not a generic Slate window manager.

The public operations are:

- `context`: observe the user's current meaningful Editor interaction;
- `open`: ensure the requested Blueprint or Graph is open and focused;
- `close`: ensure the requested Blueprint Editor or Graph document is closed.

`editor` is the sole public Editor tool. Context observation is its default
operation rather than a second public tool.

## Public Input

```ts
interface EditorInput {
  operation?: "context" | "open" | "close";
  target?: string;
}
```

The accepted combinations are:

```text
editor({})
editor({ operation: "context" })
editor({ operation: "open", target: "target { ... }" })
editor({ operation: "close", target: "target { ... }" })
```

An empty object defaults to `context`. `context` rejects `target`; `open` and
`close` require it. A Target without an operation is invalid rather than an
implicit open.

`open` and `close` do not expose `dryRun`. They change transient Editor
presentation, not authored UObject or Asset state, and their contract is an
idempotent requested postcondition. Native close confirmation is never bypassed.

The tool annotations are `readOnlyHint: false`, `destructiveHint: false`, and
`idempotentHint: true` because one schema includes both observation and
presentation-changing operations.

## Target Text

The public `target` value is exactly one bare canonical SAL Target expression.
It is not a Target binding, Query, Patch, Object Text, or Result Text.

Blueprint:

```sal
target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "11111111-2222-3333-4444-555555555555"
}
```

Graph:

```sal
target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-2222-3333-4444-555555555555",
  id: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
}
```

The first version accepts only canonical Blueprint and Graph Targets. It does
not discover by name or path, accept aliases, or control Asset, Class,
StateTree, Widget, arbitrary Dock Tab, Modal, or OS Window Targets.

SAL Target Text is the public identity spelling. The Client parses and
canonicalizes it, then sends the corresponding normalized Target JSON through
the private Bridge transport. JSON is not a second public Target syntax.

## Operation Semantics

A Blueprint Target denotes the whole Blueprint Editor. A Graph Target denotes
the exact Graph document inside its owning Blueprint Editor.

`open` means ensure-open-and-focus:

- Blueprint uses the native Asset Editor open/focus path;
- Graph first ensures one owning Blueprint Editor, then uses
  `FBlueprintEditor::OpenGraphAndBringToFront(Graph, true)`;
- focus is established through the native editor/document path, never by
  setting keyboard focus on the outer `SGraphEditor`.

`close` means ensure-closed:

- Blueprint requests native close on the one editor presenting the Blueprint;
- Graph requests native document-tab close for the exact Graph;
- no live presentation is a successful `already_closed` result;
- more than one live matching presentation is ambiguous and is not reduced to
  the first one;
- native close vetoes, confirmations, and Modal state are not bypassed.

An operation is successful only after its requested presentation postcondition
is observed. UE open/close return values alone are not proof. Any private
cross-tick operation or polling needed for that verification remains hidden
behind the single public tool call.

## Public Result

The public result has at most three independent MCP text blocks:

1. validated canonical SAL Result Text;
2. Editor outcome metadata for `open` and `close`;
3. SAL diagnostics when present.

`context` returns the ordinary context Result Text and does not add metadata
merely to restate that observation occurred.

Successful Graph open example, first block:

```sal
result exact_target
target editorTarget = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "11111111-2222-3333-4444-555555555555",
  id: "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
}
no_objects
```

Second block:

```text
###
Editor result
operation: open
status: opened
###
```

The terminal status set is deliberately small:

- open: `opened`, `focused`, `already_focused`, or `failed`;
- close: `closed`, `already_closed`, or `failed`.

The status describes presentation only. It does not use mutation metadata such
as `dryRun`, `valid`, `applied`, revisions, plans, or diffs. Detailed failure
reason and recovery guidance belong to registered diagnostics.

The main Target always remains the requested content identity, never a window
or tab identity. Closing a Graph does not delete it, so a verified close still
returns `exact_target`. The tool does not substitute whichever surface receives
focus after close; callers use `editor({})` to observe the new context.

Only Target parse or resolution failure returns `unresolved_target`. Once the
content Target resolves, a Modal block, ambiguous presentation, close veto, or
failed postcondition retains `exact_target`, returns `status: failed`, includes
an error diagnostic, and sets the MCP result `isError` flag.

## Private Bridge Result

Editor control returns a wrapper rather than adding UI fields to the closed SAL
`ObjectResult` schema:

```ts
interface EditorControlResult {
  subject: ObjectResult;
  outcome: {
    operation: "open" | "close";
    status:
      | "opened"
      | "focused"
      | "already_focused"
      | "closed"
      | "already_closed"
      | "failed";
  };
}
```

The Client validates and formats `subject` through the ordinary SAL result
path, then formats `outcome` separately. Private operation IDs, native pointers,
Tab IDs, and polling phases never enter SAL Target Text or the public result.
The current private transport endpoints are `editor.open` and `editor.close`;
`editor.context` remains the observation endpoint.

## Error Boundaries

- Invalid public argument combinations use the Client's ordinary invalid-tool-
  argument error path.
- Invalid or non-canonical Target Text returns unresolved SAL Result Text plus
  a language diagnostic without calling the Bridge.
- Missing Blueprint/Graph identity returns unresolved Target diagnostics.
- Multiple matching editor/document presentations return
  `resolution.editor_presentation_ambiguous` and perform no close or arbitrary
  focus choice.
- An active Modal returns `runtime.editor_blocked_by_modal`. Loomle does not
  accept, discard, or force-close arbitrary native dialogs.
- Native open failure uses `validation.editor_open_failed`; a close veto uses
  `validation.editor_close_vetoed`; and an unverified postcondition uses
  `validation.editor_verification_failed`. A request is never reported as
  complete merely because a UE request API returned true.
