import assert from "node:assert/strict";
import { parseLglObject } from "../../src/index.js";

const header = `bp = asset(path: "/Game/BP_LGLExample", type: blueprint)
g = graph(domain: blueprint, asset: bp, graph: EventGraph)
patch g`;

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
    name: "multi-edge add connect",
    text: `${header}

add delay begin.Then -> delay.Exec/Completed -> print.Exec
`,
    code: "invalid_add_connect",
  },
  {
    name: "legacy palette binding",
    text: `${header}

DelaySource = palette(id: "palette:blueprint:function:/Script/Engine.KismetSystemLibrary.Delay")
`,
    code: "unsupported_palette_binding",
  },
];

for (const testCase of cases) {
  const result = parseLglObject(testCase.text);
  assert.equal(result.object, undefined, testCase.name);
  assert.equal(result.diagnostics[0]?.code, testCase.code, testCase.name);
  console.log(`[PASS] ${testCase.name}`);
}
