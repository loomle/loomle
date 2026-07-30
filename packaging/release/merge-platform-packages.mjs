#!/usr/bin/env node

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import {
  chmod,
  cp,
  mkdir,
  readFile,
  readdir,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { basename, dirname, isAbsolute, join, relative, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import { verifyPackageDerivation } from "../fab/verify-derivation.mjs";

const DESCRIPTOR_PATH = "LoomleBridge.uplugin";
const PLATFORMS = [
  {
    target: "darwin-arm64",
    unrealPlatform: "Mac",
    client: "Resources/Loomle/darwin-arm64/loomle",
    binaries: "Binaries/Mac",
  },
  {
    target: "win32-x64",
    unrealPlatform: "Win64",
    client: "Resources/Loomle/win32-x64/loomle.exe",
    binaries: "Binaries/Win64",
  },
];
const PLATFORM_CLIENT_ROOTS = PLATFORMS.map(
  ({ target }) => `Resources/Loomle/${target}`,
);

export async function mergePlatformPackages({
  macSourcePluginRoot,
  macPluginRoot,
  windowsSourcePluginRoot,
  windowsPluginRoot,
  outputSourcePluginRoot,
  outputPluginRoot,
}) {
  const inputs = {
    "darwin-arm64": {
      source: resolve(macSourcePluginRoot),
      plugin: resolve(macPluginRoot),
    },
    "win32-x64": {
      source: resolve(windowsSourcePluginRoot),
      plugin: resolve(windowsPluginRoot),
    },
  };
  const outputSource = resolve(outputSourcePluginRoot);
  const outputPlugin = resolve(outputPluginRoot);
  assertDistinctRoots([
    ...Object.values(inputs).flatMap(({ source, plugin }) => [source, plugin]),
    outputSource,
    outputPlugin,
  ]);

  for (const platform of PLATFORMS) {
    const pair = inputs[platform.target];
    await verifyPackageDerivation({
      sourcePluginRoot: pair.source,
      githubPluginRoot: pair.plugin,
      target: platform.target,
    });
    await validateNativeDescriptor(pair.source, platform, false);
    await validateNativeDescriptor(pair.plugin, platform, true);
    await requireNonEmptyFile(join(pair.source, platform.client), platform.client);
    await requireDirectory(join(pair.plugin, platform.binaries), platform.binaries);
  }

  const macSourceInventory = await inventory(inputs["darwin-arm64"].source);
  const windowsSourceInventory = await inventory(inputs["win32-x64"].source);
  const sharedSourceFiles = await assertSharedSourceIdentity({
    macRoot: inputs["darwin-arm64"].source,
    macInventory: macSourceInventory,
    windowsRoot: inputs["win32-x64"].source,
    windowsInventory: windowsSourceInventory,
  });

  const mergedSourceDescriptor = await createMergedDescriptor(
    join(inputs["darwin-arm64"].source, DESCRIPTOR_PATH),
    false,
  );
  const mergedPluginDescriptor = {
    ...structuredClone(mergedSourceDescriptor),
    Installed: true,
  };

  await resetDirectory(outputSource);
  await cp(inputs["darwin-arm64"].source, outputSource, { recursive: true });
  await mkdir(dirname(join(outputSource, PLATFORMS[1].client)), { recursive: true });
  await cp(
    join(inputs["win32-x64"].source, PLATFORMS[1].client),
    join(outputSource, PLATFORMS[1].client),
  );
  await normalizeSharedTextFiles(outputSource, sharedSourceFiles);
  await writeDescriptor(outputSource, mergedSourceDescriptor);
  if (process.platform !== "win32") {
    await chmod(join(outputSource, PLATFORMS[0].client), 0o755);
  }

  await resetDirectory(outputPlugin);
  await cp(outputSource, outputPlugin, { recursive: true });
  await mkdir(join(outputPlugin, "Binaries"), { recursive: true });
  for (const platform of PLATFORMS) {
    await cp(
      join(inputs[platform.target].plugin, platform.binaries),
      join(outputPlugin, platform.binaries),
      { recursive: true },
    );
  }
  await writeDescriptor(outputPlugin, mergedPluginDescriptor);

  await validateMergedPackage(outputSource, false);
  await validateMergedPackage(outputPlugin, true);
  await verifyMergedDerivation(outputSource, outputPlugin);

  return {
    outputPluginRoot: outputPlugin,
    outputSourcePluginRoot: outputSource,
    targets: PLATFORMS.map(({ target }) => target),
  };
}

async function assertSharedSourceIdentity({
  macRoot,
  macInventory,
  windowsRoot,
  windowsInventory,
}) {
  const macSharedFiles = sharedPaths(macInventory.files);
  const windowsSharedFiles = sharedPaths(windowsInventory.files);
  assertSamePathSet(macSharedFiles, windowsSharedFiles, "shared source file");

  const macSharedDirectories = sharedPaths(macInventory.directories);
  const windowsSharedDirectories = sharedPaths(windowsInventory.directories);
  assertSamePathSet(
    macSharedDirectories,
    windowsSharedDirectories,
    "shared source directory",
  );

  for (const path of macSharedFiles) {
    if (path === DESCRIPTOR_PATH) continue;
    await assertSameSharedFile(join(macRoot, path), join(windowsRoot, path), path);
  }

  const macDescriptor = normalizePlatformDescriptor(
    await readJson(join(macRoot, DESCRIPTOR_PATH), "Mac source descriptor"),
  );
  const windowsDescriptor = normalizePlatformDescriptor(
    await readJson(join(windowsRoot, DESCRIPTOR_PATH), "Windows source descriptor"),
  );
  if (JSON.stringify(macDescriptor) !== JSON.stringify(windowsDescriptor)) {
    fail("native source descriptors differ beyond their platform allow-lists.");
  }
  return macSharedFiles;
}

function sharedPaths(paths) {
  return new Set(
    [...paths].filter(
      (path) => !PLATFORM_CLIENT_ROOTS.some(
        (root) => path === root || path.startsWith(`${root}/`),
      ),
    ),
  );
}

function assertSamePathSet(left, right, label) {
  for (const path of left) {
    if (!right.has(path)) fail(`Windows fragment is missing ${label}: ${path}`);
  }
  for (const path of right) {
    if (!left.has(path)) fail(`Mac fragment is missing ${label}: ${path}`);
  }
}

async function validateNativeDescriptor(root, platform, installed) {
  const descriptor = await readJson(
    join(root, DESCRIPTOR_PATH),
    `${platform.target} descriptor`,
  );
  const module = requireSingleModule(descriptor);
  if (!same(descriptor.SupportedTargetPlatforms, [platform.unrealPlatform])
      || !same(module.PlatformAllowList, [platform.unrealPlatform])
      || module.PlatformArchitectureAllowList !== undefined
      || (installed && descriptor.Installed !== true)
      || (!installed && descriptor.Installed === true)) {
    fail(`${platform.target} descriptor does not match its native package role.`);
  }
}

async function createMergedDescriptor(path, installed) {
  const descriptor = await readJson(path, "source descriptor");
  const module = requireSingleModule(descriptor);
  descriptor.SupportedTargetPlatforms = PLATFORMS.map(
    ({ unrealPlatform }) => unrealPlatform,
  );
  module.PlatformAllowList = PLATFORMS.map(
    ({ unrealPlatform }) => unrealPlatform,
  );
  delete module.PlatformArchitectureAllowList;
  if (installed) descriptor.Installed = true;
  else delete descriptor.Installed;
  return descriptor;
}

function normalizePlatformDescriptor(descriptor) {
  const normalized = structuredClone(descriptor);
  delete normalized.Installed;
  if (normalized.MarketplaceURL === "") delete normalized.MarketplaceURL;
  if (normalized.IsBetaVersion === false) delete normalized.IsBetaVersion;
  normalized.SupportedTargetPlatforms = ["<native>"];
  const module = requireSingleModule(normalized);
  module.PlatformAllowList = ["<native>"];
  delete module.PlatformArchitectureAllowList;
  return sortJson(normalized);
}

async function validateMergedPackage(root, installed) {
  const packageInventory = await inventory(root);
  const descriptor = await readJson(join(root, DESCRIPTOR_PATH), "merged descriptor");
  const module = requireSingleModule(descriptor);
  const platforms = PLATFORMS.map(({ unrealPlatform }) => unrealPlatform);
  if (!same(descriptor.SupportedTargetPlatforms, platforms)
      || !same(module.PlatformAllowList, platforms)
      || module.PlatformArchitectureAllowList !== undefined
      || (installed ? descriptor.Installed !== true : descriptor.Installed === true)) {
    fail("merged descriptor does not match the cross-platform package role.");
  }

  for (const platform of PLATFORMS) {
    if (!packageInventory.files.has(platform.client)) {
      fail(`merged package is missing Client: ${platform.client}`);
    }
    if (installed && !packageInventory.directories.has(platform.binaries)) {
      fail(`merged package is missing Bridge binary directory: ${platform.binaries}`);
    }
  }
  for (const path of [...packageInventory.files, ...packageInventory.directories]) {
    const [top] = path.split("/");
    if (top === "Intermediate" || top === "Saved" || (!installed && top === "Binaries")) {
      fail(`merged package contains forbidden generated path: ${path}`);
    }
  }
}

async function verifyMergedDerivation(sourceRoot, pluginRoot) {
  const sourceInventory = await inventory(sourceRoot);
  const pluginInventory = await inventory(pluginRoot);
  for (const directory of sourceInventory.directories) {
    if (!pluginInventory.directories.has(directory)) {
      fail(`complete package omitted source directory: ${directory}`);
    }
  }
  for (const file of sourceInventory.files) {
    if (!pluginInventory.files.has(file)) {
      fail(`complete package omitted source file: ${file}`);
    }
    if (file === DESCRIPTOR_PATH) continue;
    await assertSameFile(join(sourceRoot, file), join(pluginRoot, file), file);
  }
  for (const path of [...pluginInventory.files, ...pluginInventory.directories]) {
    if (sourceInventory.files.has(path) || sourceInventory.directories.has(path)) continue;
    if (path !== "Binaries" && !path.startsWith("Binaries/")) {
      fail(`complete package contains non-build addition: ${path}`);
    }
  }
}

function requireSingleModule(descriptor) {
  if (!Array.isArray(descriptor.Modules)
      || descriptor.Modules.length !== 1
      || descriptor.Modules[0]?.Name !== "LoomleBridge") {
    fail("descriptor must contain exactly the LoomleBridge module.");
  }
  return descriptor.Modules[0];
}

async function inventory(root) {
  await requireDirectory(root, "plugin root");
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
        fail(`package contains unsupported filesystem entry: ${relativePath}`);
      }
    }
  }
  return { directories, files };
}

async function requireDirectory(path, label) {
  let value;
  try {
    value = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`${label} not found: ${path}`);
    throw error;
  }
  if (!value.isDirectory()) fail(`${label} is not a directory: ${path}`);
}

async function requireNonEmptyFile(path, label) {
  let value;
  try {
    value = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`required file not found: ${label}`);
    throw error;
  }
  if (!value.isFile() || value.size === 0) fail(`required file is empty: ${label}`);
}

async function assertSameFile(left, right, label) {
  const [leftStat, rightStat] = await Promise.all([stat(left), stat(right)]);
  if (leftStat.size !== rightStat.size
      || await sha256(left) !== await sha256(right)) {
    fail(`native fragments changed shared source file: ${label}`);
  }
}

async function assertSameSharedFile(left, right, label) {
  const [leftBytes, rightBytes] = await Promise.all([
    readFile(left),
    readFile(right),
  ]);
  if (leftBytes.equals(rightBytes)) return;
  const leftText = normalizeTextLineEndings(leftBytes);
  const rightText = normalizeTextLineEndings(rightBytes);
  if (leftText === null || rightText === null || !leftText.equals(rightText)) {
    fail(`native fragments changed shared source file: ${label}`);
  }
}

async function normalizeSharedTextFiles(root, paths) {
  for (const path of paths) {
    if (path === DESCRIPTOR_PATH) continue;
    const absolutePath = join(root, path);
    const original = await readFile(absolutePath);
    const normalized = normalizeTextLineEndings(original);
    if (normalized !== null && !normalized.equals(original)) {
      await writeFile(absolutePath, normalized);
    }
  }
}

function normalizeTextLineEndings(bytes) {
  let text;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    return null;
  }
  if (text.includes("\0") || /\r(?!\n)/.test(text)) return null;
  return Buffer.from(text.replace(/\r\n/g, "\n"), "utf8");
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

async function readJson(path, label) {
  try {
    return JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    fail(`${label} is not valid JSON (${basename(path)}): ${error.message}`);
  }
}

async function writeDescriptor(root, descriptor) {
  await writeFile(
    join(root, DESCRIPTOR_PATH),
    `${JSON.stringify(descriptor, null, 2)}\n`,
  );
}

async function resetDirectory(path) {
  await rm(path, { recursive: true, force: true });
  await mkdir(path, { recursive: true });
}

function assertDistinctRoots(roots) {
  for (let left = 0; left < roots.length; left += 1) {
    for (let right = left + 1; right < roots.length; right += 1) {
      if (contains(roots[left], roots[right]) || contains(roots[right], roots[left])) {
        fail(`package roots must not overlap: ${roots[left]} and ${roots[right]}`);
      }
    }
  }
}

function contains(parent, child) {
  const path = relative(parent, child);
  return path === ""
    || (path !== ".." && !path.startsWith(`..${process.platform === "win32" ? "\\" : "/"}`)
      && !isAbsolute(path));
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

function same(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
}

function normalizePath(path) {
  return path.split(/[\\/]+/).join("/");
}

function fail(message) {
  throw new Error(message);
}

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];
    if (!flag?.startsWith("--") || !value) fail("invalid merge arguments.");
    options[flag.slice(2)] = value;
  }
  const required = [
    "mac-source-plugin",
    "mac-plugin",
    "windows-source-plugin",
    "windows-plugin",
    "output-source-plugin",
    "output-plugin",
  ];
  for (const name of required) {
    if (!options[name]) fail(`missing required option --${name}`);
  }
  return {
    macSourcePluginRoot: options["mac-source-plugin"],
    macPluginRoot: options["mac-plugin"],
    windowsSourcePluginRoot: options["windows-source-plugin"],
    windowsPluginRoot: options["windows-plugin"],
    outputSourcePluginRoot: options["output-source-plugin"],
    outputPluginRoot: options["output-plugin"],
  };
}

const isEntryPoint = process.argv[1]
  && import.meta.url === pathToFileURL(resolve(process.argv[1])).href;

if (isEntryPoint) {
  mergePlatformPackages(parseArgs(process.argv.slice(2)))
    .then((result) => process.stdout.write(`${JSON.stringify(result, null, 2)}\n`))
    .catch((error) => {
      process.stderr.write(`[FAIL] ${error.message}\n`);
      process.exitCode = 1;
    });
}
