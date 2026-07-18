import assert from "node:assert/strict";
import {
  createSal,
  loadSalGuide,
  type MutationResult,
  type ObjectResult,
  type ObjectText,
  type SalExecutor,
} from "../../src/index.js";

const residentGuide = await loadSalGuide();
assert.match(residentGuide, /^# SAL$/m);
assert.match(residentGuide, /sal_query\(\{ text \}\)/);
assert.match(residentGuide, /editor_context\(\{\}\)/);
assert.match(residentGuide, /## Schema Discovery/);
console.log("[PASS] resident SAL guide is available to MCP clients");

const queryText = `bp = blueprint(asset: "/Game/BP_SALExample.BP_SALExample")
g = graph(asset: bp, name: "EventGraph")
query g
nodes "Branch"
with layout`;

const patchText = `bp = blueprint(asset: "/Game/BP_SALExample.BP_SALExample")
g = graph(asset: bp, name: "EventGraph")
patch g dry run
set node@N1.NodeComment = "Guard"`;

const object: ObjectText = {
  statements: [
    { target: { kind: "local", name: "g" }, value: { kind: "call", callee: "graph", args: { id: "G1" } } },
    { target: { kind: "local", name: "branch" }, value: { kind: "call", callee: "node", args: { graph: { kind: "local", name: "g" }, id: "N1", type: "/Script/BlueprintGraph.K2Node_IfThenElse" } } },
    { kind: "comment", text: "schema available with with schema" },
  ],
};

let calls = 0;
const executor: SalExecutor = {
  interfaces: ["blueprint", "graph"],
  async query(): Promise<ObjectResult> {
    calls += 1;
    return { object, diagnostics: [], page: { next: "offset:1" } };
  },
  async patch(patch): Promise<MutationResult> {
    calls += 1;
    return {
      object,
      diagnostics: [],
      isError: false,
      dryRun: patch.dryRun,
      valid: true,
      applied: false,
      operation: "patch",
      planned: { operations: 1 },
    };
  },
};

const sal = createSal({ executor });
const schemaIndex = await sal.schema();
assert.match(schemaIndex.text ?? "", /^blueprint$/m);
assert.match(schemaIndex.text ?? "", /^graph$/m);
assert.equal(calls, 0);
console.log("[PASS] sal.schema is static and interface-scoped");

const queryResult = await sal.query(queryText);
assert.deepEqual(queryResult.diagnostics, []);
assert.match(queryResult.text ?? "", /g = graph\(id: "G1"\)/);
assert.match(queryResult.text ?? "", /branch = node/);
assert.equal(queryResult.page?.next, "offset:1");
console.log("[PASS] sal.query returns Object Text and preserves pagination");

const patchResult = await sal.patch(patchText);
assert.deepEqual(patchResult.diagnostics, []);
assert.match(patchResult.text ?? "", /branch = node/);
assert.equal(patchResult.dryRun, true);
assert.equal(patchResult.valid, true);
assert.equal(patchResult.applied, false);
assert.deepEqual(patchResult.planned, { operations: 1 });
console.log("[PASS] sal.patch returns the same Object Text plus mutation fields");

const wrongKind = await sal.query(patchText);
assert.equal(wrongKind.diagnostics[0]?.code, "language.wrong_document_kind");
console.log("[PASS] query rejects Patch Text");

const queryOnly: SalExecutor = { interfaces: ["graph"], async query() { return { object, diagnostics: [] }; } };
const unavailable = await createSal({ executor: queryOnly }).patch(patchText);
assert.equal(unavailable.diagnostics[0]?.code, "capability.patch_unavailable");
console.log("[PASS] patch reports executor capability");

const invalidResultExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query() {
    return { object: { statements: [{ kind: "unknown" }] }, diagnostics: [] } as unknown as ObjectResult;
  },
};
const invalidResult = await createSal({ executor: invalidResultExecutor }).query(queryText);
assert.equal(invalidResult.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] executor output is schema-validated");

const invalidReferenceExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query() {
    return {
      object: {
        statements: [
          {
            from: { kind: "member", object: { kind: "local", name: "missing" }, path: ["Out"] },
            to: { kind: "pin", id: "P2" },
          },
        ],
      },
      diagnostics: [],
    };
  },
};
const invalidReference = await createSal({ executor: invalidReferenceExecutor }).query(queryText);
assert.equal(invalidReference.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] executor output preserves ordered local-reference semantics");

const queryMutationEnvelope = await createSal({
  executor: {
    interfaces: ["graph"],
    async query() {
      return {
        diagnostics: [],
        isError: false,
        dryRun: false,
        valid: true,
        applied: false,
        operation: "patch",
      } as never;
    },
  },
}).query(queryText);
assert.equal(queryMutationEnvelope.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] query rejects a mutation envelope");

const patchPlainEnvelope = await createSal({
  executor: {
    interfaces: ["graph"],
    async query() { return { diagnostics: [] }; },
    async patch() { return { diagnostics: [] } as never; },
  },
}).patch(patchText);
assert.equal(patchPlainEnvelope.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] patch requires a mutation envelope");

const inheritedRequestAliasExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query() {
    return {
      object: {
        statements: [
          {
            target: { kind: "local", name: "branch" },
            value: {
              kind: "call",
              callee: "node",
              args: {
                graph: { kind: "local", name: "g" },
                id: "N1",
                type: "/Script/BlueprintGraph.K2Node_IfThenElse",
              },
            },
          },
        ],
      },
      diagnostics: [],
    };
  },
};
const inheritedRequestAlias = await createSal({ executor: inheritedRequestAliasExecutor }).query(queryText);
assert.equal(inheritedRequestAlias.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] result Text must declare compact bindings instead of inheriting request aliases");
