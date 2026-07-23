import assert from "node:assert/strict";
import { catalog, guide } from "../src/index.js";

const expectedNames = ["asset", "blueprint", "class", "graph", "state_tree", "widget"];

assert.match(guide, /^# SAL\n/);
assert.match(guide, /project\(\{\}\)/);
assert.match(guide, /project\(\{ projectId: "<id>" \}\)/);
assert.match(guide, /## Project Binding/);
assert.match(guide, /binding is sticky/);
assert.match(guide, /sal_query\(\{ text \}\)/);
assert.match(guide, /editor_context\(\{\}\)/);
assert.match(guide, /## Schema Discovery/);
assert.match(guide, /operation-less form is the shared exact-target read/);
assert.deepEqual(
  catalog.map(({ name }) => name),
  expectedNames,
);
assert.equal(new Set(catalog.map(({ name }) => name)).size, catalog.length);

for (const entry of catalog) {
  assert.ok(entry.description.length > 0, `${entry.name} needs a description.`);
  assert.match(entry.text, new RegExp(`^# ${entry.name}\\n`));
}

console.log(`Validated the resident guide and ${catalog.length} interface documents.`);
