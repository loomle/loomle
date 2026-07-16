import assert from "node:assert/strict";
import { parseSalObject } from "../../src/index.js";

const header = `bp = asset(path: "/Game/BP_SALExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g`;

const queryHeader = `bp = asset(path: "/Game/BP_SALExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
query g
find nodes`;

const cases: Array<{ name: string; text: string; code: string }> = [
  {
    name: "invalid insert shape",
    text: `${header}

insert begin.Then -> delay.Exec -> print.Exec
`,
    code: "invalid_insert",
  },
  {
    name: "multi-edge disconnect",
    text: `${header}

disconnect begin.Then -> delay.Exec/Completed -> print.Exec
`,
    code: "invalid_disconnect",
  },
  {
    name: "legacy palette binding",
    text: `${header}

DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
`,
    code: "unsupported_palette_binding",
  },
  {
    name: "unknown query detail",
    text: `${queryHeader}
with unknownDetail
`,
    code: "language.invalid_detail",
  },
  {
    name: "invalid order by direction",
    text: `${queryHeader}
order by score descending
`,
    code: "language.invalid_order_by",
  },
  {
    name: "invalid page limit",
    text: `${queryHeader}
page limit 0
`,
    code: "language.invalid_page",
  },
];

for (const testCase of cases) {
  const result = parseSalObject(testCase.text);
  assert.equal(result.object, undefined, testCase.name);
  assert.equal(result.diagnostics[0]?.code, testCase.code, testCase.name);
  console.log(`[PASS] ${testCase.name}`);
}
