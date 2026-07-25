import assert from "node:assert/strict";
import {
  createSal,
  parseSalObject,
  type ObjectText,
  type Query,
} from "../../src/index.js";
import { createMemoryExecutor } from "../fixtures/memory-executor.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const locator = `g = target {domain: graph, asset: "/Game/BP_Test.BP_Test", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}`;
const targetRequest = parseSalObject(`${locator}\nquery g\nsummary`).object as Query;

const graph: ObjectText = {
  statements: [
    {
      target: { kind: "local", name: "begin" },
      value: {
        kind: "object",
        semanticTag: "node",
        fields: { id: "N1", type: "/Script/BlueprintGraph.K2Node_Event", name: "BeginPlay" },
      },
    },
    {
      target: { kind: "member", object: { kind: "local", name: "begin" }, path: ["Then"] },
      value: {
        kind: "object",
        semanticTag: "pin",
        fields: { id: "P1", direction: { kind: "name", name: "out" } },
      },
    },
    {
      target: { kind: "local", name: "branch" },
      value: {
        kind: "object",
        semanticTag: "node",
        fields: { id: "N2", type: "/Script/BlueprintGraph.K2Node_IfThenElse", name: "Branch" },
      },
    },
    { from: { kind: "stable_ref", identityPath: ["P1"] }, to: { kind: "stable_ref", identityPath: ["P2"] } },
  ],
};

const executor = createMemoryExecutor({
  interfaces: ["graph"],
  documents: [{ target: targetRequest.target, object: graph }],
});
const sal = createSal({ executor, catalog: testInterfaceCatalog });

const query = await sal.query(`${locator}\nquery g\nsummary`);
assert.deepEqual(query.diagnostics, []);
assert.match(query.text ?? "", /^result exact_target/m);
assert.match(query.text ?? "", /^target g = target \{/m);
assert.match(query.text ?? "", /branch = node \{/);
console.log("[PASS] memory executor returns a contextual Result Text envelope");

const dryRun = await sal.patch(`${locator}
patch g dry run
set @N2.NodeComment = "Dry"`);
assert.equal(dryRun.valid, true);
assert.equal(dryRun.applied, false);
assert.doesNotMatch(JSON.stringify(executor.getDocuments()), /Dry/);
console.log("[PASS] memory executor plans dry runs without mutation");

const patch = await sal.patch(`${locator}
patch g
print = node {id: "N3", type: "/Script/BlueprintGraph.K2Node_CallFunction", name: "PrintString"}
add print
connect @P1 -> print.execute
set @N2.NodeComment = "Guard"`);
assert.deepEqual(patch.diagnostics, []);
assert.equal(patch.applied, true);
const stored = executor.getDocuments()[0].object;
assert.match(JSON.stringify(stored), /PrintString/);
assert.match(JSON.stringify(stored), /Guard/);
console.log("[PASS] memory executor applies new-protocol Patch statements");
