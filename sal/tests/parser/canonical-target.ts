import assert from "node:assert/strict";
import {
  formatTargetExpression,
  parseCanonicalTargetText,
  type CanonicalEditorTarget,
} from "../../src/index.js";

const blueprintId = "11111111-1111-1111-1111-111111111111";
const graphId = "22222222-2222-2222-2222-222222222222";
const asset = "/Game/Doors/BP_Door.BP_Door";

const blueprint = parseCanonicalTargetText(
  `target {domain: blueprint, asset: "${asset}", id: "${blueprintId}"}`,
);
assert.deepEqual(blueprint.diagnostics, []);
assert.deepEqual(blueprint.target, {
  kind: "target",
  domain: "blueprint",
  asset,
  id: blueprintId,
});

const graph = parseCanonicalTargetText(`target {
  domain: graph,
  asset: "${asset}",
  blueprintId: "11111111111111111111111111111111",
  id: "22222222-2222-2222-2222-222222222222"
}`);
assert.deepEqual(graph.diagnostics, []);
assert.deepEqual(graph.target, {
  kind: "target",
  domain: "graph",
  asset,
  blueprintId,
  id: graphId,
});

for (const target of [blueprint.target, graph.target] as CanonicalEditorTarget[]) {
  const formatted = formatTargetExpression(target);
  const reparsed = parseCanonicalTargetText(formatted);
  assert.deepEqual(reparsed.diagnostics, [], formatted);
  assert.deepEqual(reparsed.target, target, formatted);
}

const invalidCases: Array<{ name: string; text: string; code: string }> = [
  {
    name: "empty input",
    text: "",
    code: "language.invalid_target",
  },
  {
    name: "aliased target",
    text: `bp = target {domain: blueprint, asset: "${asset}", id: "${blueprintId}"}`,
    code: "language.invalid_target",
  },
  {
    name: "query document",
    text: `bp = target {domain: blueprint, asset: "${asset}", id: "${blueprintId}"}\nquery bp`,
    code: "language.invalid_target",
  },
  {
    name: "patch document",
    text: `g = target {domain: graph, asset: "${asset}", blueprintId: "${blueprintId}", id: "${graphId}"}\npatch g\nsave`,
    code: "language.invalid_target",
  },
  {
    name: "leading comment",
    text: `# target selected by context\ntarget {domain: blueprint, asset: "${asset}", id: "${blueprintId}"}`,
    code: "language.invalid_target",
  },
  {
    name: "multiple target expressions",
    text: `target {domain: blueprint, asset: "${asset}", id: "${blueprintId}"}\ntarget {domain: graph, asset: "${asset}", blueprintId: "${blueprintId}", id: "${graphId}"}`,
    code: "language.invalid_target",
  },
  {
    name: "Blueprint discovery target",
    text: `target {domain: blueprint, asset: "${asset}"}`,
    code: "language.incomplete_target",
  },
  {
    name: "Graph discovery by name",
    text: `target {domain: graph, asset: "${asset}", name: "EventGraph"}`,
    code: "language.incomplete_target",
  },
  {
    name: "Graph missing Blueprint identity",
    text: `target {domain: graph, asset: "${asset}", id: "${graphId}"}`,
    code: "language.incomplete_target",
  },
  {
    name: "Graph canonical identity mixed with discovery name",
    text: `target {domain: graph, asset: "${asset}", blueprintId: "${blueprintId}", id: "${graphId}", name: "EventGraph"}`,
    code: "language.incomplete_target",
  },
  {
    name: "canonical Asset Target",
    text: `target {domain: asset, path: "${asset}", type: "/Script/Engine.Blueprint"}`,
    code: "language.invalid_target_domain",
  },
  {
    name: "canonical Class Target",
    text: "target {domain: class, path: \"/Script/Engine.Actor\"}",
    code: "language.invalid_target_domain",
  },
  {
    name: "canonical Widget Target",
    text: `target {domain: widget, asset: "${asset}", id: "${blueprintId}"}`,
    code: "language.invalid_target_domain",
  },
  {
    name: "canonical StateTree Target",
    text: "target {domain: state_tree, asset: \"/Game/AI/ST_AI.ST_AI\", type: \"/Script/StateTreeModule.StateTree\"}",
    code: "language.invalid_target_domain",
  },
];

for (const testCase of invalidCases) {
  const parsed = parseCanonicalTargetText(testCase.text);
  assert.equal(parsed.target, undefined, testCase.name);
  assert.equal(parsed.diagnostics.length, 1, testCase.name);
  assert.equal(parsed.diagnostics[0]?.code, testCase.code, testCase.name);
}

console.log(`canonical Target cases passed: ${2 + invalidCases.length}`);
