import assert from "node:assert/strict";
import {
  createMemoryGraphExecutor,
  createSal,
  parseSalObject,
  type ObjectText,
  type Query,
} from "../../src/index.js";

const locator = `bp = blueprint(asset: "/Game/BP_Test.BP_Test")
g = graph(asset: bp, name: "EventGraph")`;
const targetRequest = parseSalObject(`${locator}\nquery g\nsummary`).object as Query;

const graph: ObjectText = {
  statements: [
    { target: { kind: "local", name: "begin" }, value: { kind: "call", callee: "node", args: { id: "N1", type: "/Script/BlueprintGraph.K2Node_Event", name: "BeginPlay" } } },
    { target: { kind: "member", object: { kind: "local", name: "begin" }, path: ["Then"] }, value: { kind: "call", callee: "pin", args: { id: "P1", direction: { kind: "name", name: "out" } } } },
    { target: { kind: "local", name: "branch" }, value: { kind: "call", callee: "node", args: { id: "N2", type: "/Script/BlueprintGraph.K2Node_IfThenElse", name: "Branch" } } },
    { target: { kind: "member", object: { kind: "local", name: "branch" }, path: ["execute"] }, value: { kind: "call", callee: "pin", args: { id: "P2", direction: { kind: "name", name: "in" } } } },
    { from: { kind: "pin", id: "P1" }, to: { kind: "pin", id: "P2" } },
    { target: { kind: "local", name: "delayTemplate" }, value: { kind: "call", callee: "node", args: { palette: "P_Delay" } } },
    { target: { kind: "member", object: { kind: "local", name: "delayTemplate" }, path: ["execute"] }, value: { kind: "call", callee: "pin", args: { type: "(PinCategory=exec)", direction: { kind: "name", name: "in" } } } },
  ],
};

const executor = createMemoryGraphExecutor({ documents: [{ target: targetRequest.target, object: graph }] });
const sal = createSal({ executor });

const collection = await sal.query(`${locator}\nquery g\nnodes "Branch"`);
assert.deepEqual(collection.diagnostics, []);
assert.match(collection.text ?? "", /branch = node/);
assert.doesNotMatch(collection.text ?? "", /branch\.execute = pin/);
console.log("[PASS] generic memory executor keeps collection results compact");

const exact = await sal.query(`${locator}\nquery g\nnode@N2`);
assert.deepEqual(exact.diagnostics, []);
assert.match(exact.text ?? "", /branch = node/);
assert.match(exact.text ?? "", /branch\.execute = pin/);
console.log("[PASS] generic memory executor returns owned Pins for exact Nodes");

const pin = await sal.query(`${locator}\nquery g\npin@P2`);
assert.deepEqual(pin.diagnostics, []);
assert.match(pin.text ?? "", /branch = node/);
assert.match(pin.text ?? "", /branch\.execute = pin/);
assert.doesNotMatch(pin.text ?? "", /begin\.Then = pin/);
console.log("[PASS] generic memory executor returns a compact owner for exact Pins");

const palette = await sal.query(`${locator}\nquery g\npalette @P_Delay`);
assert.deepEqual(palette.diagnostics, []);
assert.match(palette.text ?? "", /delayTemplate = node/);
assert.match(palette.text ?? "", /delayTemplate\.execute = pin/);
console.log("[PASS] generic memory executor returns determinable Pins for exact Palette Entries");

const dryRun = await sal.patch(`${locator}\npatch g dry run\nset node@N2.NodeComment = "Dry"`);
assert.equal(dryRun.valid, true);
assert.equal(dryRun.applied, false);
assert.doesNotMatch(JSON.stringify(executor.getDocuments()), /Dry/);
console.log("[PASS] generic memory executor plans dry runs without mutation");

const patch = await sal.patch(`${locator}\npatch g

print = node(id: "N3", type: "/Script/BlueprintGraph.K2Node_CallFunction", name: "PrintString")
add print
connect pin@P1 -> print.execute
set node@N2.NodeComment = "Guard"`);
assert.deepEqual(patch.diagnostics, []);
assert.equal(patch.applied, true);
const stored = executor.getDocuments()[0].object;
assert.match(JSON.stringify(stored), /PrintString/);
assert.match(JSON.stringify(stored), /Guard/);
console.log("[PASS] generic memory executor applies ordered Patch statements");
