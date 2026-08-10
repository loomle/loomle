import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  renderDistributionFile,
  requireDistributionFile,
} from "./distribution.mjs";

test("renders and validates strict distribution metadata", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-package-distribution-"));
  const path = join(root, "distribution.json");
  try {
    await writeFile(path, renderDistributionFile("fab"));
    assert.deepEqual(await requireDistributionFile(path, "fab"), {
      schemaVersion: 1,
      channel: "fab",
    });
    await assert.rejects(requireDistributionFile(path, "github"), /strict github/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("rejects unknown channels and extra fields", async () => {
  assert.throws(() => renderDistributionFile("store"), /unsupported distribution channel/);
  const root = await mkdtemp(join(tmpdir(), "loomle-package-distribution-"));
  const path = join(root, "distribution.json");
  try {
    await writeFile(path, JSON.stringify({ schemaVersion: 1, channel: "fab", extra: true }));
    await assert.rejects(requireDistributionFile(path, "fab"), /strict fab/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
