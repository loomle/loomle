#!/usr/bin/env node

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  descriptorEngineVersion,
  requireSupportedUnrealVersion,
  SUPPORTED_UNREAL_VERSIONS,
  unrealVersionSlug,
} from "../tools/unreal-versions.mjs";

const RELEASE_TARGETS = [
  {
    target: "darwin-arm64",
    platform: "Mac",
    cliPrefix: "darwin-arm64",
  },
  {
    target: "win32-x64",
    platform: "Win64",
    cliPrefix: "win32-x64",
  },
];

const RELEASE_CANDIDATES = SUPPORTED_UNREAL_VERSIONS.flatMap((engineVersion) =>
  RELEASE_TARGETS.map((releaseTarget) => ({
    ...releaseTarget,
    engineVersion,
    cliPrefix: `${unrealVersionSlug(engineVersion)}-${releaseTarget.cliPrefix}`,
  })));

export async function validatePromotion({
  repoRoot,
  candidates,
  headSha,
  channel,
}) {
  if (!/^[0-9a-f]{40}$/.test(headSha)) {
    fail("verified commit must be a lowercase 40-character Git SHA.");
  }
  if (!["prerelease", "final"].includes(channel)) {
    fail(`unsupported release channel: ${channel}.`);
  }
  const product = await readJson(join(repoRoot, "package.json"));
  const version = product.version;
  const versionPattern = channel === "prerelease"
    ? /^[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+$/
    : /^[0-9]+\.[0-9]+\.[0-9]+$/;
  if (typeof version !== "string" || !versionPattern.test(version)) {
    const expected = channel === "prerelease" ? "x.y.z-rc.N" : "x.y.z";
    fail(`${channel} promotion requires an ${expected} product version; found ${version}.`);
  }

  if (!Array.isArray(candidates)) {
    fail("release candidates must be an array.");
  }
  const candidatesByKey = new Map();
  for (const candidate of candidates) {
    if (!candidate || typeof candidate.target !== "string"
        || typeof candidate.engineVersion !== "string") {
      fail("each release candidate must name an engine version and target.");
    }
    requireSupportedUnrealVersion(candidate.engineVersion);
    const key = candidateKey(candidate.engineVersion, candidate.target);
    if (candidatesByKey.has(key)) {
      fail(`duplicate release candidate: ${key}.`);
    }
    candidatesByKey.set(key, candidate);
  }
  const expectedCandidates = RELEASE_CANDIDATES.map(({ engineVersion, target }) =>
    candidateKey(engineVersion, target));
  const actualCandidates = [...candidatesByKey.keys()];
  const missingCandidates = expectedCandidates.filter((key) => !candidatesByKey.has(key));
  const unexpectedCandidates = actualCandidates.filter((key) => !expectedCandidates.includes(key));
  if (missingCandidates.length > 0 || unexpectedCandidates.length > 0) {
    fail(
      "release candidates must contain exactly"
      + ` ${expectedCandidates.join(", ")};`
      + ` missing: ${missingCandidates.join(", ") || "none"};`
      + ` unexpected: ${unexpectedCandidates.join(", ") || "none"}.`,
    );
  }

  const artifacts = [];
  for (const releaseTarget of RELEASE_CANDIDATES) {
    const candidate = candidatesByKey.get(
      candidateKey(releaseTarget.engineVersion, releaseTarget.target),
    );
    const {
      archivePath,
      shaFilePath,
      automationResultPath,
      e2eResultPath,
    } = candidate;
    const label = candidateKey(releaseTarget.engineVersion, releaseTarget.target);

    await requireNonEmptyFile(archivePath, `${label} verified archive`);
    await requireNonEmptyFile(shaFilePath, `${label} SHA-256 sidecar`);
    const archiveSha256 = await sha256(archivePath);
    const sidecar = parseSha256Sidecar(
      await readFile(shaFilePath, "utf8"),
      basename(archivePath),
    );
    if (sidecar !== archiveSha256) {
      fail(
        `${label} archive SHA-256 ${archiveSha256}`
        + ` does not match sidecar ${sidecar}.`,
      );
    }

    const descriptor = JSON.parse(
      readZipEntry(archivePath, "LoomleBridge/LoomleBridge.uplugin"),
    );
    const moduleNames = descriptor.Modules?.map((module) => module.Name);
    const module = descriptor.Modules?.[0];
    if (descriptor.VersionName !== version) {
      fail(
        `${label} archive VersionName ${JSON.stringify(descriptor.VersionName)}`
        + ` does not match product version ${JSON.stringify(version)}.`,
      );
    }
    if (descriptor.EngineVersion !== descriptorEngineVersion(releaseTarget.engineVersion)
        || descriptor.Installed !== true
        || (descriptor.IsBetaVersion ?? false) !== (channel === "prerelease")
        || !same(descriptor.SupportedTargetPlatforms, [releaseTarget.platform])
        || !same(moduleNames, ["LoomleBridge"])
        || !same(module?.PlatformAllowList, [releaseTarget.platform])
        || module?.PlatformArchitectureAllowList !== undefined) {
      fail(`${label} archive descriptor does not match its native platform.`);
    }

    const automation = await readJson(automationResultPath);
    if (automation.status !== "passed"
        || automation.commit !== headSha
        || automation.target !== releaseTarget.target
        || automation.engineVersion !== releaseTarget.engineVersion) {
      fail(`${label} UE Automation result does not match the verified commit, engine, and target.`);
    }
    const e2e = await readJson(e2eResultPath);
    if (e2e.status !== "passed"
        || e2e.commit !== headSha
        || e2e.target !== releaseTarget.target
        || e2e.engineVersion !== releaseTarget.engineVersion
        || e2e.archiveSha256 !== archiveSha256) {
      fail(
        `${label} packaged E2E result does not match`
        + " the verified commit, engine, target, and ZIP.",
      );
    }

    artifacts.push({
      archiveSha256,
      engineVersion: releaseTarget.engineVersion,
      target: releaseTarget.target,
    });
  }

  const notesPath = join(repoRoot, "packaging", "release", "notes", `${version}.md`);
  await requireNonEmptyFile(notesPath, "release notes");
  return {
    artifacts,
    notesPath,
    tag: `v${version}`,
    version,
  };
}

function candidateKey(engineVersion, target) {
  return `${unrealVersionSlug(engineVersion)}/${target}`;
}

function parseSha256Sidecar(text, expectedName) {
  const match = text.trim().match(/^([0-9a-f]{64})[ \t]+[*]?(.+)$/);
  if (!match || match[2] !== expectedName) {
    fail(`SHA-256 sidecar must name exactly ${expectedName}.`);
  }
  return match[1];
}

function same(actual, expected) {
  return JSON.stringify(actual) === JSON.stringify(expected);
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
    "head-sha",
    "channel",
    ...RELEASE_CANDIDATES.flatMap(({ cliPrefix }) => [
      `${cliPrefix}-archive`,
      `${cliPrefix}-sha-file`,
      `${cliPrefix}-automation-result`,
      `${cliPrefix}-e2e-result`,
    ]),
  ];
  if (required.some((name) => !values[name])) usage();
  return {
    repoRoot: resolve(values["repo-root"]),
    candidates: RELEASE_CANDIDATES.map(({ target, engineVersion, cliPrefix }) => ({
      target,
      engineVersion,
      archivePath: resolve(values[`${cliPrefix}-archive`]),
      shaFilePath: resolve(values[`${cliPrefix}-sha-file`]),
      automationResultPath: resolve(values[`${cliPrefix}-automation-result`]),
      e2eResultPath: resolve(values[`${cliPrefix}-e2e-result`]),
    })),
    headSha: values["head-sha"],
    channel: values.channel,
  };
}

function usage() {
  throw new Error(
    "Usage: node packaging/release/validate-promotion.mjs"
    + " --repo-root <path>"
    + " --ue5.7-darwin-arm64-archive <zip> ..."
    + " --ue5.7-win32-x64-archive <zip> ..."
    + " --ue5.8-darwin-arm64-archive <zip> ..."
    + " --ue5.8-win32-x64-archive <zip> ..."
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
