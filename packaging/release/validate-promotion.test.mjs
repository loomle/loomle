import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import {
  mkdir,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { basename, dirname, join } from "node:path";
import test from "node:test";

import { validatePromotion } from "./validate-promotion.mjs";

const VERSION = "0.7.0-rc.1";
const COMMIT = "a".repeat(40);
const PLATFORMS = [
  {
    target: "darwin-arm64",
    platform: "Mac",
  },
  {
    target: "win32-x64",
    platform: "Win64",
  },
];
const ENGINE_VERSIONS = ["5.7", "5.8"];
const TARGETS = ENGINE_VERSIONS.flatMap((engineVersion) =>
  PLATFORMS.map((platform) => ({
    ...platform,
    engineVersion,
    key: `ue${engineVersion}/${platform.target}`,
    archiveName: `loomle-fab-plugin-ue${engineVersion}-${platform.target}.zip`,
  })));

test("accepts exact successful Mac and Windows prerelease candidates", async () => {
  const fixture = await createFixture();
  try {
    assert.deepEqual(await validatePromotion(fixture.input), {
      artifacts: TARGETS.map(({ target, engineVersion, key }) => ({
        archiveSha256: fixture.archiveSha256ByTarget[key],
        engineVersion,
        target,
      })),
      notesPath: join(
        fixture.repoRoot,
        "packaging",
        "release",
        "notes",
        `${VERSION}.md`,
      ),
      tag: `v${VERSION}`,
      version: VERSION,
    });
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("accepts exact successful Mac and Windows final candidates", async () => {
  const fixture = await createFixture({ version: "0.7.0", channel: "final" });
  try {
    const result = await validatePromotion(fixture.input);
    assert.equal(result.version, "0.7.0");
    assert.equal(result.tag, "v0.7.0");
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("requires the product version to match the selected channel", async () => {
  const fixture = await createFixture();
  try {
    await assert.rejects(
      validatePromotion({ ...fixture.input, channel: "final" }),
      /final promotion requires an x\.y\.z product version/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("does not publish a stable product through the prerelease channel", async () => {
  const fixture = await createFixture({ version: "0.7.0", channel: "prerelease" });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /prerelease promotion requires an x\.y\.z-rc\.N product version/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects a final candidate whose descriptor remains beta", async () => {
  const fixture = await createFixture({
    version: "0.7.0",
    channel: "final",
    targetOverrides: {
      "ue5.7/darwin-arm64": { isBetaVersion: true },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /ue5\.7\/darwin-arm64 archive descriptor does not match its native platform/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects either platform when E2E names other archive bytes", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "ue5.8/win32-x64": { e2eArchiveSha256: "0".repeat(64) },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /ue5\.8\/win32-x64 packaged E2E result does not match the verified commit, engine, target, and ZIP/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects version drift between the checkout and either archive", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "ue5.7/darwin-arm64": { descriptorVersion: "0.7.0-rc.2" },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /ue5\.7\/darwin-arm64 archive VersionName "0\.7\.0-rc\.2" does not match product version "0\.7\.0-rc\.1"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects an archive whose descriptor names the other native platform", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "ue5.8/win32-x64": { descriptorPlatform: "Mac" },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /ue5\.8\/win32-x64 archive descriptor does not match its native platform/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("requires the complete advertised target set", async () => {
  const fixture = await createFixture();
  try {
    await assert.rejects(
      validatePromotion({
        ...fixture.input,
        candidates: fixture.input.candidates.filter(
          ({ target, engineVersion }) => !(target === "win32-x64" && engineVersion === "5.8"),
        ),
      }),
      /missing: ue5\.8\/win32-x64/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture(options = {}) {
  const root = await mkdtemp(join(tmpdir(), "loomle-promotion-"));
  const repoRoot = join(root, "repo");
  const version = options.version ?? VERSION;
  const channel = options.channel ?? "prerelease";

  await write(join(repoRoot, "package.json"), JSON.stringify({ version }));
  await write(
    join(repoRoot, "packaging", "release", "notes", `${version}.md`),
    "release notes\n",
  );

  const candidates = [];
  const archiveSha256ByTarget = {};
  for (const releaseTarget of TARGETS) {
    const override = options.targetOverrides?.[releaseTarget.key] ?? {};
    const targetRoot = join(root, `ue${releaseTarget.engineVersion}`, releaseTarget.target);
    const payloadRoot = join(targetRoot, "payload");
    const archivePath = join(targetRoot, releaseTarget.archiveName);
    const shaFilePath = `${archivePath}.sha256`;
    const automationResultPath = join(targetRoot, "automation-result.json");
    const e2eResultPath = join(targetRoot, "e2e-result.json");
    const descriptorPlatform = override.descriptorPlatform ?? releaseTarget.platform;

    await write(
      join(payloadRoot, "LoomleBridge", "LoomleBridge.uplugin"),
      JSON.stringify({
        Installed: true,
        IsBetaVersion: override.isBetaVersion
          ?? (channel === "prerelease" ? true : undefined),
        EngineVersion: override.descriptorEngineVersion
          ?? `${releaseTarget.engineVersion}.0`,
        VersionName: override.descriptorVersion ?? version,
        SupportedTargetPlatforms: [descriptorPlatform],
        Modules: [{
          Name: "LoomleBridge",
          PlatformAllowList: [descriptorPlatform],
        }],
      }),
    );
    const zipped = process.platform === "win32"
      ? spawnSync(
        "tar.exe",
        ["-a", "-cf", archivePath, "-C", payloadRoot, "LoomleBridge"],
        { encoding: "utf8" },
      )
      : spawnSync(
        "zip",
        ["-qry", archivePath, "LoomleBridge"],
        { cwd: payloadRoot, encoding: "utf8" },
      );
    if (zipped.error) throw zipped.error;
    assert.equal(zipped.status, 0, zipped.stderr);

    const archiveSha256 = createHash("sha256")
      .update(await readFile(archivePath))
      .digest("hex");
    archiveSha256ByTarget[releaseTarget.key] = archiveSha256;
    await write(shaFilePath, `${archiveSha256}  ${basename(archivePath)}\n`);
    await write(automationResultPath, JSON.stringify({
      status: "passed",
      commit: override.automationCommit ?? COMMIT,
      target: override.automationTarget ?? releaseTarget.target,
      engineVersion: override.automationEngineVersion ?? releaseTarget.engineVersion,
    }));
    await write(e2eResultPath, JSON.stringify({
      status: "passed",
      commit: override.e2eCommit ?? COMMIT,
      target: override.e2eTarget ?? releaseTarget.target,
      engineVersion: override.e2eEngineVersion ?? releaseTarget.engineVersion,
      archiveSha256: override.e2eArchiveSha256 ?? archiveSha256,
    }));
    candidates.push({
      target: releaseTarget.target,
      engineVersion: releaseTarget.engineVersion,
      archivePath,
      shaFilePath,
      automationResultPath,
      e2eResultPath,
    });
  }

  return {
    root,
    repoRoot,
    archiveSha256ByTarget,
    input: {
      repoRoot,
      candidates,
      headSha: COMMIT,
      channel,
    },
  };
}

async function write(path, content) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, content);
}
