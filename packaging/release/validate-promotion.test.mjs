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
const TARGETS = [
  {
    target: "darwin-arm64",
    platform: "Mac",
    archiveName: "loomle-fab-plugin-darwin-arm64.zip",
  },
  {
    target: "win32-x64",
    platform: "Win64",
    archiveName: "loomle-fab-plugin-win32-x64.zip",
  },
];

test("accepts exact successful Mac and Windows prerelease candidates", async () => {
  const fixture = await createFixture();
  try {
    assert.deepEqual(await validatePromotion(fixture.input), {
      artifacts: TARGETS.map(({ target }) => ({
        archiveSha256: fixture.archiveSha256ByTarget[target],
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

test("blocks stable promotion until platform signing and trust gates exist", async () => {
  const fixture = await createFixture();
  try {
    await assert.rejects(
      validatePromotion({ ...fixture.input, channel: "final" }),
      /stable promotion is disabled/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects either platform when E2E names other archive bytes", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "win32-x64": { e2eArchiveSha256: "0".repeat(64) },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /win32-x64 packaged E2E result does not match the verified commit, target, and ZIP/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects version drift between the checkout and either archive", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "darwin-arm64": { descriptorVersion: "0.7.0-rc.2" },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /darwin-arm64 archive VersionName "0\.7\.0-rc\.2" does not match product version "0\.7\.0-rc\.1"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects an archive whose descriptor names the other native platform", async () => {
  const fixture = await createFixture({
    targetOverrides: {
      "win32-x64": { descriptorPlatform: "Mac" },
    },
  });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /win32-x64 archive descriptor does not match its native platform/,
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
          ({ target }) => target !== "win32-x64",
        ),
      }),
      /missing: win32-x64/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture(options = {}) {
  const root = await mkdtemp(join(tmpdir(), "loomle-promotion-"));
  const repoRoot = join(root, "repo");

  await write(join(repoRoot, "package.json"), JSON.stringify({ version: VERSION }));
  await write(
    join(repoRoot, "packaging", "release", "notes", `${VERSION}.md`),
    "release notes\n",
  );

  const candidates = [];
  const archiveSha256ByTarget = {};
  for (const releaseTarget of TARGETS) {
    const override = options.targetOverrides?.[releaseTarget.target] ?? {};
    const targetRoot = join(root, releaseTarget.target);
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
        VersionName: override.descriptorVersion ?? VERSION,
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
    archiveSha256ByTarget[releaseTarget.target] = archiveSha256;
    await write(shaFilePath, `${archiveSha256}  ${basename(archivePath)}\n`);
    await write(automationResultPath, JSON.stringify({
      status: "passed",
      commit: override.automationCommit ?? COMMIT,
      target: override.automationTarget ?? releaseTarget.target,
    }));
    await write(e2eResultPath, JSON.stringify({
      status: "passed",
      commit: override.e2eCommit ?? COMMIT,
      target: override.e2eTarget ?? releaseTarget.target,
      archiveSha256: override.e2eArchiveSha256 ?? archiveSha256,
    }));
    candidates.push({
      target: releaseTarget.target,
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
      channel: "prerelease",
    },
  };
}

async function write(path, content) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, content);
}
