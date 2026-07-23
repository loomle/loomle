import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { salObjectSchemaText } from "../../src/generated/sal-object-schema-data.js";

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const canonicalSchemaText = (
  await readFile(join(packageRoot, "schema/sal-object.schema.json"), "utf8")
).replace(/\r\n?/g, "\n");

assert.equal(salObjectSchemaText, canonicalSchemaText);
assert.doesNotThrow(() => JSON.parse(salObjectSchemaText));

console.log("[PASS] embedded runtime Schema exactly matches the canonical JSON");
