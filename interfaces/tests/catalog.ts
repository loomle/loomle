import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { catalog, guide } from "../src/index.js";

const expectedNames = [
  "asset",
  "blueprint",
  "class",
  "graph",
  "state_tree",
  "widget",
  "level",
  "pcg",
  "pcg_component",
];
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
assert.match(guide, /editor\(\{\}\)/);
assert.match(guide, /operation: "open" \| "close"/);
assert.doesNotMatch(guide, /editor_context/);
assert.match(guide, /## Schema Discovery/);
assert.match(guide, /operation-less form is the shared exact-target read/);
assert.match(guide, /nine active Domain interface cards/);
assert.match(guide, /`pcg` admits Palette-backed Node creation/);
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

const graphInterface = catalog.find(({ name }) => name === "graph");
assert.ok(graphInterface, "Graph interface must be present.");
assert.match(
  graphInterface.text,
  /move @node-guid to \(\d+, \d+\)/,
  "Graph interface must advertise absolute Node movement.",
);
assert.doesNotMatch(
  graphInterface.text,
  /\bmove\b[^\n]*\bby\s*\(/,
  "Graph interface must not advertise relative Node movement.",
);
assert.match(
  graphInterface.text,
  /with layout[\s\S]*available only on `nodes`, exact Node or Pin reads, `context`, and exec\/data\s+flows/,
  "Graph interface must close the operations that accept layout detail.",
);
assert.match(
  graphInterface.text,
  /planned\.operations[\s\S]*scope: "graph"[\s\S]*diff\.changes/,
  "Graph interface must document precise move planning and move-only diffs.",
);

const levelInterface = catalog.find(({ name }) => name === "level");
assert.ok(levelInterface, "Level interface must be present.");
assert.match(levelInterface.text, /This interface is read-only\./);
assert.match(levelInterface.text, /actors \["text"\][\s\S]*components \["text"\]/);
assert.match(levelInterface.text, /native[\s\S]*scs[\s\S]*instance/);
assert.match(levelInterface.text, /Patch Target for authored/);

const pcgInterface = catalog.find(({ name }) => name === "pcg");
assert.ok(pcgInterface, "PCG interface must be present.");
assert.match(pcgInterface.text, /This interface is\s+read-only\./);
assert.match(pcgInterface.text, /@SurfaceSampler_0\/in\/"Bounding Shape"/);
assert.match(pcgInterface.text, /persisted integer Node position as `at: \[x, y\]`/);
assert.match(pcgInterface.text, /accepts no Patch Target/);
assert.doesNotMatch(pcgInterface.text, /^## Patch$/m);

const pcgComponentInterface = catalog.find(({ name }) => name === "pcg_component");
assert.ok(pcgComponentInterface, "PCG Component interface must be present.");
assert.match(pcgComponentInterface.text, /parameters \["text"\]/);
assert.match(
  pcgComponentInterface.text,
  /component_override[\s\S]*parent_instance[\s\S]*graph_default/,
);
assert.match(pcgComponentInterface.text, /accepts no Patch Target/);
assert.doesNotMatch(pcgComponentInterface.text, /^## Patch$/m);

const formalDocumentationFiles = [
  ...markdownFiles(resolve(repositoryRoot, "sal/docs")),
  resolve(repositoryRoot, "docs/SAL_OBJECT_AND_REFERENCE_MODEL.md"),
];
for (const path of formalDocumentationFiles) {
  validateSalExamples(relative(repositoryRoot, path), readFileSync(path, "utf8"));
}

console.log(
  `Validated the resident guide, ${catalog.length} interface documents, and ${formalDocumentationFiles.length} formal documentation files.`,
);
