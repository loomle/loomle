import assert from "node:assert/strict";
import {
  formatSalObject,
  parseSalObject,
  validateSalObject,
} from "../../src/index.js";

const legacyRequest = `bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
g = graph(asset: bp, id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB")
query g
context pin@AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA/BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB`;

const strict = parseSalObject(legacyRequest);
assert.equal(strict.object, undefined);
assert.equal(strict.diagnostics[0]?.code, "language.invalid_target_binding");

const compatible = parseSalObject(legacyRequest, { compatibility: "legacy" });
assert.deepEqual(compatible.diagnostics, []);
assert.ok(compatible.object && "kind" in compatible.object && compatible.object.kind === "query");
if (compatible.object && "kind" in compatible.object && compatible.object.kind === "query") {
  assert.deepEqual(compatible.object.target, {
    alias: "g",
    target: {
      kind: "target",
      domain: "graph",
      asset: "/Game/BP_Test.BP_Test",
      blueprintId: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
      id: "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
    },
  });
  assert.deepEqual(compatible.object.operation, {
    kind: "context",
    target: {
      kind: "stable_ref",
      identityPath: [
        "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
      ],
      semanticTag: "pin",
    },
  });
}

const legacyObject = parseSalObject(
  `value = node(id: "N1", child: pin(id: "P1"))
parameter@AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA/BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB[0] -> node@CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC.Instance`,
  { compatibility: "legacy", legacyDomain: "state_tree" },
);
assert.deepEqual(legacyObject.diagnostics, []);
assert.ok(legacyObject.object);
assert.equal(JSON.stringify(legacyObject.object).includes("legacy_call"), false);
assert.equal(JSON.stringify(legacyObject.object).includes('"kind":"call"'), false);
if (legacyObject.object) {
  assert.equal(await validateSalObject(legacyObject.object), undefined);
  const canonical = formatSalObject(legacyObject.object);
  const strictReparse = parseSalObject(canonical);
  assert.deepEqual(strictReparse.diagnostics, []);
  assert.deepEqual(strictReparse.object, legacyObject.object);
}

const strictObject = parseSalObject("value = node(id: \"N1\")");
assert.equal(strictObject.object, undefined);
assert.equal(strictObject.diagnostics[0]?.code, "language.legacy_constructor_requires_compatibility");

const universalObject = parseSalObject(
  `value = object(id: "N1", __proto__: object(enabled: true))`,
  { compatibility: "legacy" },
);
assert.deepEqual(universalObject.diagnostics, []);
assert.ok(universalObject.object);
assert.equal(JSON.stringify(universalObject.object).includes('"semanticTag":"object"'), false);
assert.equal(JSON.stringify(universalObject.object).includes('"__proto__"'), true);

for (const constructor of ["graph", "true"]) {
  const unsafeConstructor = parseSalObject(
    `value = ${constructor}(id: "N1")`,
    { compatibility: "legacy" },
  );
  assert.equal(unsafeConstructor.object, undefined);
  assert.equal(
    unsafeConstructor.diagnostics[0]?.code,
    "language.unsafe_legacy_constructor",
  );
}

const ambiguousBlueprint = parseSalObject(
  `bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
query bp`,
  { compatibility: "legacy" },
);
assert.equal(ambiguousBlueprint.object, undefined);
assert.equal(ambiguousBlueprint.diagnostics[0]?.code, "language.unsafe_legacy_target");

const explicitWidget = parseSalObject(
  `menu = blueprint(asset: "/Game/WBP_Menu.WBP_Menu", id: "CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC")
query menu`,
  { compatibility: "legacy", legacyDomain: "widget" },
);
assert.deepEqual(explicitWidget.diagnostics, []);
assert.ok(explicitWidget.object && "kind" in explicitWidget.object && explicitWidget.object.kind === "query");
if (explicitWidget.object && "kind" in explicitWidget.object && explicitWidget.object.kind === "query") {
  assert.equal(explicitWidget.object.target.target.domain, "widget");
}

const graphWithLegacyType = parseSalObject(
  `bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
g = graph(asset: bp, id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB", type: GT_Ubergraph)
query g`,
  { compatibility: "legacy", legacyDomain: "graph" },
);
assert.ok(
  graphWithLegacyType.object === undefined,
);
assert.equal(graphWithLegacyType.diagnostics.length, 1);
assert.equal(
  graphWithLegacyType.diagnostics[0]?.code,
  "language.unsafe_legacy_target",
);

const unknownLegacyTargetField = parseSalObject(
  `bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA", typo: 1)
query bp`,
  { compatibility: "legacy", legacyDomain: "blueprint" },
);
assert.equal(unknownLegacyTargetField.object, undefined);
assert.equal(
  unknownLegacyTargetField.diagnostics[0]?.code,
  "language.unsafe_legacy_target",
);

const discardedOwnerAssertion = parseSalObject(
  `assetValue = asset(path: "/Game/BP_Test.BP_Test", type: "/Script/Engine.Blueprint")
bp = blueprint(asset: assetValue, id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
query bp`,
  { compatibility: "legacy", legacyDomain: "blueprint" },
);
assert.equal(discardedOwnerAssertion.object, undefined);
assert.equal(
  discardedOwnerAssertion.diagnostics[0]?.code,
  "language.unsafe_legacy_target",
);

const invalidOptionalLegacyField = parseSalObject(
  `bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: 1)
query bp`,
  { compatibility: "legacy", legacyDomain: "blueprint" },
);
assert.equal(invalidOptionalLegacyField.object, undefined);
assert.equal(
  invalidOptionalLegacyField.diagnostics[0]?.code,
  "language.unsafe_legacy_target",
);

const unusedLegacyTarget = parseSalObject(
  `actor = class(path: "/Script/Engine.Actor")
bp = blueprint(asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA")
g = graph(asset: bp, id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB")
query g`,
  { compatibility: "legacy", legacyDomain: "graph" },
);
assert.equal(unusedLegacyTarget.object, undefined);
assert.equal(
  unusedLegacyTarget.diagnostics[0]?.code,
  "language.unsafe_legacy_target",
);

const mixedExplicitAndLegacyTargets = parseSalObject(
  `actor = target {domain: class, path: "/Script/Engine.Actor"}
g = graph(asset: "/Game/BP_Test.BP_Test", id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB")
query g`,
  { compatibility: "legacy", legacyDomain: "graph" },
);
assert.equal(mixedExplicitAndLegacyTargets.object, undefined);
assert.equal(
  mixedExplicitAndLegacyTargets.diagnostics[0]?.code,
  "language.multiple_request_targets",
);

const unsafeUnderScopedPin = parseSalObject(
  `g = target { domain: graph, asset: "/Game/BP_Test.BP_Test", id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB" }
query g
context pin@AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA`,
  { compatibility: "legacy" },
);
assert.equal(unsafeUnderScopedPin.object, undefined);
assert.equal(
  unsafeUnderScopedPin.diagnostics[0]?.code,
  "language.unsafe_legacy_reference",
);

const unsafeRootSelf = parseSalObject(
  `g = target { domain: graph, asset: "/Game/BP_Test.BP_Test", id: "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB" }
query g
context graph@BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB`,
  { compatibility: "legacy" },
);
assert.equal(unsafeRootSelf.object, undefined);
assert.equal(
  unsafeRootSelf.diagnostics[0]?.code,
  "language.unsafe_legacy_reference",
);

const safeBlueprintGraph = parseSalObject(
  `bp = target { domain: blueprint, asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA" }
query bp
context graph@BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB`,
  { compatibility: "legacy" },
);
assert.deepEqual(safeBlueprintGraph.diagnostics, []);
assert.ok(safeBlueprintGraph.object && "kind" in safeBlueprintGraph.object && safeBlueprintGraph.object.kind === "query");
if (safeBlueprintGraph.object && "kind" in safeBlueprintGraph.object && safeBlueprintGraph.object.kind === "query") {
  assert.deepEqual(safeBlueprintGraph.object.operation, {
    kind: "context",
    target: {
      kind: "stable_ref",
      identityPath: ["bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"],
    },
  });
}

const safeWidget = parseSalObject(
  `menu = target { domain: widget, asset: "/Game/WBP_Menu.WBP_Menu", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA" }
query menu
context widget@BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB`,
  { compatibility: "legacy" },
);
assert.deepEqual(safeWidget.diagnostics, []);
assert.ok(safeWidget.object && "kind" in safeWidget.object && safeWidget.object.kind === "query");
if (safeWidget.object && "kind" in safeWidget.object && safeWidget.object.kind === "query") {
  assert.deepEqual(safeWidget.object.operation, {
    kind: "context",
    target: {
      kind: "stable_ref",
      identityPath: ["bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"],
    },
  });
}

const safeGraphLocalVariable = parseSalObject(
  `g = target { domain: graph, asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA" }
query g
context variable@BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB/CCCCCCCC-CCCC-CCCC-CCCC-CCCCCCCCCCCC`,
  { compatibility: "legacy" },
);
assert.deepEqual(safeGraphLocalVariable.diagnostics, []);

for (const unsafe of [
  `assets = target { domain: asset, path: "/Game/BP_Test.BP_Test" }
query assets
context node@AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA`,
  `actor = target { domain: class, path: "/Script/Engine.Actor" }
query actor
context node@AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA`,
  `bp = target { domain: blueprint, asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA" }
query bp
context variable@BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB`,
  `g = target { domain: graph, asset: "/Game/BP_Test.BP_Test", id: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA" }
query g
context node@00000000-0000-0000-0000-000000000000`,
]) {
  const parsed = parseSalObject(unsafe, { compatibility: "legacy" });
  assert.equal(parsed.object, undefined);
  assert.equal(
    parsed.diagnostics[0]?.code,
    "language.unsafe_legacy_reference",
  );
}

console.log("legacy compatibility lowering passed");
