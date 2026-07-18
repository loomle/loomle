import assert from "node:assert/strict";
import { parseSalObject } from "../../src/index.js";

const graph = `bp = blueprint(asset: "/Game/BP_SALExample.BP_SALExample")
g = graph(asset: bp, name: "EventGraph")`;

const cases: Array<{ name: string; text: string; code: string }> = [
  {
    name: "query requires operation",
    text: `${graph}\nquery g`,
    code: "language.missing_query_operation",
  },
  {
    name: "patch requires statement",
    text: `${graph}\npatch g`,
    code: "language.missing_patch_statement",
  },
  {
    name: "patch requires bound call target",
    text: "patch asset\nsave",
    code: "language.unknown_target",
  },
  {
    name: "search text must be quoted",
    text: `${graph}\nquery g\nnodes Print String`,
    code: "language.expected_quoted_text",
  },
  {
    name: "relationship query requires stable ref",
    text: `${graph}\nquery g\ncontext localNode`,
    code: "language.expected_stable_reference",
  },
  {
    name: "invalid insert shape",
    text: `${graph}\npatch g\ninsert pin@A -> delay.execute -> pin@B`,
    code: "language.invalid_operation",
  },
  {
    name: "set requires member ref",
    text: `${graph}\npatch g\nset node@A = 1`,
    code: "language.expected_member",
  },
  {
    name: "invalid order direction",
    text: `${graph}\nquery g\nnodes\norder by score descending`,
    code: "language.invalid_order_by",
  },
  {
    name: "page limit must be positive",
    text: `${graph}\nquery g\nnodes\npage limit 0`,
    code: "language.invalid_page",
  },
  {
    name: "unclosed block comment",
    text: "###\nunfinished",
    code: "language.unclosed_comment",
  },
  {
    name: "invalid string escape",
    text: `${graph}\nquery g\nnodes "bad\\q"`,
    code: "language.invalid_string",
  },
  {
    name: "duplicate inline object key",
    text: "value = object(fields: {Name: 1, Name: 2})",
    code: "language.duplicate_object_key",
  },
  {
    name: "empty array element is not discarded",
    text: "value = [1,,2]",
    code: "language.unsupported_value",
  },
  {
    name: "empty edge endpoint is not discarded",
    text: "pin@A -> -> pin@B",
    code: "language.invalid_object_statement",
  },
  {
    name: "literal keyword cannot be a local alias",
    text: "true = node()",
    code: "language.invalid_binding_target",
  },
  {
    name: "object edge requires an earlier local binding",
    text: "missing.Out -> pin@B",
    code: "language.unknown_local_reference",
  },
  {
    name: "member binding requires an earlier owner",
    text: "missing.Value = 1",
    code: "language.unknown_local_reference",
  },
  {
    name: "patch cannot use an erased locator alias",
    text: `${graph}\npatch g\ninvoke bp Reconstruct()`,
    code: "language.unknown_local_reference",
  },
  {
    name: "object text binding targets are unique",
    text: "value = 1\nvalue = 2",
    code: "language.duplicate_binding",
  },
];

for (const testCase of cases) {
  const result = parseSalObject(testCase.text);
  assert.equal(result.object, undefined, testCase.name);
  assert.equal(result.diagnostics[0]?.code, testCase.code, testCase.name);
  console.log(`[PASS] ${testCase.name}`);
}
