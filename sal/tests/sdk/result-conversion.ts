import assert from "node:assert/strict";
import {
  objectResultToTextResult,
  parseSalResultText,
  type ExactQueryResult,
  type ObjectResult,
} from "../../src/index.js";

const graphTarget = {
  alias: "g",
  target: {
    kind: "target" as const,
    domain: "graph" as const,
    asset: "/Game/BP_Door.BP_Door",
    blueprintId: "11111111-1111-1111-1111-111111111111",
    id: "22222222-2222-2222-2222-222222222222",
  },
};
const blueprintTarget = {
  alias: "bp",
  target: {
    kind: "target" as const,
    domain: "blueprint" as const,
    asset: "/Game/BP_Door.BP_Door",
    id: "11111111-1111-1111-1111-111111111111",
  },
};

const result: ExactQueryResult = {
  targetContext: "exact_target",
  target: graphTarget,
  object: {
    statements: [{
      target: { kind: "local", name: "branch" },
      value: {
        kind: "object",
        semanticTag: "node",
        fields: { id: "N1", title: "Branch" },
      },
    }],
  },
  diagnostics: [],
  page: { next: "cursor:2" },
};

const converted = await objectResultToTextResult(result);
assert.deepEqual(converted.diagnostics, []);
assert.equal(
  converted.text,
  `result exact_target
target g = target {domain: graph, asset: "/Game/BP_Door.BP_Door", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
objects
branch = node {id: "N1", title: "Branch"}`,
);
assert.equal(converted.page?.next, "cursor:2");
assert.equal(converted.targetContext, "exact_target");
assert.deepEqual(converted.target, graphTarget);
assert.deepEqual(parseSalResultText(converted.text ?? "").diagnostics, []);
console.log("[PASS] public result conversion emits the complete Result Text envelope");

const mutation = {
  targetContext: "exact_target" as const,
  target: graphTarget,
  diagnostics: [],
  isError: false,
  dryRun: true,
  valid: true,
  applied: false,
  operation: "patch",
  planned: { operations: 1 },
};
const convertedMutation = await objectResultToTextResult(mutation);
assert.deepEqual(convertedMutation.diagnostics, []);
assert.match(convertedMutation.text ?? "", /no_objects$/);
assert.equal(convertedMutation.dryRun, true);
assert.equal(convertedMutation.applied, false);
assert.deepEqual(convertedMutation.planned, { operations: 1 });
console.log("[PASS] public result conversion preserves mutation execution fields");

const validRelated: ObjectResult = {
  ...result,
  relatedTargets: [blueprintTarget],
  handoffs: [{
    kind: "target_handoff",
    purpose: "compile owner",
    target: { kind: "local", name: "bp" },
  }],
};
const relatedConversion = await objectResultToTextResult(validRelated);
assert.deepEqual(relatedConversion.diagnostics, []);
assert.match(relatedConversion.text ?? "", /^related bp = target/m);
assert.match(relatedConversion.text ?? "", /^handoff "compile owner" to bp/m);

const semanticallyInvalid: unknown[] = [
  {
    ...result,
    relatedTargets: [{ ...blueprintTarget, alias: "g" }],
    handoffs: [{ kind: "target_handoff", purpose: "compile", target: { kind: "local", name: "g" } }],
  },
  {
    ...result,
    relatedTargets: [{ ...graphTarget, alias: "other" }],
    handoffs: [{ kind: "target_handoff", purpose: "inspect", target: { kind: "local", name: "other" } }],
  },
  {
    ...result,
    relatedTargets: [blueprintTarget],
  },
  {
    ...result,
    relatedTargets: [blueprintTarget],
    handoffs: [{ kind: "target_handoff", purpose: "wrong", target: { kind: "local", name: "g" } }],
  },
  {
    ...result,
    object: {
      statements: [{
        target: { kind: "local", name: "foreign" },
        value: {
          kind: "scoped_stable_ref",
          target: { kind: "local", name: "missing" },
          reference: { kind: "stable_ref", identityPath: ["N"] },
        },
      }],
    },
  },
];

for (const invalid of semanticallyInvalid) {
  const convertedInvalid = await objectResultToTextResult(invalid);
  assert.equal(convertedInvalid.diagnostics[0]?.code, "language.invalid_result_shape");
}
console.log("[PASS] Target table aliases, deduplication, handoffs, scopes, and related usage are enforced");

const domainRootWithUnqualifiedRef = await objectResultToTextResult({
  targetContext: "domain_root",
  target: { alias: "catalog", target: { kind: "target", domain: "asset" } },
  object: {
    statements: [{
      target: { kind: "local", name: "item" },
      value: { kind: "stable_ref", identityPath: ["N"] },
    }],
  },
  diagnostics: [],
});
assert.equal(domainRootWithUnqualifiedRef.diagnostics[0]?.code, "language.invalid_result_shape");

const invalidShape = await objectResultToTextResult({ diagnostics: "not-an-array" });
assert.equal(invalidShape.text, "result unresolved_target\nno_objects");
assert.deepEqual(parseSalResultText(invalidShape.text).diagnostics, []);
assert.equal(invalidShape.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] public result conversion rejects malformed and context-unsafe RPC values");
