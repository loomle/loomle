import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { productVersion } from "../src/generated/product-version.js";
import {
  ClientStatusService,
  ReleaseManifestChecker,
  platformTarget,
} from "../src/status.js";

const manifest = {
  schemaVersion: 1,
  channels: {
    stable: {
      version: "0.7.1",
      releaseUrl: "https://example.test/v0.7.1",
      targets: {
        "darwin-arm64": {
          assetUrl: "https://example.test/stable-mac.zip",
          sha256: "a".repeat(64),
        },
      },
    },
    prerelease: {
      version: "0.7.0-rc.2",
      releaseUrl: "https://example.test/v0.7.0-rc.2",
      targets: {
        "darwin-arm64": {
          assetUrl: "https://example.test/rc-mac.zip",
          sha256: "b".repeat(64),
        },
        "win32-x64": {
          assetUrl: "https://example.test/rc-win.zip",
          sha256: "c".repeat(64),
        },
      },
    },
  },
};

function checker(value: unknown, options: { ok?: boolean; status?: number } = {}) {
  return new ReleaseManifestChecker({
    fetchManifest: async () => ({
      ok: options.ok ?? true,
      status: options.status ?? 200,
      async json() {
        return value;
      },
    }),
  });
}

test("selects prerelease and stable channels and reports current versions", async () => {
  const releases = checker(manifest);
  assert.deepEqual(await releases.check("0.7.0-rc.1", "darwin-arm64"), {
    status: "available",
    version: "0.7.0-rc.2",
    releaseUrl: "https://example.test/v0.7.0-rc.2",
    assetUrl: "https://example.test/rc-mac.zip",
    sha256: "b".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.0", "darwin-arm64"), {
    status: "available",
    version: "0.7.1",
    releaseUrl: "https://example.test/v0.7.1",
    assetUrl: "https://example.test/stable-mac.zip",
    sha256: "a".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.0-rc.2", "darwin-arm64"), {
    status: "current",
  });
});

test("keeps malformed, offline, and unsupported update discovery informational", async () => {
  assert.deepEqual(await checker({ bad: true }).check("0.7.0-rc.1", "darwin-arm64"), {
    status: "unknown",
    reason: "invalid_release_manifest",
  });
  assert.deepEqual(await checker({}, { ok: false, status: 503 })
    .check("0.7.0-rc.1", "darwin-arm64"), {
    status: "unknown",
    reason: "manifest_http_503",
  });
  assert.deepEqual(await checker(manifest).check("0.7.0-rc.1", "linux-x64"), {
    status: "unknown",
    reason: "unsupported_target",
  });
  const offline = new ReleaseManifestChecker({
    fetchManifest: async () => {
      throw new Error("network_offline");
    },
  });
  assert.deepEqual(await offline.check("0.7.0-rc.1", "darwin-arm64"), {
    status: "unknown",
    reason: "network_offline",
  });
});

test("bounds manifest latency and reuses a valid in-process cache", async () => {
  const timeout = new ReleaseManifestChecker({
    timeoutMs: 1,
    fetchManifest: async (_url, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener("abort", () => {
        const error = new Error("aborted");
        error.name = "AbortError";
        reject(error);
      }, { once: true });
    }),
  });
  assert.deepEqual(await timeout.check("0.7.0-rc.1", "darwin-arm64"), {
    status: "unknown",
    reason: "manifest_timeout",
  });

  let fetches = 0;
  const cached = new ReleaseManifestChecker({
    fetchManifest: async () => {
      fetches += 1;
      return { ok: true, status: 200, async json() { return manifest; } };
    },
  });
  await cached.check("0.7.0-rc.1", "darwin-arm64");
  await cached.check("0.7.0-rc.1", "win32-x64");
  assert.equal(fetches, 1);
});

test("Client status remains usable when update and session discovery fail", async () => {
  const service = new ClientStatusService({
    async sessionStatus() {
      throw new Error("session unavailable");
    },
  }, {
    version: "0.7.0-rc.1",
    pid: 42,
    platform: "darwin",
    arch: "arm64",
    executable: "/plugin/loomle",
    updateChecker: {
      async check() {
        throw new Error("network unavailable");
      },
    },
  });

  assert.deepEqual(await service.report(), {
    client: {
      version: "0.7.0-rc.1",
      pid: 42,
      platform: "darwin",
      target: "darwin-arm64",
      executable: "/plugin/loomle",
    },
    update: { status: "unknown", reason: "network_unavailable" },
    session: { status: "unknown", reason: "session_unavailable" },
  });
});

test("maps only packaged release targets", () => {
  assert.equal(platformTarget("darwin", "arm64"), "darwin-arm64");
  assert.equal(platformTarget("win32", "x64"), "win32-x64");
  assert.equal(platformTarget("darwin", "x64"), undefined);
  assert.equal(platformTarget("linux", "x64"), undefined);
});

test("the published release manifest matches the current packaged prerelease", async () => {
  const value = JSON.parse(await readFile(
    new URL("../../../site/releases.json", import.meta.url),
    "utf8",
  )) as unknown;
  const releases = checker(value);
  assert.deepEqual(await releases.check(productVersion, "darwin-arm64"), {
    status: "current",
  });
  assert.deepEqual(await releases.check(productVersion, "win32-x64"), {
    status: "current",
  });
});
