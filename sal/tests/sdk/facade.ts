import assert from "node:assert/strict";
import {
  createSal,
  type ExactQueryResult,
  type ObjectResult,
  type ObjectText,
  type Query,
  type SalExecutor,
} from "../../src/index.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const queryText = `g = target {domain: graph, asset: "/Game/BP_SALExample.BP_SALExample", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
query g
nodes "Branch"
with layout`;

const patchText = `g = target {domain: graph, asset: "/Game/BP_SALExample.BP_SALExample", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
patch g dry run
set @N1.NodeComment = "Guard"`;

const object: ObjectText = {
  statements: [{
    target: { kind: "local", name: "branch" },
    value: {
      kind: "object",
      semanticTag: "node",
      fields: {
        id: "N1",
        type: "/Script/BlueprintGraph.K2Node_IfThenElse",
      },
    },
  }, {
    kind: "comment",
    text: "schema available with with schema",
  }],
};

function exactResult(query: Query, value: ObjectText = object): ExactQueryResult {
  if (query.target.target.domain !== "graph" || !("id" in query.target.target) || !query.target.target.blueprintId) {
    throw new Error("Fixture expects a canonical Graph Target.");
  }
  return {
    targetContext: "exact_target",
    target: { alias: query.target.alias, target: query.target.target as any },
    object: value,
    diagnostics: [],
    page: { next: "offset:1" },
  };
}

let calls = 0;
const executor: SalExecutor = {
  interfaces: ["blueprint", "graph"],
  async query(query) {
    calls += 1;
    return exactResult(query);
  },
  async patch(patch) {
    calls += 1;
    return {
      targetContext: "exact_target",
      target: patch.target,
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

const sal = createSal({ executor, catalog: testInterfaceCatalog });
const schemaIndex = await sal.schema();
assert.match(schemaIndex.text ?? "", /^blueprint$/m);
assert.match(schemaIndex.text ?? "", /^graph$/m);
assert.equal(calls, 0);
const graphSchema = await sal.schema("graph");
assert.equal(graphSchema.text, "# Test Graph Interface\n");
const inactiveSchema = await sal.schema("widget");
assert.equal(inactiveSchema.diagnostics[0]?.code, "capability.interface_unavailable");
assert.deepEqual(inactiveSchema.diagnostics[0]?.supported, ["blueprint", "graph"]);
console.log("[PASS] sal.schema uses the injected catalog and active executor interfaces");

assert.throws(
  () => createSal({
    executor: { interfaces: ["missing"], async query() { return {} as never; } },
    catalog: testInterfaceCatalog,
  }),
  /Unknown SAL interface module: missing/,
);
assert.throws(
  () => createSal({
    executor: { interfaces: ["graph"], async query() { return {} as never; } },
    catalog: [testInterfaceCatalog[2], testInterfaceCatalog[2]],
  }),
  /Duplicate SAL interface module: graph/,
);
console.log("[PASS] createSal rejects inconsistent interface catalogs");

const queryResult = await sal.query(queryText);
assert.deepEqual(queryResult.diagnostics, []);
assert.match(queryResult.text ?? "", /^result exact_target/m);
assert.match(queryResult.text ?? "", /branch = node \{/);
assert.equal(queryResult.page?.next, "offset:1");
console.log("[PASS] sal.query returns contextual Result Text and pagination");

const patchResult = await sal.patch(patchText);
assert.deepEqual(patchResult.diagnostics, []);
assert.match(patchResult.text ?? "", /branch = node \{/);
assert.equal(patchResult.dryRun, true);
assert.equal(patchResult.valid, true);
assert.equal(patchResult.applied, false);
assert.deepEqual(patchResult.planned, { operations: 1 });
console.log("[PASS] sal.patch returns contextual Result Text plus mutation fields");

const wrongKind = await sal.query(patchText);
assert.equal(wrongKind.diagnostics[0]?.code, "language.wrong_document_kind");

const queryOnly: SalExecutor = {
  interfaces: ["graph"],
  async query(query) { return exactResult(query); },
};
const unavailable = await createSal({ executor: queryOnly, catalog: testInterfaceCatalog }).patch(patchText);
assert.equal(unavailable.diagnostics[0]?.code, "capability.patch_unavailable");

const invalidResultExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query() {
    return { targetContext: "exact_target", object: { statements: [{ kind: "unknown" }] }, diagnostics: [] } as unknown as ExactQueryResult;
  },
};
const invalidResult = await createSal({ executor: invalidResultExecutor, catalog: testInterfaceCatalog }).query(queryText);
assert.equal(invalidResult.diagnostics[0]?.code, "language.invalid_result_shape");

const invalidReferenceExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query(query) {
    const result = exactResult(query, {
      statements: [{
        from: { kind: "member", object: { kind: "local", name: "missing" }, path: ["Out"] },
        to: { kind: "stable_ref", identityPath: ["P2"] },
      }],
    });
    return result;
  },
};
const invalidReference = await createSal({ executor: invalidReferenceExecutor, catalog: testInterfaceCatalog }).query(queryText);
assert.equal(invalidReference.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] executor output is schema- and reference-validated");

const queryMutationEnvelope = await createSal({
  catalog: testInterfaceCatalog,
  executor: {
    interfaces: ["graph"],
    async query(query) {
      return {
        ...exactResult(query),
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

const patchPlainEnvelope = await createSal({
  catalog: testInterfaceCatalog,
  executor: {
    interfaces: ["graph"],
    async query(query) { return exactResult(query); },
    async patch(patch) {
      return {
        targetContext: "exact_target",
        target: patch.target,
        object,
        diagnostics: [],
      } as never;
    },
  },
}).patch(patchText);
assert.equal(patchPlainEnvelope.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] Query and Patch reject the opposite result envelope");

const targetAliasExecutor: SalExecutor = {
  interfaces: ["graph"],
  async query(query) {
    return exactResult(query, {
      statements: [{
        target: { kind: "local", name: "branch" },
        value: {
          kind: "object",
          fields: { owner: { kind: "local", name: "g" }, id: "N1" },
        },
      }],
    });
  },
};
const targetAlias = await createSal({ executor: targetAliasExecutor, catalog: testInterfaceCatalog }).query(queryText);
assert.deepEqual(targetAlias.diagnostics, []);
console.log("[PASS] Result Object Text may reference an alias declared by its Target table");

const controller = new AbortController();
let receivedSignal: AbortSignal | undefined;
await createSal({
  catalog: testInterfaceCatalog,
  executor: {
    interfaces: ["graph"],
    async query(query, options) {
      receivedSignal = options?.signal;
      return exactResult(query);
    },
  },
}).query(queryText, { signal: controller.signal });
assert.equal(receivedSignal, controller.signal);
console.log("[PASS] SAL forwards execution options");
