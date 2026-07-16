import assert from "node:assert/strict";
import { createMemoryBlueprintExecutor, createSal, parseSalObject, type Query } from "../../src/index.js";

const locator = `bp = blueprint(asset: "/Game/BP_Door.BP_Door")`;
const target = (parseSalObject(`${locator}\nquery bp\nsummary`).object as Query).target;
const executor = createMemoryBlueprintExecutor({
  documents: [{
    target,
    object: { statements: [
      { target: { kind: "local", name: "door" }, value: { kind: "call", callee: "blueprint", args: { path: "/Game/BP_Door.BP_Door" } } },
      { target: { kind: "member", object: { kind: "local", name: "door" }, path: ["Health"] }, value: { kind: "call", callee: "variable", args: { name: "Health", type: "FloatProperty" } } },
    ] },
  }],
});

assert.deepEqual(executor.interfaces, ["blueprint"]);
const result = await createSal({ executor }).query(`${locator}\nquery bp\nsummary`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /door\.Health = variable/);
console.log("[PASS] blueprint executor uses the shared normalized document model");
