---
layout: default
title: Class
parent: Interfaces
nav_order: 3
---

# Class

The Class interface reads UE reflection and effective Class Defaults. Bind the
exact native Class Path:

```text
actorClass = class(path: "/Script/Engine.Actor")
doorClass = class(path: "/Game/Blueprints/BP_Door.BP_Door_C")
```

Available queries are:

```text
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

```text
doorClass = class(path: "/Game/Blueprints/BP_Door.BP_Door_C")

query doorClass
default Health
with schema
```

Default values use complete native UE `ExportText` strings. Source, inheritance,
and storage are comments rather than invented objects.

Only Blueprint Generated Classes with durable source ownership can edit
ordinary or Sparse Class Defaults:

```text
doorClass = class(path: "/Game/Blueprints/BP_Door.BP_Door_C")

patch doorClass dry run
set doorClass.Health = "150.000000"
reset doorClass.NetUpdateFrequency
```

Native `/Script/...` Classes, Reflection declarations, Metadata, Config values,
Component Templates, and default subobjects are read-only. Compile through the
source Blueprint; a changed Generated Class may use a separate save Patch.
