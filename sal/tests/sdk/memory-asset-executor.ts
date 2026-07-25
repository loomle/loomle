import assert from "node:assert/strict";
import { createSal, type TargetBinding } from "../../src/index.js";
import { createMemoryExecutor } from "../fixtures/memory-executor.js";
import { testInterfaceCatalog } from "./interface-catalog.js";

const target: TargetBinding = {
  alias: "catalog",
  target: { kind: "target", domain: "asset" },
};
const executor = createMemoryExecutor({
  interfaces: ["asset"],
  documents: [{
    target,
    object: {
      statements: [{
        target: { kind: "local", name: "door" },
        value: {
          kind: "object",
          semanticTag: "asset_info",
          fields: { path: "/Game/BP_Door.BP_Door", type: "UBlueprint", name: "BP_Door" },
        },
      }],
    },
  }],
});

const result = await createSal({ executor, catalog: testInterfaceCatalog }).query(
  `catalog = target {domain: asset}
query catalog
assets "Door"`,
);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /^result domain_root/m);
assert.match(result.text ?? "", /door = asset_info \{/);
console.log("[PASS] memory executor supports the Asset root Target");
