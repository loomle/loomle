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

test("accepts an exact successful prerelease candidate", async () => {
  const fixture = await createFixture();
  try {
    assert.deepEqual(await validatePromotion(fixture.input), {
      archiveSha256: fixture.archiveSha256,
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

test("blocks stable promotion until the signed and notarized gate exists", async () => {
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

test("rejects a candidate whose E2E result names other archive bytes", async () => {
  const fixture = await createFixture({ e2eArchiveSha256: "0".repeat(64) });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /packaged E2E result does not match the verified commit and ZIP/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

test("rejects version drift between the checkout and archive", async () => {
  const fixture = await createFixture({ descriptorVersion: "0.7.0-rc.2" });
  try {
    await assert.rejects(
      validatePromotion(fixture.input),
      /archive VersionName "0\.7\.0-rc\.2" does not match product version "0\.7\.0-rc\.1"/,
    );
  } finally {
    await rm(fixture.root, { recursive: true, force: true });
  }
});

async function createFixture(options = {}) {
  const root = await mkdtemp(join(tmpdir(), "loomle-promotion-"));
  const repoRoot = join(root, "repo");
  const payloadRoot = join(root, "payload");
  const archivePath = join(root, "loomle-fab-plugin-darwin-arm64.zip");
  const shaFilePath = `${archivePath}.sha256`;
  const automationResultPath = join(root, "automation-result.json");
  const e2eResultPath = join(root, "e2e-result.json");

  await write(join(repoRoot, "package.json"), JSON.stringify({ version: VERSION }));
  await write(
    join(repoRoot, "packaging", "release", "notes", `${VERSION}.md`),
    "release notes\n",
  );
  await write(
    join(payloadRoot, "LoomleBridge", "LoomleBridge.uplugin"),
    JSON.stringify({ VersionName: options.descriptorVersion ?? VERSION }),
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
  await write(shaFilePath, `${archiveSha256}  ${basename(archivePath)}\n`);
  await write(automationResultPath, JSON.stringify({
    status: "passed",
    commit: COMMIT,
  }));
  await write(e2eResultPath, JSON.stringify({
    status: "passed",
    commit: COMMIT,
    archiveSha256: options.e2eArchiveSha256 ?? archiveSha256,
  }));

  return {
    root,
    repoRoot,
    archiveSha256,
    input: {
      repoRoot,
      archivePath,
      shaFilePath,
      automationResultPath,
      e2eResultPath,
      headSha: COMMIT,
      channel: "prerelease",
    },
  };
}

async function write(path, content) {
  await mkdir(dirname(path), { recursive: true });
  await writeFile(path, content);
}
