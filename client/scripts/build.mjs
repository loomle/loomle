import { mkdir, writeFile } from "node:fs/promises";
import { builtinModules } from "node:module";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const clientRoot = fileURLToPath(new URL("../", import.meta.url));
const outputPath = resolve(clientRoot, "dist/main.cjs");
const allowedExternalImports = new Set([
  ...builtinModules,
  ...builtinModules.map((name) => `node:${name}`),
]);

const result = await build({
  absWorkingDir: clientRoot,
  entryPoints: ["src/main.ts"],
  outfile: "dist/main.cjs",
  bundle: true,
  platform: "node",
  target: "node20",
  format: "cjs",
  packages: "bundle",
  treeShaking: true,
  minify: false,
  sourcemap: false,
  charset: "utf8",
  legalComments: "eof",
  metafile: true,
  write: false,
});

const outputs = Object.keys(result.metafile.outputs);
if (outputs.length !== 1 || result.outputFiles.length !== 1) {
  throw new Error(
    `Client bundle must contain exactly one file; esbuild produced: ${outputs.join(", ")}.`,
  );
}

const externalImports = new Set(
  Object.values(result.metafile.outputs).flatMap((output) => output.imports
    .filter((entry) => entry.external)
    .map((entry) => entry.path)),
);
const unsupportedExternalImports = [...externalImports]
  .filter((path) => !allowedExternalImports.has(path))
  .sort();
if (unsupportedExternalImports.length > 0) {
  throw new Error(
    `Client bundle has non-built-in external imports: ${unsupportedExternalImports.join(", ")}.`,
  );
}

const [output] = result.outputFiles;
if (resolve(output.path) !== outputPath) {
  throw new Error(`Unexpected Client bundle path: ${output.path}.`);
}

await mkdir(dirname(outputPath), { recursive: true });
await writeFile(outputPath, output.contents);
console.log(`Built self-contained Client bundle: ${outputPath}`);
