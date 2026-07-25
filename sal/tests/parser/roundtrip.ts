import assert from "node:assert/strict";
import {
  formatObjectResultText,
  formatSalObject,
  parseSalObject,
  parseSalResultText,
  validateObjectResult,
  validateSalObject,
  type ObjectResult,
} from "../../src/index.js";

const blueprintId = "11111111-1111-1111-1111-111111111111";
const graphId = "22222222-2222-2222-2222-222222222222";

const documents = [
  `catalog = target {domain: asset}

query catalog
assets "door"
with schema
page limit 10`,
  `door = target {domain: asset, path: "/Game/Doors/BP_Door.BP_Door"}

query door
summary`,
  `bp = target {domain: blueprint, asset: "/Game/Doors/BP_Door.BP_Door", id: "${blueprintId}"}

query bp
graphs`,
  `actor = target {domain: class, path: "/Script/Engine.Actor"}

query actor
properties`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
node @N1`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
target`,
  `behavior = target {domain: state_tree, asset: "/Game/AI/ST_Omle.ST_Omle", type: "/Script/StateTreeModule.StateTree"}

query behavior
tree @ROOT depth 4`,
  `menu = target {domain: widget, asset: "/Game/UI/WBP_Menu.WBP_Menu", id: "33333333-3333-3333-3333-333333333333"}

query menu
widgets`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
references to target.GeneratedClass in project`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
exec flow from pin @NODE/THEN depth 3`,
  `g = target {
  domain: graph,
  asset: "/Game/Doors/BP_Door.BP_Door",
  blueprintId: "${blueprintId}",
  id: "${graphId}"
}

query g
context g::@N1`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
palette entries "Branch" from pin @INPUT`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
palette entries "Branch" to @NODE/INPUT`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
component @N1`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
nodes @N2`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

query g
references @N3`,
  `behavior = target {domain: state_tree, asset: "/Game/AI/ST_Omle.ST_Omle", type: "/Script/StateTreeModule.StateTree"}

query behavior
palette entries "Task" to @STATE/tasks`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

patch g dry run
delay = node {palette: "Delay", Duration: 1, "editor-only": {enabled: true}}
add delay
connect @P1 -> delay.execute
insert @SOURCE/OUT -> @DELAY/IN / @DELAY/OUT -> @TARGET/IN
set @N2.NodeComment = "Guard"
move node @N2 by (160, -40)
invoke node @N2 AddPin(options: {count: 1}) as subpins.X: extraPin
replace @"old with quoted delimiter" with @"new -> quoted delimiter"
compile
save`,
  `g = target {domain: graph, asset: "/Game/Doors/BP_Door.BP_Door", blueprintId: "${blueprintId}", id: "${graphId}"}

patch g dry run
move @N1 by (-0, 1)
move @N2 to (1, -0)`,
  `# ordered graph fragment
begin = node {id: "N1", metadata: {"display name": "Begin Play"}}
begin.Then = pin {id: "P1", direction: out}
task = {id: "N2", nested: [{value: 1}, null]}
targetValue = targeted {id: "N3"}
@"source -> pin" -> @"target, pin"`,
];

for (const text of documents) {
  const parsed = parseSalObject(text);
  assert.deepEqual(parsed.diagnostics, [], text);
  assert.ok(parsed.object, text);
  assert.equal(await validateSalObject(parsed.object), undefined, text);

  const formatted = formatSalObject(parsed.object);
  const reparsed = parseSalObject(formatted);
  assert.deepEqual(reparsed.diagnostics, [], formatted);
  assert.deepEqual(reparsed.object, parsed.object, formatted);
}

const prototypeKeyObject = parseSalObject(
  `payload = {"__proto__": {polluted: true}, kind: "ordinary-data"}`,
);
assert.deepEqual(prototypeKeyObject.diagnostics, []);
assert.ok(prototypeKeyObject.object);
assert.equal(
  JSON.stringify(prototypeKeyObject.object).includes('"__proto__"'),
  true,
);
assert.equal(
  formatSalObject(prototypeKeyObject.object).includes("__proto__:"),
  true,
);
assert.equal(
  ({} as { polluted?: boolean }).polluted,
  undefined,
);

const negativeZeroObject = parseSalObject(`payload = {n: -0}`);
assert.deepEqual(negativeZeroObject.diagnostics, []);
assert.ok(negativeZeroObject.object);
const negativeZeroText = formatSalObject(negativeZeroObject.object);
assert.equal(negativeZeroText.includes("n: -0"), true);
const reparsedNegativeZero = parseSalObject(negativeZeroText);
assert.deepEqual(reparsedNegativeZero.diagnostics, []);
assert.deepEqual(reparsedNegativeZero.object, negativeZeroObject.object);

const sharedSubobject = parseSalObject(`payload = {a: {n: 1}, b: {n: 2}}`);
assert.ok(sharedSubobject.object && !("kind" in sharedSubobject.object));
if (sharedSubobject.object && !("kind" in sharedSubobject.object)) {
  const binding = sharedSubobject.object.statements[0];
  assert.ok(binding && "value" in binding && typeof binding.value === "object" && binding.value !== null && "fields" in binding.value);
  if (binding && "value" in binding && typeof binding.value === "object" && binding.value !== null && "fields" in binding.value) {
    binding.value.fields.b = binding.value.fields.a;
  }
  assert.equal(await validateSalObject(sharedSubobject.object), undefined);
}

const nonFiniteObject = parseSalObject(`payload = {n: 0}`);
assert.ok(nonFiniteObject.object && !("kind" in nonFiniteObject.object));
if (nonFiniteObject.object && !("kind" in nonFiniteObject.object)) {
  const binding = nonFiniteObject.object.statements[0];
  assert.ok(binding && "value" in binding && typeof binding.value === "object" && binding.value !== null);
  if (binding && "value" in binding && typeof binding.value === "object" && binding.value !== null && "fields" in binding.value) {
    binding.value.fields.n = Number.POSITIVE_INFINITY;
  }
  assert.equal((await validateSalObject(nonFiniteObject.object))?.code, "language.invalid_object_shape");
  assert.throws(() => formatSalObject(nonFiniteObject.object!), /non-finite number/);
}

const uppercase = parseSalObject(
  `g = target {domain: graph, asset: "/Game/BP.BP", blueprintId: "AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA", id: "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"}
query g`,
);
assert.deepEqual(uppercase.diagnostics, []);
assert.ok(uppercase.object && "kind" in uppercase.object && uppercase.object.kind === "query");
if (uppercase.object && "kind" in uppercase.object && uppercase.object.kind === "query") {
  assert.equal(uppercase.object.target.target.domain, "graph");
  if (uppercase.object.target.target.domain === "graph") {
    assert.equal(uppercase.object.target.target.blueprintId, "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    assert.equal("id" in uppercase.object.target.target && uppercase.object.target.target.id, "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
  }
}

const result: ObjectResult = {
  targetContext: "exact_target",
  target: {
    alias: "g",
    target: {
      kind: "target",
      domain: "graph",
      asset: "/Game/Doors/BP_Door.BP_Door",
      blueprintId,
      id: graphId,
    },
  },
  relatedTargets: [{
    alias: "bp",
    target: {
      kind: "target",
      domain: "blueprint",
      asset: "/Game/Doors/BP_Door.BP_Door",
      id: blueprintId,
    },
  }],
  handoffs: [{
    kind: "target_handoff",
    purpose: "compile owner",
    target: { kind: "local", name: "bp" },
  }],
  object: {
    statements: [{
      target: { kind: "local", name: "call" },
      value: {
        kind: "object",
        semanticTag: "node",
        fields: {
          id: "N1",
          owner: {
            kind: "scoped_stable_ref",
            target: { kind: "local", name: "bp" },
            reference: { kind: "stable_ref", identityPath: ["DECL"] },
          },
        },
      },
    }],
  },
  diagnostics: [],
};
assert.equal(await validateObjectResult(result), undefined);
const nonFiniteResult = structuredClone(result);
assert.ok(nonFiniteResult.object);
if (nonFiniteResult.object) {
  const binding = nonFiniteResult.object.statements[0];
  assert.ok(binding && "value" in binding && typeof binding.value === "object" && binding.value !== null && "fields" in binding.value);
  if (binding && "value" in binding && typeof binding.value === "object" && binding.value !== null && "fields" in binding.value) {
    binding.value.fields.count = Number.NEGATIVE_INFINITY;
  }
}
assert.equal((await validateObjectResult(nonFiniteResult))?.code, "language.invalid_result_shape");
const resultText = formatObjectResultText(result);
const parsedResult = parseSalResultText(resultText);
assert.deepEqual(parsedResult.diagnostics, []);
assert.ok(parsedResult.result);
assert.equal(parsedResult.result?.targetContext, result.targetContext);
assert.deepEqual(parsedResult.result && "target" in parsedResult.result ? parsedResult.result.target : undefined, result.target);
assert.deepEqual(parsedResult.result?.object, result.object);

console.log(`roundtrip cases passed: ${documents.length}`);
