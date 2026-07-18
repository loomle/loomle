import assert from "node:assert/strict";
import { createSal, parseSalObject, type Query } from "../../src/index.js";
import { createMemoryExecutor } from "../fixtures/memory-executor.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const locator = `menu = blueprint(asset: "/Game/UI/WBP_Menu.WBP_Menu")`;
const target = (parseSalObject(`${locator}\nquery menu\ntree depth 20`).object as Query).target;
const executor = createMemoryExecutor({
  interfaces: ["blueprint", "widget"],
  documents: [{
    target,
    object: { statements: [
      { target: { kind: "local", name: "root" }, value: { kind: "call", callee: "widget", args: { id: "W1", type: "/Script/UMG.CanvasPanel" } } },
      { target: { kind: "member", object: { kind: "local", name: "root" }, path: ["start"] }, value: { kind: "call", callee: "widget", args: { id: "W2", type: "/Script/UMG.Button" } } },
    ] },
  }],
});

assert.deepEqual(executor.interfaces, ["blueprint", "widget"]);
const result = await createSal({ executor, catalog: testInterfaceCatalog }).query(`${locator}\nquery menu\ntree depth 20`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /root\.start = widget/);
console.log("[PASS] generic memory executor supports the Widget interface fixture");
