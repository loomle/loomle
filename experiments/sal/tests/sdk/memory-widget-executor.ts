import assert from "node:assert/strict";
import { createMemoryWidgetExecutor, createSal, parseSalObject, type Query } from "../../src/index.js";

const locator = `menu = blueprint(asset: "/Game/UI/WBP_Menu.WBP_Menu")`;
const target = (parseSalObject(`${locator}\nquery menu\ntree depth 20`).object as Query).target;
const executor = createMemoryWidgetExecutor({
  documents: [{
    target,
    object: { statements: [
      { target: { kind: "local", name: "root" }, value: { kind: "call", callee: "widget", args: { id: "W1", type: "/Script/UMG.CanvasPanel" } } },
      { target: { kind: "member", object: { kind: "local", name: "root" }, path: ["start"] }, value: { kind: "call", callee: "widget", args: { id: "W2", type: "/Script/UMG.Button" } } },
    ] },
  }],
});

assert.deepEqual(executor.interfaces, ["blueprint", "widget"]);
const result = await createSal({ executor }).query(`${locator}\nquery menu\ntree depth 20`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /root\.start = widget/);
console.log("[PASS] widget executor uses ordered Object Text for tree output");
