import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { catalog, guide } from "../src/index.js";

const expectedNames = ["asset", "blueprint", "class", "graph", "state_tree", "widget"];
const interfacesRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const repositoryRoot = resolve(interfacesRoot, "..");
const resultFragmentMarker =
  "Result Text fragment, not a standalone Result Text document.";
const symbolicGuidPlaceholder =
  /(?:\b(?:id|blueprintId)\s*:\s*"[^"\n]*-guid"|@[A-Za-z0-9_-]*-guid\b)/i;

function markdownFiles(root: string): string[] {
  return readdirSync(root, { withFileTypes: true }).flatMap((entry) => {
    const path = resolve(root, entry.name);
    if (entry.isDirectory()) {
      return markdownFiles(path);
    }
    return entry.isFile() && entry.name.endsWith(".md") ? [path] : [];
  });
}

function paragraphs(text: string): string[] {
  return text.trim().split(/\r?\n[ \t]*\r?\n/).filter(Boolean);
}

function salFences(markdown: string): Array<{ body: string; precedingContext: string }> {
  const fences: Array<{ body: string; precedingContext: string }> = [];
  const pattern = /```sal[ \t]*\r?\n([\s\S]*?)```/g;
  for (const match of markdown.matchAll(pattern)) {
    const before = markdown.slice(0, match.index);
    const previousFence = before.lastIndexOf("```");
    const trailingText = before.slice(previousFence < 0 ? 0 : previousFence + 3).trim();
    const precedingParagraphs = paragraphs(trailingText);
    const precedingContext = precedingParagraphs.at(-1) ?? "";
    fences.push({
      body: match[1].trim(),
      precedingContext,
    });
  }
  return fences;
}

function validateSalExamples(name: string, markdown: string): void {
  for (const { body, precedingContext } of salFences(markdown)) {
    const startsWithResult = /^result (?:exact_target|domain_root|unresolved_target)\b/.test(body);
    const usesResultOnlySyntax =
      /^(?:related\s+\S+\s*=\s*target\s*\{|handoff\b|objects$|no_objects$)/m.test(body);
    const markedFragment = precedingContext.includes(resultFragmentMarker);
    const isCopyableSchemaContract = /^###\r?\nschema\b/.test(body);

    assert.ok(
      !usesResultOnlySyntax || startsWithResult || markedFragment,
      `${name} has a Result Text example without a result header or fragment marker:\n${body}`,
    );

    if (
      startsWithResult ||
      isCopyableSchemaContract ||
      /\b(?:canonical|copyable)\b/i.test(precedingContext)
    ) {
      assert.doesNotMatch(
        body,
        symbolicGuidPlaceholder,
        `${name} uses a symbolic *-guid placeholder in a canonical/copyable SAL example.`,
      );
    }
  }
}

assert.match(guide, /^# SAL\n/);
assert.match(guide, /status\(\{\}\)/);
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
validateSalExamples("GUIDE.md", guide);

for (const entry of catalog) {
  assert.ok(entry.description.length > 0, `${entry.name} needs a description.`);
  assert.match(entry.text, new RegExp(`^# ${entry.name}\\n`));
  validateSalExamples(`${entry.name}.md`, entry.text);
}

const formalDocumentationFiles = [
  ...markdownFiles(resolve(repositoryRoot, "sal/docs")),
  resolve(repositoryRoot, "docs/planned/SAL_OBJECT_AND_REFERENCE_MODEL.md"),
];
for (const path of formalDocumentationFiles) {
  validateSalExamples(relative(repositoryRoot, path), readFileSync(path, "utf8"));
}

console.log(
  `Validated the resident guide, ${catalog.length} interface documents, and ${formalDocumentationFiles.length} formal documentation files.`,
);
