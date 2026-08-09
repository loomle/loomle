#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import {
  cp,
  mkdir,
  readFile,
  readdir,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import {
  basename,
  dirname,
  join,
  relative,
  resolve,
} from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const DEFAULT_REPO_ROOT = fileURLToPath(new URL("../../", import.meta.url));
const TARGETS = new Map([
  ["darwin-arm64", { binariesDirectory: "Mac" }],
  ["win32-x64", { binariesDirectory: "Win64" }],
]);
const PRODUCTION_MODULE = "LoomleBridge";
const TEST_MODULE = "LoomleBridgeTests";

export async function prepareTestedPlugin({
  repoRoot,
  sourcePlugin,
  outputDir,
}) {
  const resolvedRepoRoot = resolve(repoRoot);
  const resolvedSourcePlugin = resolve(sourcePlugin);
  const resolvedOutputDir = resolve(outputDir);
  const pluginRoot = join(resolvedOutputDir, PRODUCTION_MODULE);
  assertSeparateTrees(resolvedSourcePlugin, resolvedOutputDir);

  const sourceDescriptor = await readJson(
    join(resolvedSourcePlugin, `${PRODUCTION_MODULE}.uplugin`),
  );
  const productionModule = requireModules(
    sourceDescriptor,
    [PRODUCTION_MODULE],
    "Fab source descriptor",
  )[0];
  const developmentPlugin = join(
    resolvedRepoRoot,
    "engine",
    PRODUCTION_MODULE,
  );
  const developmentDescriptor = await readJson(
    join(developmentPlugin, `${PRODUCTION_MODULE}.uplugin`),
  );
  const [, developmentTestModule] = requireModules(
    developmentDescriptor,
    [PRODUCTION_MODULE, TEST_MODULE],
    "development descriptor",
  );
  const testSource = join(
    developmentPlugin,
    "Source",
    TEST_MODULE,
  );
  await requireDirectory(testSource, "development test module");

  await rm(resolvedOutputDir, { recursive: true, force: true });
  await mkdir(resolvedOutputDir, { recursive: true });
  await cp(resolvedSourcePlugin, pluginRoot, { recursive: true });
  await cp(
    testSource,
    join(pluginRoot, "Source", TEST_MODULE),
    { recursive: true },
  );

  const testModule = structuredClone(developmentTestModule);
  testModule.PlatformAllowList = [...productionModule.PlatformAllowList];
  delete testModule.PlatformArchitectureAllowList;
  sourceDescriptor.Modules = [productionModule, testModule];
  await writeJson(
    join(pluginRoot, `${PRODUCTION_MODULE}.uplugin`),
    sourceDescriptor,
  );
  await assertNoBuildState(join(pluginRoot, "Source", TEST_MODULE));

  return { pluginRoot };
}

export async function finalizeTestedPlugin({
  pluginDir,
  target,
  receiptPath,
}) {
  const targetSpec = TARGETS.get(target);
  if (!targetSpec) {
    fail(
      `unsupported tested-plugin target ${JSON.stringify(target)}; accepted targets: ${[
        ...TARGETS.keys(),
      ].join(", ")}`,
    );
  }
  const pluginRoot = resolve(pluginDir);
  const descriptorPath = join(pluginRoot, `${PRODUCTION_MODULE}.uplugin`);
  const descriptor = await readJson(descriptorPath);
  const [productionModule] = requireModules(
    descriptor,
    [PRODUCTION_MODULE, TEST_MODULE],
    "compiled descriptor",
  );
  const binariesRoot = join(
    pluginRoot,
    "Binaries",
    targetSpec.binariesDirectory,
  );
  const manifestPath = await findEditorModulesManifest(binariesRoot);
  const manifest = await readJson(manifestPath);
  const manifestModules = manifest.Modules;
  if (!manifestModules
      || typeof manifestModules !== "object"
      || Array.isArray(manifestModules)
      || typeof manifestModules[PRODUCTION_MODULE] !== "string"
      || typeof manifestModules[TEST_MODULE] !== "string"
      || Object.keys(manifestModules).length !== 2) {
    fail(
      `compiled modules manifest must contain exactly ${PRODUCTION_MODULE} and ${TEST_MODULE}`,
    );
  }

  const productionBinary = join(
    binariesRoot,
    manifestModules[PRODUCTION_MODULE],
  );
  const testBinary = join(binariesRoot, manifestModules[TEST_MODULE]);
  await requireFile(productionBinary, "compiled production module binary");
  await requireFile(testBinary, "compiled test module binary");
  const productionSha256 = await sha256(productionBinary);

  await rm(join(pluginRoot, "Source", TEST_MODULE), {
    recursive: true,
    force: true,
  });
  await removeTestBinaryArtifacts(binariesRoot, manifestPath);
  descriptor.Modules = [productionModule];
  manifest.Modules = {
    [PRODUCTION_MODULE]: manifestModules[PRODUCTION_MODULE],
  };
  await writeJson(descriptorPath, descriptor);
  await writeJson(manifestPath, manifest);

  await requireFile(productionBinary, "final production module binary");
  const finalProductionSha256 = await sha256(productionBinary);
  if (finalProductionSha256 !== productionSha256) {
    fail("finalization changed the tested production module binary");
  }
  if (await pathExists(join(pluginRoot, "Source", TEST_MODULE))) {
    fail("final plugin still contains the test module source");
  }
  const remainingTestArtifacts = (await collectFiles(binariesRoot))
    .filter((path) => basename(path).includes(TEST_MODULE));
  if (remainingTestArtifacts.length > 0) {
    fail(
      `final plugin still contains test module binaries:\n${remainingTestArtifacts.join("\n")}`,
    );
  }

  const receipt = {
    schemaVersion: 1,
    target,
    productionModule: PRODUCTION_MODULE,
    removedTestModule: TEST_MODULE,
    productionBinary: relative(pluginRoot, productionBinary)
      .split(/[\\/]+/)
      .join("/"),
    productionSha256,
  };
  if (receiptPath) await writeJson(resolve(receiptPath), receipt);
  return receipt;
}

async function removeTestBinaryArtifacts(binariesRoot, manifestPath) {
  for (const path of await collectFiles(binariesRoot)) {
    if (path === manifestPath) continue;
    if (basename(path).includes(TEST_MODULE)) {
      await rm(path, { force: true });
    }
  }
}

async function findEditorModulesManifest(binariesRoot) {
  const candidates = (await collectFiles(binariesRoot)).filter(
    (path) => basename(path) === "UnrealEditor.modules",
  );
  if (candidates.length !== 1) {
    fail(
      `expected exactly one UnrealEditor.modules under ${binariesRoot}; found ${candidates.length}`,
    );
  }
  return candidates[0];
}

function requireModules(descriptor, expectedNames, label) {
  const modules = descriptor?.Modules;
  const actualNames = Array.isArray(modules)
    ? modules.map((module) => module?.Name)
    : [];
  if (JSON.stringify(actualNames) !== JSON.stringify(expectedNames)) {
    fail(`${label} modules must equal ${JSON.stringify(expectedNames)}`);
  }
  return modules;
}

function assertSeparateTrees(source, output) {
  const fromSource = relative(source, output);
  const fromOutput = relative(output, source);
  if (fromSource === ""
      || (!fromSource.startsWith("..") && !fromSource.startsWith("/"))
      || (!fromOutput.startsWith("..") && !fromOutput.startsWith("/"))) {
    fail("tested-plugin output directory must not overlap the Fab source plugin");
  }
}

async function assertNoBuildState(root) {
  const forbidden = (await collectFiles(root)).filter((path) => path
    .slice(root.length + 1)
    .split(/[\\/]+/)
    .some((part) => ["Binaries", "Intermediate", "Saved"].includes(part)));
  if (forbidden.length > 0) {
    fail(`development test module contains build state:\n${forbidden.join("\n")}`);
  }
}

async function collectFiles(root) {
  if (!(await pathExists(root))) return [];
  const files = [];
  const pending = [root];
  while (pending.length > 0) {
    const directory = pending.pop();
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      const path = join(directory, entry.name);
      if (entry.isDirectory()) pending.push(path);
      else if (entry.isFile()) files.push(path);
    }
  }
  return files;
}

async function requireDirectory(path, label) {
  if (!(await isDirectory(path))) fail(`${label} not found: ${path}`);
}

async function requireFile(path, label) {
  let info;
  try {
    info = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`${label} not found: ${path}`);
    throw error;
  }
  if (!info.isFile() || info.size === 0) fail(`${label} must be a non-empty file: ${path}`);
}

async function isDirectory(path) {
  try {
    return (await stat(path)).isDirectory();
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
}

async function pathExists(path) {
  try {
    await stat(path);
    return true;
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

async function writeJson(path, value) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, `${JSON.stringify(value, null, 2)}\n`);
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

function fail(message) {
  throw new Error(message);
}

function parseCli(argv) {
  const [command, ...rest] = argv;
  const values = new Map();
  for (let index = 0; index < rest.length; index += 1) {
    const raw = rest[index];
    if (!raw.startsWith("--")) fail(`unexpected argument: ${raw}`);
    const name = raw.slice(2);
    const value = rest[index + 1];
    if (!value || value.startsWith("--")) fail(`missing value for --${name}`);
    values.set(name, value);
    index += 1;
  }
  return { command, values };
}

async function runCli() {
  const { command, values } = parseCli(process.argv.slice(2));
  if (command === "prepare") {
    const result = await prepareTestedPlugin({
      repoRoot: values.get("repo-root") ?? DEFAULT_REPO_ROOT,
      sourcePlugin: values.get("source-plugin"),
      outputDir: values.get("output-dir"),
    });
    process.stdout.write(`${result.pluginRoot}\n`);
    return;
  }
  if (command === "finalize") {
    const result = await finalizeTestedPlugin({
      pluginDir: values.get("plugin-dir"),
      target: values.get("target"),
      receiptPath: values.get("receipt"),
    });
    process.stdout.write(`${JSON.stringify(result)}\n`);
    return;
  }
  fail("usage: tested-plugin.mjs <prepare|finalize> [options]");
}

if (process.argv[1]
    && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await runCli().catch((error) => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  });
}
