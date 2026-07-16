import assert from "node:assert/strict";
import { createMemoryAssetExecutor, createSal, type Target } from "../../src/index.js";

const target: Target = { alias: "asset", value: { kind: "name", name: "asset" } };
const executor = createMemoryAssetExecutor({
  documents: [{
    target,
    object: { statements: [
      { target: { kind: "local", name: "door" }, value: { kind: "call", callee: "asset", args: { path: "/Game/BP_Door.BP_Door", type: "UBlueprint", name: "BP_Door" } } },
      { target: { kind: "local", name: "menu" }, value: { kind: "call", callee: "asset", args: { path: "/Game/WBP_Menu.WBP_Menu", type: "UWidgetBlueprint", name: "WBP_Menu" } } },
    ] },
  }],
});

assert.deepEqual(executor.interfaces, ["asset"]);
const result = await createSal({ executor }).query(`query asset
assets "Door"
where type = "UBlueprint"`);
assert.deepEqual(result.diagnostics, []);
assert.match(result.text ?? "", /BP_Door/);
assert.doesNotMatch(result.text ?? "", /WBP_Menu/);
console.log("[PASS] asset executor is a thin interface wrapper over Object Text");
