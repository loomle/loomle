# class

Inspect UE Class Reflection and effective Class Defaults. Reflection is
read-only; supported Blueprint Generated Class Defaults may be edited through
their durable Blueprint source.

## Target

```sal
actorClass = target {
  domain: class,
  path: "/Script/Engine.Actor"
}

doorClass = target {
  domain: class,
  path: "/Game/BP_Door.BP_Door_C"
}
```

The exact native Class Path is complete Target identity. Class currently
defines no contained-object StableRefs and no Palette.

## Query

```sal
target
summary
properties ["text"]
property <name>
functions ["text"]
function <name>
defaults ["text"]
default <name>
```

Plural operations return the effective inherited view and use cursor
pagination. `defaults` alone supports:

```sal
where overridden = true
```

Singular names are exact selectors inside the bound Class. Exact Property,
Function, and Default reads may use `with schema`.

```sal
query doorClass
property Health
with schema
```

Property and Function results retain exact native owner Paths as ordinary
data:

```sal
health = {
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
}
```

Class does not invent StableRefs from those Paths.

## Defaults

An exact Default returns compact Property data before the value:

```sal
result exact_target
target doorClass = target {
  domain: class,
  path: "/Game/BP_Door.BP_Door_C"
}
objects
health = {
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
}
doorClass.Health = "150.000000"
# value: local override
# source: /Game/BP_Door.BP_Door
```

Values are complete native UE `ExportText` strings. A native fixed array uses
an SAL array containing exactly one native string per fixed element; a dynamic
`FArrayProperty` remains one native string.

## Patch

Only Blueprint Generated Classes with durable source ownership may edit
ordinary or Sparse Defaults:

```sal
patch doorClass [dry run]
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

`set` establishes a local override. `reset` resumes inheritance or restores a
Property introduced by the current Class. Exact Default schema controls
writability, constraints, source, and reset behavior.

Native Classes, Config values, Component Templates, default subobjects,
Reflection declarations, and Metadata are read-only. Defaults Patch does not
accept nested value paths, creation bindings, Palette, or `invoke`.

## Save And Handoff

```sal
patch doorClass
save
```

Save resolves `ClassGeneratedBy` and persists the source Blueprint Package.
Class defines no `compile`. When compilation is required, the result supplies
an independent Blueprint Target and `handoff compile to <alias>`.
