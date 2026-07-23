import { createHash } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import {
  chmod,
  copyFile,
  mkdir,
  readFile,
  rename,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { createRequire } from "node:module";
import { join, resolve } from "node:path";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { inject } = require("postject");

const repoRoot = fileURLToPath(new URL("../../", import.meta.url));
const manifestPath = fileURLToPath(new URL("node-runtime.json", import.meta.url));
const bundleEntry = "client/dist/main.cjs";
const target = parseTarget(process.argv.slice(2));
const hostTarget = `${process.platform}-${process.arch}`;

if (target !== hostTarget) {
  throw new Error(
    `Executable builds are native: requested ${target}, running on ${hostTarget}. Use a ${target} runner.`,
  );
}

const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
const product = JSON.parse(await readFile(join(repoRoot, "package.json"), "utf8"));
const runtime = manifest.targets?.[target];
if (!runtime) {
  throw new Error(`Unsupported executable target: ${target}.`);
}
validateRuntime(manifest.nodeVersion, runtime);
if (typeof product.version !== "string" || product.version.length === 0) {
  throw new Error("package.json must contain a product version.");
}
const protocolVersion = product.loomle?.protocolVersion;
if (!Number.isInteger(protocolVersion) || protocolVersion < 1 || protocolVersion > 2_147_483_647) {
  throw new Error("package.json must contain a positive int32 loomle.protocolVersion.");
}

console.log(`Building Loomle executable for ${target} with Node.js ${manifest.nodeVersion}.`);
run("npm", ["run", "build"], { cwd: repoRoot });

const cacheDirectory = resolve(repoRoot, ".tmp/client-cache");
const archivePath = join(cacheDirectory, runtime.url.slice(runtime.url.lastIndexOf("/") + 1));
await mkdir(cacheDirectory, { recursive: true });
await ensureArchive(runtime.url, archivePath, runtime.sha256);

const runtimeDirectory = join(cacheDirectory, `runtime-${target}`);
await rm(runtimeDirectory, { recursive: true, force: true });
await mkdir(runtimeDirectory, { recursive: true });
run("tar", ["-xzf", archivePath, "-C", runtimeDirectory]);

const nodePath = join(runtimeDirectory, runtime.archiveRoot, runtime.nodePath);
const nodeVersion = run(nodePath, ["--version"], { capture: true });
if (nodeVersion !== `v${manifest.nodeVersion}`) {
  throw new Error(`Extracted runtime reports ${nodeVersion}; expected v${manifest.nodeVersion}.`);
}

const buildDirectory = resolve(repoRoot, `.tmp/client-build/${target}`);
const outputDirectory = resolve(repoRoot, `.tmp/client/${target}`);
const outputPath = join(outputDirectory, "loomle");
const receiptPath = join(outputDirectory, "build.json");
const nodeLicensePath = join(outputDirectory, "node-license.txt");
const temporaryOutputPath = join(buildDirectory, "loomle");
const blobPath = join(buildDirectory, "loomle.blob");
const configPath = join(buildDirectory, "sea-config.json");
await rm(buildDirectory, { recursive: true, force: true });
await rm(outputDirectory, { recursive: true, force: true });
await mkdir(buildDirectory, { recursive: true });
await mkdir(outputDirectory, { recursive: true });

await writeFile(configPath, `${JSON.stringify({
  main: bundleEntry,
  output: blobPath,
  disableExperimentalSEAWarning: true,
  useSnapshot: false,
  useCodeCache: false,
  execArgvExtension: "none",
}, null, 2)}\n`);

run(nodePath, ["--experimental-sea-config", configPath], {
  cwd: repoRoot,
  env: { ...process.env, NODE_OPTIONS: "" },
});

await copyFile(nodePath, temporaryOutputPath);
await chmod(temporaryOutputPath, 0o755);
run("codesign", ["--remove-signature", temporaryOutputPath]);

await inject(temporaryOutputPath, "NODE_SEA_BLOB", await readFile(blobPath), {
  machoSegmentName: "NODE_SEA",
  sentinelFuse: "NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2",
});

await chmod(temporaryOutputPath, 0o755);
run("codesign", ["--force", "--sign", "-", "--timestamp=none", temporaryOutputPath]);
run("codesign", ["--verify", "--strict", "--verbose=2", temporaryOutputPath]);
await assertFileExcludes(temporaryOutputPath, repoRoot);
await rename(temporaryOutputPath, outputPath);
run("codesign", ["--verify", "--strict", "--verbose=2", outputPath]);
await copyFile(
  join(runtimeDirectory, runtime.archiveRoot, "LICENSE"),
  nodeLicensePath,
);

const outputStat = await stat(outputPath);
const outputHash = await sha256(outputPath);
const nodeLicenseHash = await sha256(nodeLicensePath);
await writeFile(receiptPath, `${JSON.stringify({
  schemaVersion: 3,
  productVersion: product.version,
  protocolVersion,
  target,
  nodeVersion: manifest.nodeVersion,
  runtimeSha256: runtime.sha256,
  executable: "loomle",
  sha256: outputHash,
  nodeLicenseSha256: nodeLicenseHash,
}, null, 2)}\n`);
console.log(`Built ${outputPath}`);
console.log(`Size: ${outputStat.size} bytes`);
console.log(`SHA-256: ${outputHash}`);
console.log(`Node license SHA-256: ${nodeLicenseHash}`);
console.log(`Receipt: ${receiptPath}`);

function parseTarget(args) {
  if (args.length === 0) return `${process.platform}-${process.arch}`;
  if (args.length === 2 && args[0] === "--target" && args[1]) return args[1];
  throw new Error("Usage: node packaging/client/build.mjs [--target <platform-arch>]");
}

function validateRuntime(nodeVersionValue, runtimeValue) {
  if (typeof nodeVersionValue !== "string" || !/^\d+\.\d+\.\d+$/.test(nodeVersionValue)) {
    throw new Error("node-runtime.json must contain an exact nodeVersion.");
  }
  for (const field of ["url", "sha256", "archiveRoot", "nodePath"]) {
    if (typeof runtimeValue[field] !== "string" || runtimeValue[field].length === 0) {
      throw new Error(`node-runtime.json target is missing ${field}.`);
    }
  }
  if (!runtimeValue.url.startsWith(`https://nodejs.org/download/release/v${nodeVersionValue}/`)) {
    throw new Error("Node runtime URL must use the pinned release on nodejs.org.");
  }
  if (!/^[0-9a-f]{64}$/.test(runtimeValue.sha256)) {
    throw new Error("Node runtime SHA-256 must be a lowercase 64-character digest.");
  }
}

async function ensureArchive(url, path, expectedHash) {
  try {
    if (await sha256(path) === expectedHash) {
      console.log(`Using verified Node.js archive: ${path}`);
      return;
    }
    console.log(`Discarding Node.js archive with an unexpected checksum: ${path}`);
    await rm(path, { force: true });
  } catch (error) {
    if (error?.code !== "ENOENT") throw error;
  }

  const partialPath = `${path}.partial`;
  await rm(partialPath, { force: true });
  console.log(`Downloading ${url}`);
  const response = await fetch(url, { redirect: "follow" });
  if (!response.ok || !response.body) {
    throw new Error(`Node.js archive download failed: HTTP ${response.status}.`);
  }
  try {
    await pipeline(Readable.fromWeb(response.body), createWriteStream(partialPath, { flags: "wx" }));
    const actualHash = await sha256(partialPath);
    if (actualHash !== expectedHash) {
      throw new Error(`Node.js archive checksum mismatch: expected ${expectedHash}, got ${actualHash}.`);
    }
    await rename(partialPath, path);
  } catch (error) {
    await rm(partialPath, { force: true });
    throw error;
  }
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

async function assertFileExcludes(path, text) {
  const needle = Buffer.from(text);
  let carry = Buffer.alloc(0);
  for await (const chunk of createReadStream(path)) {
    const data = carry.length === 0 ? chunk : Buffer.concat([carry, chunk]);
    if (data.indexOf(needle) !== -1) {
      throw new Error(`Executable contains its build checkout path: ${text}.`);
    }
    carry = data.subarray(Math.max(0, data.length - needle.length + 1));
  }
}

function run(command, args, options = {}) {
  const capture = options.capture === true;
  const result = spawnSync(command, args, {
    cwd: options.cwd,
    env: options.env,
    encoding: "utf8",
    stdio: capture ? "pipe" : "inherit",
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    if (capture) {
      if (result.stdout) process.stdout.write(result.stdout);
      if (result.stderr) process.stderr.write(result.stderr);
    }
    throw new Error(`${command} exited with status ${result.status}.`);
  }
  return capture ? result.stdout.trim() : "";
}
