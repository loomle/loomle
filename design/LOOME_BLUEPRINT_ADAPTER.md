# Loome Blueprint Adapter

## 1. Scope

- Adapter name: `LoomeBlueprintAdapter`
- Role: execute `Loome Graph` blueprint operations inside UE editor runtime.
- Python exposure policy (target): `unreal.LoomeBlueprintAdapter` is the canonical Python entry.
- Compatibility policy: no legacy Python alias is kept.

## 2. Operation Contract

### 2.1 Node Operations

1. `addNode.event`
- Inputs: `eventName`, `eventClassPath`, `position`, `clientRef?`
- Output: `nodeId`

2. `addNode.cast`
- Inputs: `targetClassPath`, `position`, `clientRef?`
- Output: `nodeId`

3. `addNode.callFunction`
- Inputs: `functionClassPath`, `functionName`, `position`, `clientRef?`
- Output: `nodeId`

4. `addNode.branch`
- Inputs: `position`, `clientRef?`
- Output: `nodeId`

5. `addNode.comment`
- Inputs: `text`, `position`, `size`, `color?`, `clientRef?`
- Output: `nodeId`

6. `removeNode`
- Inputs: `nodeId`
- Output: `removed`

7. `moveNode`
- Inputs: `nodeId`, `position`
- Output: `updated`

8. `resizeComment`
- Inputs: `nodeId`, `size`
- Output: `updated`

9. `setNodeComment`
- Inputs: `nodeId`, `comment`
- Output: `updated`

10. `setNodeEnabled`
- Inputs: `nodeId`, `enabled`
- Output: `updated`

### 2.2 Pin/Edge Operations

1. `connectPins`
- Inputs: `from`, `to`
- Output: `connected`

2. `disconnectPins`
- Inputs: `from`, `to`
- Output: `disconnected`

3. `breakPinLinks`
- Inputs: `target`
- Output: `removedLinks`

4. `setPinDefault`
- Inputs: `target`, `value`
- Output: `updated`

### 2.3 Component Operations

1. `addComponent`
- Inputs: `componentClassPath`, `componentName`, `parentComponentName?`
- Output: `componentName`

2. `setComponentProperty`
- Inputs: `componentName`, `property`, `value`
- Output: `updated`

3. `removeComponent`
- Inputs: `componentName`
- Output: `removed`

### 2.4 Build/Runtime Operations

1. `compile`
- Inputs: none
- Output: `compiled`, `status`

2. `spawnActor`
- Inputs: `location`, `rotation`
- Output: `actorPath`

## 3. Batch and Revision

- `ops[]` execute sequentially.
- `continueOnError=false` by default.
- `clientRef` can be referenced as `nodeRef` later in same batch.
- `expectedRevision` mismatch returns `REVISION_CONFLICT`.
- `idempotencyKey` deduplicates identical mutate requests.

## 4. Mapping to Current UE Implementation

Currently implemented (runtime class `LoomeBlueprintAdapter`, target API name `LoomeBlueprintAdapter`):

- `addNode.event` -> `AddEventNode`
- `addNode.cast` -> `AddCastNode`
- `addNode.callFunction` -> `AddCallFunctionNode`
- `connectPins` -> `ConnectPins`
- `setPinDefault` -> `SetPinDefaultValue`
- `compile` -> `CompileBlueprint`
- `spawnActor` -> `SpawnBlueprintActor`
- `addComponent` -> `AddComponent`
- query helpers -> `ListEventGraphNodes`, `GetNodeDetails`, `FindNodesByClass`

Planned to add:

- `addNode.branch`
- `addNode.comment`
- `removeNode`
- `moveNode`
- `resizeComment`
- `disconnectPins`
- `breakPinLinks`
- `setNodeComment`
- `setNodeEnabled`
- `setComponentProperty`
- `removeComponent`

## 5. Adapter Output Shape

Per-op result:

```json
{
  "index": 0,
  "op": "connectPins",
  "ok": true,
  "nodeId": "",
  "changed": true,
  "error": ""
}
```

## 6. Constraints

- Adapter must never emit non-deterministic ids for the same already-existing object lookup.
- Adapter must normalize pin name aliases before connect/disconnect.
- Adapter must return explicit error code + message for every failed op.
- Adapter must not compile implicitly unless `compile` op exists.
- Python direct calls must expose the same operation semantics as `graph.mutate`.
