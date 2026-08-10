import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  validateChannelDirectory,
  validateChannelDocument,
} from "./channel-document.mjs";

const version = "0.7.10";
const bridge = {
  source: "github_release",
  tag: `v${version}`,
  assets: {
    "ue5.7": {
      url: `https://github.com/loomle/loomle/releases/download/v${version}/loomle-bridge-${version}-ue5.7.zip`,
      sha256: "a".repeat(64),
    },
    "ue5.8": {
      url: `https://github.com/loomle/loomle/releases/download/v${version}/loomle-bridge-${version}-ue5.8.zip`,
      sha256: "b".repeat(64),
    },
  },
};

test("accepts strict Fab and Claude channel documents", () => {
  assert.equal(validateChannelDocument({
    schemaVersion: 1,
    channel: "fab",
    version,
    publishedAt: "2026-08-10T00:00:00Z",
    listingUrl: "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428",
  }, "fab").version, version);
  assert.equal(validateChannelDocument({
    schemaVersion: 1,
    channel: "claude",
    version,
    publishedAt: "2026-08-10T00:00:00.000Z",
    listingUrl: "https://claude.ai/directory/connectors/loomle",
    bridge,
  }, "claude").channel, "claude");
});

test("rejects cross-channel, unversioned, extra, and malformed values", () => {
  const base = {
    schemaVersion: 1,
    channel: "claude",
    version,
    publishedAt: "2026-08-10T00:00:00Z",
    listingUrl: "https://claude.ai/directory/connectors/loomle",
    bridge,
  };
  assert.throws(() => validateChannelDocument({ ...base, channel: "codex" }, "claude"), /channel/);
  assert.throws(() => validateChannelDocument({ ...base, extra: true }, "claude"), /unexpected fields/);
  assert.throws(() => validateChannelDocument({ ...base, version: "0.7.10-rc.1" }, "claude"), /stable semantic/);
  assert.throws(() => validateChannelDocument({
    ...base,
    bridge: {
      ...bridge,
      assets: {
        ...bridge.assets,
        "ue5.7": { ...bridge.assets["ue5.7"], url: "https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.7.zip" },
      },
    },
  }, "claude"), /exact versioned GitHub Release URL/);
});

test("validates only recognized JSON files in the public channel directory", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-channels-"));
  try {
    await mkdir(root, { recursive: true });
    await writeFile(join(root, "fab.json"), `${JSON.stringify({
      schemaVersion: 1,
      channel: "fab",
      version,
      publishedAt: "2026-08-10T00:00:00Z",
      listingUrl: "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428",
    })}\n`);
    assert.deepEqual(await validateChannelDirectory(root), ["fab.json"]);
    await writeFile(join(root, "unknown.json"), "{}\n");
    await assert.rejects(validateChannelDirectory(root), /unexpected channel document/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
