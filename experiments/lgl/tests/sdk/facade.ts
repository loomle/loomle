import assert from "node:assert/strict";
import { createLgl, type Adapter, type ObjectResult } from "../../src/index.js";

const queryText = `query blueprint("/Game/BP_LGLExample"/EventGraph)

find node branch with pins, defaults
`;

const patchText = `patch blueprint("/Game/BP_LGLExample"/EventGraph) dry run

Delay = palette({id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay"})

delay = Delay({Duration: 1.0})
insert begin.Then -> delay.Exec/Completed -> print.Exec
`;

const echoAdapter: Adapter = {
  domain: "blueprint",
  async query(object): Promise<ObjectResult> {
    return { object, diagnostics: [] };
  },
  async patch(object): Promise<ObjectResult> {
    return { object, diagnostics: [] };
  },
};

const lgl = createLgl({ adapters: [echoAdapter] });

const queryResult = await lgl.query(queryText);
assert.equal(queryResult.diagnostics.length, 0);
assert.match(queryResult.text ?? "", /^query blueprint/);
assert.match(queryResult.text ?? "", /find node branch with pins, defaults/);
console.log("[PASS] lgl.query dispatches and formats adapter result");

const patchResult = await lgl.patch(patchText);
assert.equal(patchResult.diagnostics.length, 0);
assert.match(patchResult.text ?? "", /^patch blueprint/);
assert.match(patchResult.text ?? "", /insert begin.Then -> delay.Exec\/Completed -> print.Exec/);
console.log("[PASS] lgl.patch dispatches and formats adapter result");

const wrongKindResult = await lgl.query(patchText);
assert.equal(wrongKindResult.text, undefined);
assert.equal(wrongKindResult.diagnostics[0]?.code, "wrong_document_kind");
console.log("[PASS] lgl.query rejects patch documents");

const missingAdapterResult = await createLgl().query(queryText);
assert.equal(missingAdapterResult.text, undefined);
assert.equal(missingAdapterResult.diagnostics[0]?.code, "missing_adapter");
console.log("[PASS] lgl.query reports missing adapter");

const diagnosticAdapter: Adapter = {
  domain: "blueprint",
  async query(): Promise<ObjectResult> {
    return {
      diagnostics: [
        {
          severity: "error",
          code: "adapter_error",
          message: "Adapter failed.",
        },
      ],
    };
  },
  async patch(object): Promise<ObjectResult> {
    return { object, diagnostics: [] };
  },
};

const diagnosticResult = await createLgl({ adapters: [diagnosticAdapter] }).query(queryText);
assert.equal(diagnosticResult.text, undefined);
assert.equal(diagnosticResult.diagnostics[0]?.code, "adapter_error");
console.log("[PASS] lgl.query preserves adapter diagnostics");
