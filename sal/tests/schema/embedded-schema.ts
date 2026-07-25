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

console.log("[PASS] embedded runtime Schema and reserved words match the canonical JSON");
