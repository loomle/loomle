import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import { dirname, extname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Ajv2020 } from "ajv/dist/2020.js";

interface DiagnosticCatalog {
  diagnostics: DiagnosticDefinition[];
}

interface DiagnosticDefinition {
  code: string;
  layer: "language" | "capability" | "resolution" | "validation" | "project" | "runtime" | "tool";
}

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = join(here, "../../..");
const catalogPath = join(packageRoot, "diagnostics/catalog.json");
const catalogSchemaPath = join(packageRoot, "diagnostics/catalog.schema.json");
const sourceRoots = [
  join(packageRoot, "src"),
  join(packageRoot, "tests"),
  join(packageRoot, "../client/src"),
  join(packageRoot, "../engine/LoomleBridge/Source/LoomleBridge/Private/Sal"),
];
const sourceFiles = [
  join(packageRoot, "../engine/LoomleBridge/Source/LoomleBridge/Private/LoomleBridgeRpc.cpp"),
];

const catalog = JSON.parse(await readFile(catalogPath, "utf8")) as DiagnosticCatalog;
const catalogSchema = JSON.parse(await readFile(catalogSchemaPath, "utf8"));

const ajv = new Ajv2020({ allErrors: true, strict: false });
const validateCatalog = ajv.compile(catalogSchema);
assert.equal(
  validateCatalog(catalog),
  true,
  ajv.errorsText(validateCatalog.errors, { separator: "\n" }),
);
console.log("[PASS] diagnostic catalog matches its schema");

const byCode = new Map<string, DiagnosticDefinition>();
for (const diagnostic of catalog.diagnostics) {
  assert.equal(byCode.has(diagnostic.code), false, `Duplicate diagnostic code ${diagnostic.code}`);
  byCode.set(diagnostic.code, diagnostic);

  const prefix = diagnostic.code.split(".")[0];
  assert.equal(prefix, diagnostic.layer, `Diagnostic ${diagnostic.code} prefix must match layer ${diagnostic.layer}`);
}
console.log("[PASS] diagnostic catalog codes are unique and layer-consistent");

const usedCodes = await collectUsedDiagnosticCodes(sourceRoots, sourceFiles);
const missing = [...usedCodes].filter((code) => !byCode.has(code)).sort();
assert.deepEqual(missing, [], `Unregistered diagnostic codes: ${missing.join(", ")}`);
console.log("[PASS] source diagnostic codes are registered");

async function collectUsedDiagnosticCodes(roots: string[], files: string[]): Promise<Set<string>> {
  const codes = new Set<string>();
  for (const root of roots) {
    for (const file of await listSourceFiles(root)) {
      collectCodesFromSource(await readFile(file, "utf8"), codes);
    }
  }
  for (const file of files) {
    collectCodesFromSource(await readFile(file, "utf8"), codes);
  }
  return codes;
}

function collectCodesFromSource(source: string, codes: Set<string>): void {
  collectPattern(
    source,
    /["']((?:language|capability|resolution|validation|project|runtime|tool)\.[a-z][a-z0-9_]*)["']/g,
    codes,
  );
}

function collectPattern(source: string, pattern: RegExp, codes: Set<string>): void {
  for (const match of source.matchAll(pattern)) {
    codes.add(match[1]);
  }
}

async function listSourceFiles(root: string): Promise<string[]> {
  const files: string[] = [];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = join(root, entry.name);
    if (entry.isDirectory()) {
      files.push(...await listSourceFiles(path));
    } else if ([".ts", ".js", ".cpp", ".h"].includes(extname(entry.name))) {
      files.push(path);
    }
  }
  return files;
}
