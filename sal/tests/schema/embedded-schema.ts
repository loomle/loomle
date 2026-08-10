import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { reservedKeywords } from "../../src/core/expr.js";
import { salObjectSchemaText } from "../../src/generated/sal-object-schema-data.js";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const canonicalSchemaText = (
  await readFile(join(packageRoot, "schema/sal-object.schema.json"), "utf8")
).replace(/\r\n?/g, "\n");

assert.equal(salObjectSchemaText, canonicalSchemaText);
const schema = JSON.parse(salObjectSchemaText);
assert.deepEqual(
  [...reservedKeywords].sort(),
  [...schema.$defs.SemanticTag.not.enum].sort(),
  "parser and Schema semantic-tag reserved words must remain identical",
);
assert.equal(schema.$defs.Target.$ref, "#/$defs/QueryTarget");
assert.equal(schema.$defs.TargetBinding.$ref, "#/$defs/QueryTargetBinding");
assert.equal(schema.$defs.Query.properties.target.$ref, "#/$defs/QueryTargetBinding");
assert.equal(schema.$defs.Patch.properties.target.$ref, "#/$defs/PatchTargetBinding");
assert.equal(schema.$defs.ExactQueryResult.properties.target.$ref, "#/$defs/CanonicalTargetBinding");
assert.ok(
  schema.$defs.CollectionOperation.properties.kind.enum.includes("actors"),
  "protocol v6 must admit the Level actors collection operation",
);
assert.equal(
  schema.$defs.PatchTarget.oneOf.some(
    (entry: { $ref: string }) => entry.$ref === "#/$defs/PcgComponentTarget",
  ),
  false,
  "pcg_component must remain Query-only until its edit guard is certified",
);

console.log("[PASS] embedded runtime Schema and reserved words match the canonical JSON");
