import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  distributionChannelEnvironment,
  distributionChannelFileName,
  resolveDistributionChannel,
} from "../src/distribution.js";

test("resolves a strict adjacent native distribution file", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-distribution-"));
  const executable = join(root, "loomle");
  try {
    await writeFile(join(root, distributionChannelFileName), `${JSON.stringify({
      schemaVersion: 1,
      channel: "fab",
    })}\n`);
    assert.equal(resolveDistributionChannel({ executable, environment: {} }), "fab");

    await writeFile(join(root, distributionChannelFileName), `${JSON.stringify({
      schemaVersion: 1,
      channel: "fab",
      unexpected: true,
    })}\n`);
    assert.equal(resolveDistributionChannel({ executable, environment: {} }), "development");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("packaging environment takes precedence and invalid values fail closed", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-distribution-"));
  const executable = join(root, "bin", "loomle");
  try {
    await mkdir(join(root, "bin"), { recursive: true });
    await writeFile(join(root, "bin", distributionChannelFileName), `${JSON.stringify({
      schemaVersion: 1,
      channel: "fab",
    })}\n`);
    assert.equal(resolveDistributionChannel({
      executable,
      environment: { [distributionChannelEnvironment]: "github" },
    }), "github");
    assert.equal(resolveDistributionChannel({
      executable,
      environment: { [distributionChannelEnvironment]: "github-latest" },
    }), "development");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("an unpackaged Client is a development distribution", () => {
  assert.equal(resolveDistributionChannel({
    executable: "/missing/loomle",
    environment: {},
  }), "development");
});
