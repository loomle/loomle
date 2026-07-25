import assert from "node:assert/strict";
import { createSal, parseSalObject, type Query } from "../../src/index.js";
import { createMemoryExecutor } from "../fixtures/memory-executor.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const locator = `menu = target {domain: widget, asset: "/Game/UI/WBP_Menu.WBP_Menu", id: "33333333-3333-3333-3333-333333333333"}`;
const target = (parseSalObject(`${locator}\nquery menu\ntree depth 20`).object as Query).target;
const executor = createMemoryExecutor({
  interfaces: ["widget"],
  documents: [{
    target,
    object: {
      statements: [{
        target: { kind: "local", name: "root" },
        value: {
          kind: "object",
          semanticTag: "widget_node",
          fields: { id: "W1", type: "/Script/UMG.CanvasPanel" },
        },
      }, {
        target: { kind: "member", object: { kind: "local", name: "root" }, path: ["start"] },
        value: {
          kind: "object",
          semanticTag: "widget_node",
          fields: { id: "W2", type: "/Script/UMG.Button" },
        },
      }],
    },
  }],
});

const result = await createSal({ executor, catalog: testInterfaceCatalog }).query(`${locator}\nquery menu\ntree depth 20`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /root\.start = widget_node \{/);
console.log("[PASS] memory executor supports the Widget Target");
