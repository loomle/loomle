#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { readdir, readFile, stat } from "node:fs/promises";
import { basename, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const TARGETS = new Map([
  ["darwin-arm64", {
    bridgeBinary: "Binaries/Mac/UnrealEditor-LoomleBridge.dylib",
    client: "Resources/Loomle/darwin-arm64/loomle",
  }],
  ["win32-x64", {
    bridgeBinary: "Binaries/Win64/UnrealEditor-LoomleBridge.dll",
    client: "Resources/Loomle/win32-x64/loomle.exe",
  }],
]);
const SOURCE_FORBIDDEN_ROOTS = new Set([
  "Binaries",
  "Intermediate",
  "Saved",
]);
const GITHUB_GENERATED_ROOTS = new Set([
  "Binaries",
]);
const DESCRIPTOR_PATH = "LoomleBridge.uplugin";

export async function verifyPackageDerivation({
  sourcePluginRoot,
  githubPluginRoot,
  target,
}) {
  const targetSpec = TARGETS.get(target);
  if (!targetSpec) {
    fail(`unsupported package target "${target}"; accepted targets: ${[...TARGETS.keys()].join(", ")}`);
  }

  const resolvedSourceRoot = resolve(sourcePluginRoot);
  const resolvedGithubRoot = resolve(githubPluginRoot);
  await requirePluginRoot(resolvedSourceRoot, "Fab source");
  await requirePluginRoot(resolvedGithubRoot, "GitHub");

  const sourceInventory = await inventory(resolvedSourceRoot);
  const githubInventory = await inventory(resolvedGithubRoot);
  assertFabSourceBoundary(sourceInventory);
  assertGithubBoundary(githubInventory);

  for (const sourceDirectory of sourceInventory.directories) {
    if (!githubInventory.directories.has(sourceDirectory)) {
      fail(`GitHub package omitted Fab source directory: ${sourceDirectory}`);
    }
  }

  let comparedFileCount = 0;
  for (const sourceFile of sourceInventory.files) {
    if (!githubInventory.files.has(sourceFile)) {
      fail(`GitHub package omitted Fab source file: ${sourceFile}`);
    }
    if (sourceFile === DESCRIPTOR_PATH) {
      await verifyDerivedDescriptor({
        sourcePath: join(resolvedSourceRoot, sourceFile),
        githubPath: join(resolvedGithubRoot, sourceFile),
      });
    } else {
      await assertSameFile(
        join(resolvedSourceRoot, sourceFile),
        join(resolvedGithubRoot, sourceFile),
        sourceFile,
      );
    }
    comparedFileCount += 1;
  }

  for (const githubFile of githubInventory.files) {
    if (sourceInventory.files.has(githubFile)) continue;
    const [root] = githubFile.split("/");
    if (!GITHUB_GENERATED_ROOTS.has(root)) {
      fail(`GitHub package contains a non-build file absent from Fab source: ${githubFile}`);
    }
  }
  for (const githubDirectory of githubInventory.directories) {
    if (sourceInventory.directories.has(githubDirectory)) continue;
    const [root] = githubDirectory.split("/");
    if (!GITHUB_GENERATED_ROOTS.has(root)) {
      fail(`GitHub package contains a non-build directory absent from Fab source: ${githubDirectory}`);
    }
  }

  if (!sourceInventory.files.has(targetSpec.client)) {
    fail(`Fab source is missing the target Client: ${targetSpec.client}`);
  }
  if (!githubInventory.files.has(targetSpec.bridgeBinary)) {
    fail(`GitHub package is missing the target Bridge binary: ${targetSpec.bridgeBinary}`);
  }

  return {
    comparedFileCount,
    githubGeneratedFileCount: [...githubInventory.files]
      .filter((path) => !sourceInventory.files.has(path))
      .length,
    target,
  };
}

async function requirePluginRoot(root, label) {
  let rootStat;
  try {
    rootStat = await stat(root);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`${label} plugin root not found: ${root}`);
    throw error;
  }
  if (!rootStat.isDirectory()) fail(`${label} plugin root is not a directory: ${root}`);
  const descriptor = join(root, DESCRIPTOR_PATH);
  try {
    if (!(await stat(descriptor)).isFile()) fail(`${label} descriptor is not a file: ${descriptor}`);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`${label} descriptor not found: ${descriptor}`);
    throw error;
  }
}

async function inventory(root) {
  const files = new Set();
  const directories = new Set();
  const pending = [root];
  while (pending.length > 0) {
    const directory = pending.pop();
    const entries = await readdir(directory, { withFileTypes: true });
    entries.sort((left, right) => left.name.localeCompare(right.name));
    for (const entry of entries) {
      const absolutePath = join(directory, entry.name);
      const relativePath = normalizePath(relative(root, absolutePath));
      if (entry.isSymbolicLink()) fail(`package must not contain symbolic links: ${relativePath}`);
      if (entry.isDirectory()) {
        directories.add(relativePath);
        pending.push(absolutePath);
      } else if (entry.isFile()) {
        files.add(relativePath);
      } else {
        fail(`package contains an unsupported filesystem entry: ${relativePath}`);
      }
    }
  }
  return { directories, files };
}

function assertFabSourceBoundary(sourceInventory) {
  for (const path of [...sourceInventory.directories, ...sourceInventory.files]) {
    const [root] = path.split("/");
    if (SOURCE_FORBIDDEN_ROOTS.has(root)) {
      fail(`Fab source package contains forbidden generated path: ${path}`);
    }
  }
  for (const requiredDirectory of ["Config", "Content", "Resources", "Source"]) {
    if (!sourceInventory.directories.has(requiredDirectory)) {
      fail(`Fab source package is missing required directory: ${requiredDirectory}`);
    }
  }
}

function assertGithubBoundary(githubInventory) {
  for (const path of [...githubInventory.directories, ...githubInventory.files]) {
    const [root] = path.split("/");
    if (root === "Intermediate" || root === "Saved") {
      fail(`GitHub package contains forbidden generated path: ${path}`);
    }
  }
  if (!githubInventory.directories.has("Source")) {
    fail("GitHub package must retain the complete Fab Source directory.");
  }
}

async function verifyDerivedDescriptor({ sourcePath, githubPath }) {
  const sourceDescriptor = await readJson(sourcePath, "Fab source descriptor");
  const githubDescriptor = await readJson(githubPath, "GitHub descriptor");
  if (githubDescriptor.Installed !== true) {
    fail("GitHub descriptor must set Installed=true.");
  }

  const normalizedSource = normalizeDescriptor(sourceDescriptor);
  const normalizedGithub = normalizeDescriptor(githubDescriptor);
  if (JSON.stringify(normalizedSource) !== JSON.stringify(normalizedGithub)) {
    fail("GitHub descriptor differs from the Fab source beyond BuildPlugin installation fields.");
  }
}

function normalizeDescriptor(descriptor) {
  const normalized = structuredClone(descriptor);
  delete normalized.Installed;
  if (normalized.MarketplaceURL === "") delete normalized.MarketplaceURL;
  return sortJson(normalized);
}

function sortJson(value) {
  if (Array.isArray(value)) return value.map(sortJson);
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.entries(value)
        .sort(([left], [right]) => left.localeCompare(right))
        .map(([key, child]) => [key, sortJson(child)]),
    );
  }
  return value;
}

async function readJson(path, label) {
  try {
    return JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    fail(`${label} is not valid JSON (${basename(path)}): ${error.message}`);
  }
}

async function assertSameFile(sourcePath, githubPath, label) {
  const [sourceStat, githubStat] = await Promise.all([
    stat(sourcePath),
    stat(githubPath),
  ]);
  if (sourceStat.size !== githubStat.size) {
    fail(`GitHub package changed Fab source file: ${label}`);
  }
  const [sourceHash, githubHash] = await Promise.all([
    sha256(sourcePath),
    sha256(githubPath),
  ]);
  if (sourceHash !== githubHash) {
    fail(`GitHub package changed Fab source file: ${label}`);
  }
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

function normalizePath(path) {
  return path.split(/[\\/]+/).join("/");
}

function fail(message) {
  throw new Error(message);
}

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (!argument.startsWith("--")) fail(`unexpected argument: ${argument}`);
    const key = argument.slice(2);
    const value = argv[index + 1];
    if (!value || value.startsWith("--")) fail(`missing value for --${key}`);
    options[key] = value;
    index += 1;
  }
  for (const required of ["source-plugin", "github-plugin", "target"]) {
    if (!options[required]) fail(`missing required option --${required}`);
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const result = await verifyPackageDerivation({
    sourcePluginRoot: options["source-plugin"],
    githubPluginRoot: options["github-plugin"],
    target: options.target,
  });
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
}

const isEntryPoint = process.argv[1]
  && import.meta.url === pathToFileURL(resolve(process.argv[1])).href;

if (isEntryPoint) {
  main().catch((error) => {
    process.stderr.write(`${error.stack ?? error.message}\n`);
    process.exitCode = 1;
  });
}
