---
layout: default
title: Class
parent: Interfaces
nav_order: 3
---

# Class

The Class interface reads UE reflection and effective Class Defaults. Bind the
exact native Class Path:

```sal
actorClass = target {
  domain: class,
  path: "/Script/Engine.Actor"
}

doorClass = target {
  domain: class,
  path: "/Game/Blueprints/BP_Door.BP_Door_C"
}
```

Class has no Target GUID field; the exact native Class Path is its complete
identity.

Available queries are:

```sal
summary
properties
property <name>
functions
function <name>
defaults
default <name>
```

Plural queries include the effective inherited view; bind `SuperClass`
explicitly to inspect a hidden parent declaration.

```sal
doorClass = target {
  domain: class,
  path: "/Game/Blueprints/BP_Door.BP_Door_C"
}

query doorClass
default Health
with schema
```

Default values use complete native UE `ExportText` strings. Source, inheritance,
and storage are comments rather than invented objects. A public result is a
complete Result Text envelope:

```sal
result exact_target
target doorClass = target {
  domain: class,
  path: "/Game/Blueprints/BP_Door.BP_Door_C"
}
objects
health = {
  path: "/Script/Game.DoorBase:Health",
  type: "FloatProperty"
}
doorClass.Health = "150.000000"
# value: local override
# source: /Game/Blueprints/BP_Door.BP_Door
```

Only Blueprint Generated Classes with durable source ownership can edit
ordinary or Sparse Class Defaults:

```sal
doorClass = target {
  domain: class,
  path: "/Game/Blueprints/BP_Door.BP_Door_C"
}

patch doorClass dry run
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

Native `/Script/...` Classes, Reflection declarations, Metadata, Config values,
Component Templates, and default subobjects are read-only. Compile through the
source Blueprint. When compilation is required, copy the independent related
Blueprint Target named by the Class result's compile handoff into a new
request. A changed Generated Class may use a separate save Patch.
