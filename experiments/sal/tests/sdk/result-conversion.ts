import assert from "node:assert/strict";
import {
  objectResultToTextResult,
  type MutationResult,
  type ObjectResult,
} from "../../src/index.js";

const result: ObjectResult = {
  object: {
    statements: [
      {
        target: { kind: "local", name: "bp" },
        value: { kind: "call", callee: "blueprint", args: { id: "BP1" } },
      },
      { kind: "comment", text: "Current editor owner" },
      {
        target: { kind: "local", name: "graph" },
        value: {
          kind: "call",
          callee: "graph",
          args: { blueprint: { kind: "local", name: "bp" }, id: "G1" },
        },
      },
    ],
  },
  diagnostics: [],
  page: { next: "cursor:2" },
};

const converted = await objectResultToTextResult(result);
assert.deepEqual(converted.diagnostics, []);
assert.equal(
  converted.text,
  `bp = blueprint(id: "BP1")\n# Current editor owner\ngraph = graph(blueprint: bp, id: "G1")`,
);
assert.equal(converted.page?.next, "cursor:2");
assert.equal("object" in converted, false);
console.log("[PASS] public result conversion preserves ordered Object Text and envelope fields");

const mutation: MutationResult = {
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
assert.equal(convertedMutation.text, undefined);
assert.equal(convertedMutation.dryRun, true);
assert.equal(convertedMutation.applied, false);
assert.deepEqual(convertedMutation.planned, { operations: 1 });
console.log("[PASS] public result conversion preserves mutation execution fields");

const invalidShape = await objectResultToTextResult({ diagnostics: "not-an-array" });
assert.equal(invalidShape.text, undefined);
assert.equal(invalidShape.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] public result conversion rejects malformed RPC values");

const invalidReference = await objectResultToTextResult({
  object: {
    statements: [
      {
        target: { kind: "local", name: "graph" },
        value: {
          kind: "call",
          callee: "graph",
          args: { blueprint: { kind: "local", name: "missing" }, id: "G1" },
        },
      },
    ],
  },
  diagnostics: [],
});
assert.equal(invalidReference.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] public result conversion enforces ordered reference safety");
