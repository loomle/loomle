# LGL Text Format

This document defines the current accepted Loomle Graph Lang text forms. It is
limited to syntax covered by the parser, formatter, and Blueprint example
conformance tests.

LGL text is self-describing. The document header carries the operation, graph
domain, asset, graph reference, and optional dry-run intent.

## Document Headers

```txt
graph blueprint("/Game/BP_Door"/EventGraph)
query blueprint("/Game/BP_Door"/EventGraph)
patch blueprint("/Game/BP_Door"/EventGraph)
palette blueprint("/Game/BP_Door"/EventGraph)
```

Graph ids are scoped by asset path:

```txt
query blueprint("/Game/BP_Door"/id("graph-id"))
```

Patch dry run is part of the text:

```txt
patch blueprint("/Game/BP_Door"/EventGraph) dry run
```

## Graph Documents

Graph documents describe graph snapshots or snippets.

```txt
graph blueprint("/Game/BP_Door"/EventGraph)

begin@A001: EventBeginPlay()
delay@A002: Delay({Duration: 1.0}) {at: [320, 0], size: [200, 100]}
print@A003: PrintString({InString: "Ready"})

begin.Then -> delay.Exec/Completed -> print.Exec
```

Node lines use:

```txt
alias@id: Type()
alias@id: Type({Field: value})
alias@id: Type({Field: value}) {at: [x, y], size: [w, h]}
```

`alias` is the local LGL node name. `id` is the target graph's stable node
identity when available. `alias@id` is a declaration/readback form. Patch
operations refer to nodes by `alias` or by id references such as `@A001` when an
adapter supports them.

Graph documents do not include palette bindings. Existing graph nodes have
already been created.

## Pin Details

Pin lines appear in graph documents when detailed readback is requested.

```txt
branch.Exec: exec in
branch.Condition: bool in
delay.Duration: float in {1.0, anchor: [320, 72]}
```

The accepted shape is:

```txt
node.pin: type in
node.pin: type out
node.pin: type in {default}
node.pin: type in {anchor: [x, y]}
node.pin: type in {default, anchor: [x, y]}
```

The leading unnamed value inside `{...}` is the pin value. `anchor` is readback
layout metadata. Pin anchors are not patch mutation targets in the current
language.

Common readable pin types include:

```txt
exec
bool
int
float
string
name
text
vector
rotator
transform
object<Actor>
class<Actor>
array<vector>
enum<SpawnActorCollisionHandlingMethod>
```

## Edges

Edges always name pins explicitly.

```txt
begin.Then -> delay.Exec
delay.Completed -> print.Exec
health.Value -> branch.Condition
```

Linear paths may use a pin chain. A middle `input/output` segment names one
input and one output pin on the same node:

```txt
begin.Then -> delay.Exec/Completed -> print.Exec
```

This expands to:

```txt
begin.Then -> delay.Exec
delay.Completed -> print.Exec
```

Implicit node chains such as `begin -> delay -> print` are not accepted.

## Values

Values are JSON-like literals plus unquoted names.

```txt
null
true
false
1.0
"Ready"
BP_Projectile
[0, 0]
{Duration: 1.0, InString: "Ready"}
```

Quoted values are strings. Unquoted symbols become LGL names and are resolved by
adapters.

Node and palette calls use named object arguments:

```txt
Delay({Duration: 1.0})
PrintString({InString: "Ready"})
palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})
```

Positional node arguments are not accepted.

## Palette Documents

Palette documents return creation entries that patch documents can bind.

```txt
palette blueprint("/Game/BP_Door"/EventGraph)

PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString", title: "Print String"})
```

Patch documents use the returned stable id:

```txt
PrintString = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.PrintString"})
```

Palette lookup semantics belong to adapters.
