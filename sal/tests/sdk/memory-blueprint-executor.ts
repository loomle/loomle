import assert from "node:assert/strict";
import { createSal, parseSalObject, type Query } from "../../src/index.js";
import { createMemoryExecutor } from "../fixtures/memory-executor.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const locator = `bp = target {domain: blueprint, asset: "/Game/BP_Door.BP_Door", id: "11111111-1111-1111-1111-111111111111"}`;
const target = (parseSalObject(`${locator}\nquery bp\nsummary`).object as Query).target;
const executor = createMemoryExecutor({
  interfaces: ["blueprint"],
  documents: [{
    target,
    object: {
      statements: [{
        target: { kind: "local", name: "door" },
        value: { kind: "object", fields: { path: "/Game/BP_Door.BP_Door" } },
      }, {
        target: { kind: "member", object: { kind: "local", name: "door" }, path: ["Health"] },
        value: {
          kind: "object",
          semanticTag: "variable",
          fields: { name: "Health", type: "FloatProperty" },
        },
      }],
    },
  }],
});

const result = await createSal({ executor, catalog: testInterfaceCatalog }).query(`${locator}\nquery bp\nsummary`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /door\.Health = variable \{/);
console.log("[PASS] memory executor supports the Blueprint Target");
