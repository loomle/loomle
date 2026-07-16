import assert from "node:assert/strict";
import { createSal, type Adapter, type ObjectResult } from "../../src/index.js";

const queryText = `bp = asset(path: "/Game/BP_SALExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find nodes
where name = branch
with pins, defaults
`;

const patchText = `bp = asset(path: "/Game/BP_SALExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g dry run

delay = delay(duration: 1.0)
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

const sal = createSal({ adapters: [echoAdapter] });

const queryResult = await sal.query(queryText);
assert.equal(queryResult.diagnostics.length, 0);
assert.match(queryResult.text ?? "", /query g/);
assert.match(queryResult.text ?? "", /find nodes/);
assert.match(queryResult.text ?? "", /where name = branch/);
console.log("[PASS] sal.query dispatches and formats adapter result");

const pagedAdapter: Adapter = {
  domain: "blueprint",
  async query(object): Promise<ObjectResult> {
    return { object, diagnostics: [], page: { next: "cursor-1" } };
  },
  async patch(object): Promise<ObjectResult> {
    return { object, diagnostics: [] };
  },
};

const pagedResult = await createSal({ adapters: [pagedAdapter] }).query(queryText);
assert.equal(pagedResult.page?.next, "cursor-1");
console.log("[PASS] sal.query preserves pagination cursors");

const patchResult = await sal.patch(patchText);
assert.equal(patchResult.diagnostics.length, 0);
assert.match(patchResult.text ?? "", /patch g dry run/);
assert.match(patchResult.text ?? "", /insert begin.Then -> delay.Exec\/Completed -> print.Exec/);
console.log("[PASS] sal.patch dispatches and formats adapter result");

const wrongKindResult = await sal.query(patchText);
assert.equal(wrongKindResult.text, undefined);
assert.equal(wrongKindResult.diagnostics[0]?.code, "wrong_document_kind");
console.log("[PASS] sal.query rejects patch documents");

const missingAdapterResult = await createSal().query(queryText);
assert.equal(missingAdapterResult.text, undefined);
assert.equal(missingAdapterResult.diagnostics[0]?.code, "missing_adapter");
console.log("[PASS] sal.query reports missing adapter");

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

const diagnosticResult = await createSal({ adapters: [diagnosticAdapter] }).query(queryText);
assert.equal(diagnosticResult.text, undefined);
assert.equal(diagnosticResult.diagnostics[0]?.code, "adapter_error");
console.log("[PASS] sal.query preserves adapter diagnostics");

const invalidObjectAdapter: Adapter = {
  domain: "blueprint",
  async query(): Promise<ObjectResult> {
    return {
      object: {
        kind: "graph",
        target: {
          domain: "blueprint",
          asset: "/Game/BP_SALExample",
          graph: { kind: "name", name: "EventGraph" },
        },
        nodes: [
          {
            alias: "print",
            type: "PrintString",
            fields: {},
            editorTitle: "Print String",
          },
        ],
        edges: [],
      },
      diagnostics: [],
    } as unknown as ObjectResult;
  },
};

const invalidObjectResult = await createSal({ adapters: [invalidObjectAdapter] }).query(queryText);
assert.equal(invalidObjectResult.text, undefined);
assert.equal(invalidObjectResult.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] sal.query rejects schema-invalid adapter objects");

const invalidEnvelopeAdapter: Adapter = {
  domain: "blueprint",
  async query(): Promise<ObjectResult> {
    return { object: { kind: "asset_result", assets: [] } } as unknown as ObjectResult;
  },
};

const invalidEnvelopeResult = await createSal({ adapters: [invalidEnvelopeAdapter] }).query(queryText);
assert.equal(invalidEnvelopeResult.text, undefined);
assert.equal(invalidEnvelopeResult.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] sal.query rejects schema-invalid adapter result envelopes");
