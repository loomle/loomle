#!/usr/bin/env node

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

export async function validatePromotion({
  repoRoot,
  archivePath,
  shaFilePath,
  automationResultPath,
  e2eResultPath,
  headSha,
  channel,
}) {
  if (!/^[0-9a-f]{40}$/.test(headSha)) {
    fail("verified commit must be a lowercase 40-character Git SHA.");
  }
  if (!["prerelease", "final"].includes(channel)) {
    fail(`unsupported release channel: ${channel}.`);
  }
  if (channel === "final") {
    fail(
      "stable promotion is disabled until Developer ID signing and notarization"
      + " are verified before packaged E2E.",
    );
  }

  const product = await readJson(join(repoRoot, "package.json"));
  const version = product.version;
  if (typeof version !== "string"
      || !/^[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+$/.test(version)) {
    fail(`prerelease promotion requires an x.y.z-rc.N product version; found ${version}.`);
  }

  await requireNonEmptyFile(archivePath, "verified archive");
  const archiveSha256 = await sha256(archivePath);
  const sidecar = parseSha256Sidecar(
    await readFile(shaFilePath, "utf8"),
    basename(archivePath),
  );
  if (sidecar !== archiveSha256) {
    fail(`archive SHA-256 ${archiveSha256} does not match sidecar ${sidecar}.`);
  }

  const descriptor = JSON.parse(readZipEntry(archivePath, "LoomleBridge/LoomleBridge.uplugin"));
  if (descriptor.VersionName !== version) {
    fail(
      `archive VersionName ${JSON.stringify(descriptor.VersionName)}`
      + ` does not match product version ${JSON.stringify(version)}.`,
    );
  }

  const automation = await readJson(automationResultPath);
  if (automation.status !== "passed" || automation.commit !== headSha) {
    fail("UE Automation result does not match the verified commit.");
  }
  const e2e = await readJson(e2eResultPath);
  if (e2e.status !== "passed"
      || e2e.commit !== headSha
      || e2e.archiveSha256 !== archiveSha256) {
    fail("packaged E2E result does not match the verified commit and ZIP.");
  }

  const notesPath = join(repoRoot, "packaging", "release", "notes", `${version}.md`);
  await requireNonEmptyFile(notesPath, "release notes");
  return {
    archiveSha256,
    notesPath,
    tag: `v${version}`,
    version,
  };
}

function parseSha256Sidecar(text, expectedName) {
  const match = text.trim().match(/^([0-9a-f]{64})[ \t]+[*]?(.+)$/);
  if (!match || match[2] !== expectedName) {
    fail(`SHA-256 sidecar must name exactly ${expectedName}.`);
  }
  return match[1];
}

function readZipEntry(archivePath, entry) {
  const result = spawnSync(
    process.platform === "win32" ? "tar.exe" : "unzip",
    process.platform === "win32"
      ? ["-xOf", archivePath, entry]
      : ["-p", archivePath, entry],
    {
      encoding: "utf8",
      maxBuffer: 1024 * 1024,
    },
  );
  if (result.error) throw result.error;
  if (result.status !== 0 || result.stdout.length === 0) {
    fail(`verified archive is missing ${entry}.`);
  }
  return result.stdout;
}

async function readJson(path) {
  try {
    return JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    fail(`cannot read JSON ${path}: ${error.message}`);
  }
}

async function requireNonEmptyFile(path, label) {
  let fileStat;
  try {
    fileStat = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") fail(`${label} not found: ${path}`);
    throw error;
  }
  if (!fileStat.isFile() || fileStat.size === 0) {
    fail(`${label} must be a non-empty file: ${path}`);
  }
}

async function sha256(path) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path)) hash.update(chunk);
  return hash.digest("hex");
}

function fail(message) {
  throw new Error(message);
}

function parseArguments(args) {
  const values = {};
  for (let index = 0; index < args.length; index += 2) {
    const flag = args[index];
    const value = args[index + 1];
    if (!flag?.startsWith("--") || !value) usage();
    values[flag.slice(2)] = value;
  }
  const required = [
    "repo-root",
    "archive",
    "sha-file",
    "automation-result",
    "e2e-result",
    "head-sha",
    "channel",
  ];
  if (required.some((name) => !values[name])) usage();
  return {
    repoRoot: resolve(values["repo-root"]),
    archivePath: resolve(values.archive),
    shaFilePath: resolve(values["sha-file"]),
    automationResultPath: resolve(values["automation-result"]),
    e2eResultPath: resolve(values["e2e-result"]),
    headSha: values["head-sha"],
    channel: values.channel,
  };
}

function usage() {
  throw new Error(
    "Usage: node packaging/release/validate-promotion.mjs"
    + " --repo-root <path> --archive <zip> --sha-file <path>"
    + " --automation-result <json> --e2e-result <json>"
    + " --head-sha <sha> --channel <prerelease|final>",
  );
}

if (process.argv[1]
    && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  validatePromotion(parseArguments(process.argv.slice(2)))
    .then((result) => process.stdout.write(`${JSON.stringify(result, null, 2)}\n`))
    .catch((error) => {
      process.stderr.write(`[FAIL] ${error.message}\n`);
      process.exitCode = 1;
    });
}
