import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  ClientStatusService,
  GitHubReleaseChecker,
  installedEngineVersion,
  latestReleaseApiUrl,
  platformTarget,
} from "../src/status.js";

const githubRelease = {
  tag_name: "v0.7.1",
  html_url: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
  draft: false,
  prerelease: false,
  assets: [{
    name: "loomle-bridge-0.7.1-ue5.7.zip",
    state: "uploaded",
    browser_download_url: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.7.zip",
    digest: `sha256:${"a".repeat(64)}`,
    size: 1024,
  }, {
    name: "loomle-bridge-0.7.1-ue5.8.zip",
    state: "uploaded",
    browser_download_url: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.8.zip",
    digest: `sha256:${"b".repeat(64)}`,
    size: 1024,
  }],
};

function checker(value: unknown, options: { ok?: boolean; status?: number } = {}) {
  return new GitHubReleaseChecker({
    fetchRelease: async () => ({
      ok: options.ok ?? true,
      status: options.status ?? 200,
      async json() {
        return value;
      },
    }),
  });
}

test("offers the latest stable GitHub Release to prerelease and stable Clients", async () => {
  const releases = checker(githubRelease);
  assert.deepEqual(await releases.check("0.7.1-rc.1", "darwin-arm64", "5.7"), {
    status: "available",
    version: "0.7.1",
    releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
    assetUrl: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.7.zip",
    sha256: "a".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.0", "darwin-arm64", "5.8"), {
    status: "available",
    version: "0.7.1",
    releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
    assetUrl: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.8.zip",
    sha256: "b".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.1", "win32-x64", "5.7"), {
    status: "current",
  });
  assert.deepEqual(await releases.check("0.8.0-rc.1", "darwin-arm64", "5.7"), {
    status: "current",
  });
});

test("keeps malformed, offline, and unsupported update discovery informational", async () => {
  assert.deepEqual(await checker({ bad: true }).check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    reason: "invalid_github_release",
  });
  assert.deepEqual(await checker({}, { ok: false, status: 503 })
    .check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    reason: "github_release_http_503",
  });
  assert.deepEqual(await checker(githubRelease).check("0.7.0-rc.1", "linux-x64", "5.7"), {
    status: "unknown",
    reason: "unsupported_target",
  });
  assert.deepEqual(await checker(githubRelease).check("0.7.0-rc.1", "darwin-arm64", "5.6"), {
    status: "unknown",
    reason: "unsupported_engine_version",
  });
  const offline = new GitHubReleaseChecker({
    fetchRelease: async () => {
      throw new Error("network_offline");
    },
  });
  assert.deepEqual(await offline.check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    reason: "network_offline",
  });
});

test("bounds GitHub latency and reuses a valid in-process cache", async () => {
  const timeout = new GitHubReleaseChecker({
    timeoutMs: 1,
    fetchRelease: async (_url, { signal }) => new Promise((_resolve, reject) => {
      signal.addEventListener("abort", () => {
        const error = new Error("aborted");
        error.name = "AbortError";
        reject(error);
      }, { once: true });
    }),
  });
  assert.deepEqual(await timeout.check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    reason: "github_release_timeout",
  });

  let fetches = 0;
  const cached = new GitHubReleaseChecker({
    fetchRelease: async () => {
      fetches += 1;
      return { ok: true, status: 200, async json() { return githubRelease; } };
    },
  });
  await cached.check("0.7.0-rc.1", "darwin-arm64", "5.7");
  await cached.check("0.7.0-rc.1", "win32-x64", "5.8");
  assert.equal(fetches, 1);
});

test("requests the public latest Release endpoint with explicit GitHub headers", async () => {
  let request: { url: string; headers: Readonly<Record<string, string>> } | undefined;
  const releases = new GitHubReleaseChecker({
    fetchRelease: async (url, { headers }) => {
      request = { url, headers };
      return { ok: true, status: 200, async json() { return githubRelease; } };
    },
  });

  assert.deepEqual(await releases.check("0.7.1", "darwin-arm64", "5.7"), { status: "current" });
  assert.equal(request?.url, latestReleaseApiUrl);
  assert.equal(request?.headers.Accept, "application/vnd.github+json");
  assert.equal(request?.headers["X-GitHub-Api-Version"], "2022-11-28");
  assert.match(request?.headers["User-Agent"] ?? "", /^loomle-client\/\d+\.\d+\.\d+/);
});

test("requires one uploaded versioned asset with GitHub's SHA-256 digest", async () => {
  for (const release of [
    { ...githubRelease, prerelease: true },
    { ...githubRelease, tag_name: "v0.7.1-rc.1" },
    { ...githubRelease, assets: [] },
    {
      ...githubRelease,
      assets: [{ ...githubRelease.assets[0], digest: "sha256:invalid" }, githubRelease.assets[1]],
    },
    {
      ...githubRelease,
      assets: [{ ...githubRelease.assets[0], browser_download_url: "https://example.test/file.zip" }, githubRelease.assets[1]],
    },
  ]) {
    assert.deepEqual(await checker(release).check("0.7.0", "darwin-arm64", "5.7"), {
      status: "unknown",
      reason: "invalid_github_release",
    });
  }
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
    engineVersion: "5.8",
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
      engineVersion: "5.8",
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

test("discovers the installed UE version from the neighboring plugin descriptor", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-client-status-"));
  const executable = join(root, "Resources", "Loomle", "darwin-arm64", "loomle");
  try {
    await mkdir(join(root, "Resources", "Loomle", "darwin-arm64"), { recursive: true });
    await writeFile(
      join(root, "LoomleBridge.uplugin"),
      JSON.stringify({ EngineVersion: "5.8.0" }),
    );
    assert.equal(installedEngineVersion(executable), "5.8");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
