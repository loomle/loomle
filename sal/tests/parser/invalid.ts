import assert from "node:assert/strict";
import { parseSalObject, parseSalResultText } from "../../src/index.js";

const graphTarget = `g = target {domain: graph, asset: "/Game/BP_Test.BP_Test", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}`;

const cases: Array<{ name: string; text: string; code: string }> = [
  {
    name: "missing target",
    text: "query g\nsummary",
    code: "language.unknown_target",
  },
  {
    name: "multiple request targets",
    text: `a = target {domain: asset}\nb = target {domain: asset}\nquery a`,
    code: "language.multiple_request_targets",
  },
  {
    name: "nested target value",
    text: `${graphTarget}\npatch g\nvalue = {nested: target {domain: asset}}`,
    code: "language.target_not_object_expression",
  },
  {
    name: "reserved semantic tag",
    text: "value = graph {id: \"G\"}",
    code: "language.reserved_semantic_tag",
  },
  {
    name: "new Domain reserved semantic tag",
    text: "value = pcg_component {id: \"C\"}",
    code: "language.reserved_semantic_tag",
  },
  {
    name: "operation keyword semantic tag",
    text: "value = tree {id: \"T\"}",
    code: "language.reserved_semantic_tag",
  },
  {
    name: "universal object keyword object tag",
    text: "value = object {id: \"O\"}",
    code: "language.reserved_semantic_tag",
  },
  {
    name: "universal object keyword stable reference tag",
    text: `${graphTarget}\nquery g\ncontext object @11111111-1111-1111-1111-111111111111`,
    code: "language.reserved_semantic_tag",
  },
  {
    name: "invalid target guid",
    text: "g = target {domain: graph, asset: \"/Game/BP.BP\", id: \"not-a-guid\"}\nquery g",
    code: "language.invalid_target_guid",
  },
  {
    name: "zero target guid",
    text: "g = target {domain: graph, asset: \"/Game/BP.BP\", id: \"00000000-0000-0000-0000-000000000000\"}\nquery g",
    code: "language.invalid_target_guid",
  },
  {
    name: "unknown target field",
    text: "g = target {domain: graph, asset: \"/Game/BP.BP\", name: \"EventGraph\", owner: \"bp\"}\nquery g",
    code: "language.unknown_target_field",
  },
  {
    name: "incomplete patch target",
    text: "g = target {domain: graph, asset: \"/Game/BP.BP\", name: \"EventGraph\"}\npatch g\nsave",
    code: "language.incomplete_patch_target",
  },
  {
    name: "pcg component source is closed",
    text: "c = target {domain: pcg_component, asset: \"/Game/Maps/Arena.Arena\", actorId: \"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa\", source: \"runtime\", id: \"PCGComponent\", type: \"/Script/PCG.PCGComponent\"}\nquery c",
    code: "language.invalid_target_value",
  },
  {
    name: "level palette from destination requires a stable reference",
    text: "arena = target {domain: level, asset: \"/Game/Maps/Arena.Arena\"}\nquery arena\npalette entries \"Static Mesh Actor\" from arena.Actors",
    code: "language.expected_stable_reference",
  },
  {
    name: "level palette destination names an unknown local",
    text: "arena = target {domain: level, asset: \"/Game/Maps/Arena.Arena\"}\nquery arena\npalette entries \"Static Mesh Actor\" to missing.Actors",
    code: "language.unknown_local_reference",
  },
  {
    name: "scoped request reference",
    text: `${graphTarget}\nquery g\ncontext bp::@N`,
    code: "language.invalid_reference",
  },
  {
    name: "local references query",
    text: `${graphTarget}\nquery g\nreferences to g.FunctionReference`,
    code: "language.expected_stable_reference",
  },
  {
    name: "malformed identity path",
    text: `${graphTarget}\nquery g\ncontext @OWNER/`,
    code: "language.invalid_reference",
  },
  {
    name: "negative member index",
    text: `${graphTarget}\npatch g\nset @TASK.Values[-1] = 1`,
    code: "language.invalid_reference",
  },
  {
    name: "decimal member index",
    text: `${graphTarget}\npatch g\nset @TASK.Values[1.5] = 1`,
    code: "language.invalid_reference",
  },
  {
    name: "overflow member index",
    text: `${graphTarget}\npatch g\nset @TASK.Values[2147483648] = 1`,
    code: "language.invalid_reference",
  },
  {
    name: "set requires member",
    text: `${graphTarget}\npatch g\nset @TASK = 1`,
    code: "language.expected_member",
  },
  {
    name: "unknown local",
    text: `${graphTarget}\npatch g\nconnect missing.Out -> @P`,
    code: "language.unknown_local_reference",
  },
  {
    name: "duplicate local",
    text: `${graphTarget}\npatch g\nvalue = {x: 1}\nvalue = {x: 2}`,
    code: "language.duplicate_binding",
  },
  {
    name: "duplicate object key",
    text: "value = {x: 1, x: 2}",
    code: "language.duplicate_object_key",
  },
  {
    name: "non-finite number",
    text: "value = {n: 1e999}",
    code: "language.invalid_number",
  },
  {
    name: "target is not object expression",
    text: "value = target {domain: asset}",
    code: "language.target_not_object_expression",
  },
];

for (const testCase of cases) {
  const parsed = parseSalObject(testCase.text);
  assert.equal(parsed.object, undefined, testCase.name);
  assert.equal(parsed.diagnostics[0]?.code, testCase.code, testCase.name);
}

const invalidResult = parseSalResultText(`result unresolved_target
target g = target {domain: asset}
no_objects`);
assert.equal(invalidResult.result, undefined);
assert.equal(invalidResult.diagnostics[0]?.code, "language.unresolved_target_has_table");

const unusedRelated = parseSalResultText(`result exact_target
target g = target {domain: graph, asset: "/Game/BP.BP", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
related bp = target {domain: blueprint, asset: "/Game/BP.BP", id: "11111111-1111-1111-1111-111111111111"}
no_objects`);
assert.equal(unusedRelated.result, undefined);
assert.equal(unusedRelated.diagnostics[0]?.code, "language.invalid_result_context");

const leadingResultComment = parseSalResultText(`# transport metadata
result unresolved_target
no_objects`);
assert.equal(leadingResultComment.result, undefined);
assert.equal(leadingResultComment.diagnostics[0]?.code, "language.invalid_result_envelope");

const reservedResultAlias = parseSalResultText(`result exact_target
target graph = target {domain: graph, asset: "/Game/BP.BP", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
no_objects`);
assert.equal(reservedResultAlias.result, undefined);
assert.equal(reservedResultAlias.diagnostics[0]?.code, "language.invalid_target_binding");

const emptyHandoffPurpose = parseSalResultText(`result exact_target
target g = target {domain: graph, asset: "/Game/BP.BP", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
related bp = target {domain: blueprint, asset: "/Game/BP.BP", id: "11111111-1111-1111-1111-111111111111"}
handoff "" to bp
no_objects`);
assert.equal(emptyHandoffPurpose.result, undefined);
assert.equal(emptyHandoffPurpose.diagnostics[0]?.code, "language.invalid_result_envelope");

console.log(`invalid cases passed: ${cases.length + 5}`);
