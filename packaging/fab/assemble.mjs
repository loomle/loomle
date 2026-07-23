#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import {
  chmod,
  copyFile,
  cp,
  mkdir,
  readFile,
  readdir,
  realpath,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { basename, dirname, isAbsolute, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { checkProductVersion } from "../tools/product-version.mjs";

const DEFAULT_REPO_ROOT = fileURLToPath(new URL("../../", import.meta.url));
const TARGETS = new Map([
  ["darwin-arm64", {
    executableName: "loomle",
    unrealPlatform: "Mac",
    unrealArchitecture: "arm64",
  }],
]);
const IGNORED_SOURCE_NAMES = new Set([
  ".DS_Store",
  "Binaries",
  "Intermediate",
  "Saved",
  "tests",
]);
const FORBIDDEN_BINARY_SUFFIXES = new Set([
  ".dll",
  ".dylib",
  ".exe",
  ".lib",
  ".pdb",
  ".so",
]);

export async function assembleFabPlugin({ repoRoot, outputDir, target }) {
  const resolvedRepoRoot = resolve(repoRoot);
  const resolvedOutputDir = resolve(outputDir);
  const targetSpec = resolveTarget(target);
  await checkProductVersion(resolvedRepoRoot);

  const executableName = targetSpec.executableName;
  const clientSource = join(
    resolvedRepoRoot,
    ".tmp",
    "client",
    target,
    executableName,
  );
  const clientReceipt = join(dirname(clientSource), "build.json");
  const enginePlugin = join(resolvedRepoRoot, "engine", "LoomleBridge");
  const fabReadme = join(resolvedRepoRoot, "packaging", "fab", "FAB_PLUGIN_README.md");
  const pluginRoot = join(resolvedOutputDir, "LoomleBridge");
  const stagedClient = join(pluginRoot, "Resources", "Loomle", target, executableName);

  const assemblyInputs = [clientSource, clientReceipt, enginePlugin, fabReadme];
  await assertNoOutputOverlap(resolvedOutputDir, assemblyInputs);
  const clientBuild = await validateClientBuild({
    repoRoot: resolvedRepoRoot,
    executablePath: clientSource,
    receiptPath: clientReceipt,
    target,
  });
  await assertNoOutputOverlap(resolvedOutputDir, assemblyInputs);
  await resetDirectory(resolvedOutputDir);
  await copyPluginSource(enginePlugin, pluginRoot);
  await copyRequiredFile(fabReadme, join(pluginRoot, "README.md"));
  await specializeDescriptor({
    descriptorPath: join(pluginRoot, "LoomleBridge.uplugin"),
    targetSpec,
  });

  // Release artifacts never come from the source plugin tree. Recreate the
  // Client resource boundary from the one canonical executable input.
  await rm(join(pluginRoot, "Resources", "Loomle"), { recursive: true, force: true });
  await rm(join(pluginRoot, "Resources", "MCP"), { recursive: true, force: true });
  await copyRequiredFile(clientSource, stagedClient);
  if (!target.startsWith("win32-")) await chmod(stagedClient, 0o755);

  await rm(join(pluginRoot, "Content"), { recursive: true, force: true });
  await validateFabPlugin({
    repoRoot: resolvedRepoRoot,
    pluginRoot,
    stagedClient,
    target,
    targetSpec,
    expectedClientSha256: clientBuild.sha256,
  });

  return {
    pluginRoot,
    client: stagedClient,
    clientSha256: await sha256(stagedClient),
    target,
    packageKind: "fab-plugin",
  };
}

async function resetDirectory(path) {
  await rm(path, { recursive: true, force: true });
  await mkdir(path, { recursive: true });
}

async function assertNoOutputOverlap(outputDir, inputs) {
  const canonicalOutput = await canonicalizeForContainment(outputDir);
  for (const input of inputs) {
    const canonicalInput = await canonicalizeForContainment(input);
    if (isWithin(canonicalInput, canonicalOutput)
        || isWithin(canonicalOutput, canonicalInput)) {
      fail(`output directory must not overlap an assembly input: ${input}`);
    }
  }
}

async function canonicalizeForContainment(path) {
  let existingAncestor = resolve(path);
  const missingSegments = [];
  while (true) {
    try {
      const canonicalAncestor = await realpath(existingAncestor);
      return resolve(canonicalAncestor, ...missingSegments);
    } catch (error) {
      if (error?.code !== "ENOENT" && error?.code !== "ENOTDIR") throw error;
      const parent = dirname(existingAncestor);
      if (parent === existingAncestor) throw error;
      missingSegments.unshift(basename(existingAncestor));
      existingAncestor = parent;
    }
  }
}

async function copyPluginSource(source, destination) {
  if (!(await isDirectory(source))) fail(`source not found: ${source}`);
  await cp(source, destination, {
    recursive: true,
    filter: (path) => !IGNORED_SOURCE_NAMES.has(basename(path)),
  });
}

async function copyRequiredFile(source, destination) {
  if (!(await isFile(source))) fail(`file not found: ${source}`);
  await mkdir(dirname(destination), { recursive: true });
  await copyFile(source, destination);
}

async function validateClientExecutable(path, target) {
  let fileStat;
  try {
    fileStat = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") {
      fail(`canonical Client executable not found: ${path}`);
    }
    throw error;
  }
  if (!fileStat.isFile() || fileStat.size === 0) {
    fail(`canonical Client executable must be a non-empty file: ${path}`);
  }
  if (!target.startsWith("win32-") && (fileStat.mode & 0o111) === 0) {
    fail(`canonical Client executable is not executable: ${path}`);
  }
}

async function validateClientBuild({ repoRoot, executablePath, receiptPath, target }) {
  await validateClientExecutable(executablePath, target);
  if (!(await isFile(receiptPath))) {
    fail(`canonical Client build receipt not found: ${receiptPath}`);
  }
  const receipt = await readJson(receiptPath);
  const product = await readJson(join(repoRoot, "package.json"));
  const runtimeManifest = await readJson(
    join(repoRoot, "packaging", "client", "node-runtime.json"),
  );
  if (receipt.schemaVersion !== 2) {
    fail("canonical Client build receipt must use schemaVersion 2.");
  }
  if (receipt.productVersion !== product.version) {
    fail(
      `canonical Client build receipt productVersion ${JSON.stringify(receipt.productVersion)}`
      + ` does not match product version ${JSON.stringify(product.version)}.`,
    );
  }
  if (receipt.protocolVersion !== product.loomle?.protocolVersion) {
    fail(
      `canonical Client build receipt protocolVersion ${JSON.stringify(receipt.protocolVersion)}`
      + ` does not match protocol version ${JSON.stringify(product.loomle?.protocolVersion)}.`,
    );
  }
  if (receipt.target !== target) {
    fail(
      `canonical Client build receipt target ${JSON.stringify(receipt.target)}`
      + ` does not match requested target ${JSON.stringify(target)}.`,
    );
  }
  if (receipt.executable !== basename(executablePath)) {
    fail(
      `canonical Client build receipt executable ${JSON.stringify(receipt.executable)}`
      + ` does not match ${JSON.stringify(basename(executablePath))}.`,
    );
  }
  if (receipt.nodeVersion !== runtimeManifest.nodeVersion) {
    fail(
      `canonical Client build receipt nodeVersion ${JSON.stringify(receipt.nodeVersion)}`
      + ` does not match pinned Node version ${JSON.stringify(runtimeManifest.nodeVersion)}.`,
    );
  }
  const pinnedRuntimeSha256 = runtimeManifest.targets?.[target]?.sha256;
  if (receipt.runtimeSha256 !== pinnedRuntimeSha256) {
    fail(
      `canonical Client build receipt runtimeSha256 ${JSON.stringify(receipt.runtimeSha256)}`
      + ` does not match pinned runtime SHA-256 ${JSON.stringify(pinnedRuntimeSha256)}.`,
    );
  }
  if (typeof receipt.sha256 !== "string" || !/^[0-9a-f]{64}$/.test(receipt.sha256)) {
    fail("canonical Client build receipt must contain a lowercase SHA-256 digest.");
  }
  const actualSha256 = await sha256(executablePath);
  if (receipt.sha256 !== actualSha256) {
    fail(
      `canonical Client executable SHA-256 ${actualSha256}`
      + ` does not match build receipt ${receipt.sha256}.`,
    );
  }
  return receipt;
}

async function specializeDescriptor({ descriptorPath, targetSpec }) {
  const descriptor = await readJson(descriptorPath);
  const modules = descriptor.Modules;
  if (!Array.isArray(modules)) {
    fail("source LoomleBridge.uplugin must contain a Modules array.");
  }
  const module = modules.find((entry) => entry?.Name === "LoomleBridge");
  if (!module) {
    fail("source LoomleBridge.uplugin must contain the LoomleBridge module.");
  }
  if (descriptor.SupportedTargetPlatforms !== undefined
      && (!Array.isArray(descriptor.SupportedTargetPlatforms)
        || !descriptor.SupportedTargetPlatforms.includes(targetSpec.unrealPlatform))) {
    fail(`source LoomleBridge plugin does not support ${targetSpec.unrealPlatform}.`);
  }
  if (!Array.isArray(module.PlatformAllowList)
      || !module.PlatformAllowList.includes(targetSpec.unrealPlatform)) {
    fail(`source LoomleBridge module does not allow ${targetSpec.unrealPlatform}.`);
  }

  descriptor.SupportedTargetPlatforms = [targetSpec.unrealPlatform];
  module.PlatformAllowList = [targetSpec.unrealPlatform];
  module.PlatformArchitectureAllowList = [
    `${targetSpec.unrealPlatform}:${targetSpec.unrealArchitecture}`,
  ];
  await writeFile(descriptorPath, `${JSON.stringify(descriptor, null, 2)}\n`);
}

async function validateFabPlugin({
  repoRoot,
  pluginRoot,
  stagedClient,
  target,
  targetSpec,
  expectedClientSha256,
}) {
  const requiredDirectories = [
    join(pluginRoot, "Config"),
    join(pluginRoot, "Source"),
    join(pluginRoot, "Resources", "Loomle", target),
  ];
  const descriptorPath = join(pluginRoot, "LoomleBridge.uplugin");
  const filterPath = join(pluginRoot, "Config", "FilterPlugin.ini");
  const requiredFiles = [
    descriptorPath,
    join(pluginRoot, "README.md"),
    join(pluginRoot, "Source", "LoomleBridge", "LoomleBridge.Build.cs"),
    filterPath,
    stagedClient,
  ];
  const missing = [];
  for (const path of requiredDirectories) {
    if (!(await isDirectory(path))) missing.push(path);
  }
  for (const path of requiredFiles) {
    if (!(await isFile(path))) missing.push(path);
  }
  if (missing.length > 0) {
    fail(`Fab plugin staging is missing required files:\n${missing.join("\n")}`);
  }
  if (await pathExists(join(pluginRoot, "Content"))) {
    fail("Fab plugin must not include a Content directory when CanContainContent=false.");
  }
  if (await pathExists(join(pluginRoot, "Resources", "MCP"))) {
    fail("Fab plugin must not include the retired Resources/MCP directory.");
  }

  await validateDescriptor(repoRoot, descriptorPath, targetSpec);
  await validateFilterPlugin(filterPath);
  await validateOnlyClientTarget(pluginRoot, target);
  await validateClientExecutable(stagedClient, target);
  const stagedClientSha256 = await sha256(stagedClient);
  if (stagedClientSha256 !== expectedClientSha256) {
    fail(
      `staged Client SHA-256 ${stagedClientSha256}`
      + ` does not match verified build receipt ${expectedClientSha256}.`,
    );
  }

  const forbidden = [];
  for await (const path of walk(pluginRoot)) {
    const relativeParts = path.slice(pluginRoot.length + 1).split(/[\\/]/);
    if (relativeParts.some((part) => ["Binaries", "Intermediate", "Saved"].includes(part))) {
      forbidden.push(path);
      continue;
    }
    if (!(await isFile(path)) || path === stagedClient) continue;
    const suffix = path.slice(path.lastIndexOf(".")).toLowerCase();
    if (FORBIDDEN_BINARY_SUFFIXES.has(suffix)) forbidden.push(path);
  }
  if (forbidden.length > 0) {
    fail(`Fab plugin contains unexpected platform/build outputs:\n${forbidden.join("\n")}`);
  }
}

async function validateDescriptor(repoRoot, descriptorPath, targetSpec) {
  const rootPackage = await readJson(join(repoRoot, "package.json"));
  const descriptor = await readJson(descriptorPath);
  if (typeof rootPackage.version !== "string" || rootPackage.version.length === 0) {
    fail("package.json must contain a product version.");
  }
  if (descriptor.VersionName !== rootPackage.version) {
    fail(
      `staged LoomleBridge.uplugin VersionName ${JSON.stringify(descriptor.VersionName)}`
      + ` does not match product version ${JSON.stringify(rootPackage.version)}.`,
    );
  }
  if (!Number.isInteger(descriptor.Version) || descriptor.Version < 1) {
    fail("staged LoomleBridge.uplugin Version must be a positive Fab build number.");
  }
  if (descriptor.CanContainContent !== false) {
    fail("staged LoomleBridge.uplugin must set CanContainContent=false.");
  }
  assertExactArray(
    descriptor.SupportedTargetPlatforms,
    [targetSpec.unrealPlatform],
    "staged LoomleBridge.uplugin SupportedTargetPlatforms",
  );
  const module = descriptor.Modules?.find((entry) => entry?.Name === "LoomleBridge");
  if (!module) {
    fail("staged LoomleBridge.uplugin is missing the LoomleBridge module.");
  }
  assertExactArray(
    module.PlatformAllowList,
    [targetSpec.unrealPlatform],
    "staged LoomleBridge module PlatformAllowList",
  );
  assertExactArray(
    module.PlatformArchitectureAllowList,
    [`${targetSpec.unrealPlatform}:${targetSpec.unrealArchitecture}`],
    "staged LoomleBridge module PlatformArchitectureAllowList",
  );
}

function assertExactArray(actual, expected, label) {
  if (!Array.isArray(actual)
      || actual.length !== expected.length
      || actual.some((value, index) => value !== expected[index])) {
    fail(`${label} must equal ${JSON.stringify(expected)}.`);
  }
}

async function validateFilterPlugin(filterPath) {
  const entries = new Set(
    (await readFile(filterPath, "utf8"))
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line.length > 0 && !line.startsWith(";") && line !== "[FilterPlugin]"),
  );
  const required = [
    "/Config/FilterPlugin.ini",
    "/Resources/Loomle/...",
    "/Resources/LoomleToolbarIcon.png",
    "/README.md",
  ];
  const missing = required.filter((entry) => !entries.has(entry));
  if (missing.length > 0) {
    fail(`Config/FilterPlugin.ini is missing required entries:\n${missing.join("\n")}`);
  }
  if (entries.has("/Resources/MCP/...")) {
    fail("Config/FilterPlugin.ini still contains the retired /Resources/MCP/... entry.");
  }
}

async function validateOnlyClientTarget(pluginRoot, target) {
  const clientRoot = join(pluginRoot, "Resources", "Loomle");
  const entries = await readdir(clientRoot, { withFileTypes: true });
  if (entries.length !== 1 || !entries[0].isDirectory() || entries[0].name !== target) {
    fail(`Resources/Loomle must contain only the staged ${target} Client.`);
  }
}

async function readJson(path) {
  try {
    return JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    fail(`cannot read JSON ${path}: ${error.message}`);
  }
}

async function isFile(path) {
  try {
    return (await stat(path)).isFile();
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
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

async function* walk(root) {
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = join(root, entry.name);
    yield path;
    if (entry.isDirectory()) yield* walk(path);
  }
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

function resolveTarget(target) {
  const targetSpec = TARGETS.get(target);
  if (!targetSpec) {
    fail(
      `unsupported Fab target ${JSON.stringify(target)}; accepted targets: `
      + `${[...TARGETS.keys()].join(", ")}.`,
    );
  }
  return targetSpec;
}

function isWithin(path, directory) {
  const pathFromDirectory = relative(directory, path);
  return pathFromDirectory === ""
    || (!pathFromDirectory.startsWith("..") && !isAbsolute(pathFromDirectory));
}

function fail(message) {
  throw new Error(message);
}

function parseArguments(args) {
  let repoRoot = DEFAULT_REPO_ROOT;
  let outputDir;
  let target;
  for (let index = 0; index < args.length; index += 2) {
    const flag = args[index];
    const value = args[index + 1];
    if (!value) usage();
    if (flag === "--repo-root") repoRoot = value;
    else if (flag === "--output-dir") outputDir = value;
    else if (flag === "--target") target = value;
    else usage();
  }
  if (!outputDir || !target) usage();
  return { repoRoot, outputDir, target };
}

function usage() {
  throw new Error(
    "Usage: node packaging/fab/assemble.mjs --output-dir <path> --target <platform-arch>"
    + " [--repo-root <path>]",
  );
}

async function main() {
  const result = await assembleFabPlugin(parseArguments(process.argv.slice(2)));
  process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main().catch((error) => {
    process.stderr.write(`[FAIL] ${error.message}\n`);
    process.exitCode = 1;
  });
}
