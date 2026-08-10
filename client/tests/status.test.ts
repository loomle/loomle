import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  AgentChannelChecker,
  ClientStatusService,
  FabChannelChecker,
  GitHubReleaseChecker,
  RegistryChannelChecker,
  claudeChannelDocumentUrl,
  fabChannelDocumentUrl,
  installedEngineVersion,
  latestReleaseApiUrl,
  mcpRegistryLatestUrl,
  platformTarget,
  updateCheckerFor,
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

const fabChannel = {
  schemaVersion: 1,
  channel: "fab",
  version: "0.7.1",
  publishedAt: "2026-08-10T00:00:00Z",
  listingUrl: "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428",
};

const bridge = {
  source: "github_release",
  tag: "v0.7.1",
  assets: {
    "ue5.7": {
      url: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.7.zip",
      sha256: "a".repeat(64),
    },
    "ue5.8": {
      url: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.8.zip",
      sha256: "b".repeat(64),
    },
  },
};

const registryVersion = {
  server: {
    name: "io.github.loomle/loomle",
    version: "0.7.1",
    packages: [{
      registryType: "mcpb",
      identifier: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-mcp-registry-0.7.1.mcpb",
      fileSha256: "c".repeat(64),
      transport: { type: "stdio" },
    }],
  },
  _meta: {
    "io.modelcontextprotocol.registry/official": { isLatest: true },
  },
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
    authority: "github",
    version: "0.7.1",
    releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
    assetUrl: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.7.zip",
    sha256: "a".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.0", "darwin-arm64", "5.8"), {
    status: "available",
    authority: "github",
    version: "0.7.1",
    releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
    assetUrl: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.8.zip",
    sha256: "b".repeat(64),
  });
  assert.deepEqual(await releases.check("0.7.1", "win32-x64", "5.7"), {
    status: "current",
    authority: "github",
  });
  assert.deepEqual(await releases.check("0.8.0-rc.1", "darwin-arm64", "5.7"), {
    status: "current",
    authority: "github",
  });
});

test("keeps malformed, offline, and unsupported update discovery informational", async () => {
  assert.deepEqual(await checker({ bad: true }).check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "github",
    reason: "invalid_github_release",
  });
  assert.deepEqual(await checker({}, { ok: false, status: 503 })
    .check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "github",
    reason: "github_release_http_503",
  });
  assert.deepEqual(await checker(githubRelease).check("0.7.0-rc.1", "linux-x64", "5.7"), {
    status: "unknown",
    authority: "github",
    reason: "unsupported_target",
  });
  assert.deepEqual(await checker(githubRelease).check("0.7.0-rc.1", "darwin-arm64", "5.6"), {
    status: "unknown",
    authority: "github",
    reason: "unsupported_engine_version",
  });
  const offline = new GitHubReleaseChecker({
    fetchRelease: async () => {
      throw new Error("network_offline");
    },
  });
  assert.deepEqual(await offline.check("0.7.0-rc.1", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "github",
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
    authority: "github",
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

  assert.deepEqual(await releases.check("0.7.1", "darwin-arm64", "5.7"), {
    status: "current",
    authority: "github",
  });
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
      authority: "github",
      reason: "invalid_github_release",
    });
  }
});

test("Fab Clients check only Loomle's published Fab channel pointer", async () => {
  let requestUrl: string | undefined;
  const checker = new FabChannelChecker({
    fetchDocument: async (url) => {
      requestUrl = url;
      return { ok: true, status: 200, async json() { return fabChannel; } };
    },
  });
  assert.deepEqual(await checker.check("0.7.0", "darwin-arm64", "5.8"), {
    status: "available",
    authority: "fab",
    version: "0.7.1",
    listing: fabChannel.listingUrl,
  });
  assert.equal(requestUrl, fabChannelDocumentUrl);
  assert.deepEqual(await checker.check("0.7.1", "win32-x64", "5.7"), {
    status: "current",
    authority: "fab",
  });
});

test("Fab channel documents are strict and failures stay informational", async () => {
  const invalid = new FabChannelChecker({
    fetchDocument: async () => ({
      ok: true,
      status: 200,
      async json() { return { ...fabChannel, extra: true }; },
    }),
  });
  assert.deepEqual(await invalid.check("0.7.0", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "fab",
    reason: "invalid_fab_channel_document",
  });
  const offline = new FabChannelChecker({
    fetchDocument: async () => {
      throw new Error("network offline");
    },
  });
  assert.deepEqual(await offline.check("0.7.0", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "fab",
    reason: "network_offline",
  });
});

test("Claude uses only its own strict channel document", async () => {
  let requested: string | undefined;
  const listingUrl = "https://claude.ai/directory/connectors/loomle";
  const checker = new AgentChannelChecker({
    fetchDocument: async (url) => {
      requested = url;
      return {
        ok: true,
        status: 200,
        async json() {
          return {
            schemaVersion: 1,
            channel: "claude",
            version: "0.7.1",
            publishedAt: "2026-08-10T00:00:00Z",
            listingUrl,
            bridge,
          };
        },
      };
    },
  });
  assert.deepEqual(await checker.check("0.7.0", "darwin-arm64", "5.8"), {
    status: "available",
    authority: "claude",
    version: "0.7.1",
    listing: listingUrl,
    bridge: {
      version: "0.7.1",
      releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
      assetUrl: bridge.assets["ue5.8"].url,
      sha256: "b".repeat(64),
    },
  });
  assert.equal(requested, claudeChannelDocumentUrl);
  assert.deepEqual(await checker.check("0.7.2", "darwin-arm64", "5.8"), {
    status: "current",
    authority: "claude",
  });
});

test("agent channel documents reject cross-channel and unversioned Bridge data", async () => {
  const invalid = new AgentChannelChecker({
    fetchDocument: async () => ({
      ok: true,
      status: 200,
      async json() {
        return {
          schemaVersion: 1,
          channel: "codex",
          version: "0.7.1",
          publishedAt: "2026-08-10T00:00:00Z",
          listingUrl: "https://claude.ai/directory/connectors/loomle",
          bridge: {
            ...bridge,
            assets: {
              ...bridge.assets,
              "ue5.7": {
                ...bridge.assets["ue5.7"],
                url: "https://github.com/loomle/loomle/releases/latest/download/loomle-bridge-ue5.7.zip",
              },
            },
          },
        };
      },
    }),
  });
  assert.deepEqual(await invalid.check("0.7.0", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "claude",
    reason: "invalid_claude_channel_document",
  });
});

test("MCP Registry resolves its version and the exact matching Bridge release", async () => {
  let registryRequest: string | undefined;
  let releaseRequest: string | undefined;
  const checker = new RegistryChannelChecker({
    fetchRegistry: async (url) => {
      registryRequest = url;
      return { ok: true, status: 200, async json() { return registryVersion; } };
    },
    fetchRelease: async (url) => {
      releaseRequest = url;
      return { ok: true, status: 200, async json() { return githubRelease; } };
    },
  });
  assert.deepEqual(await checker.check("0.7.0", "win32-x64", "5.7"), {
    status: "available",
    authority: "mcp_registry",
    version: "0.7.1",
    listing: "https://registry.modelcontextprotocol.io/?q=io.github.loomle%2Floomle",
    bridge: {
      version: "0.7.1",
      releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
      assetUrl: bridge.assets["ue5.7"].url,
      sha256: "a".repeat(64),
    },
  });
  assert.equal(registryRequest, mcpRegistryLatestUrl);
  assert.equal(
    releaseRequest,
    "https://api.github.com/repos/loomle/loomle/releases/tags/v0.7.1",
  );
});

test("MCP Registry rejects package and Bridge version drift", async () => {
  const checker = new RegistryChannelChecker({
    fetchRegistry: async () => ({
      ok: true,
      status: 200,
      async json() { return registryVersion; },
    }),
    fetchRelease: async () => ({
      ok: true,
      status: 200,
      async json() { return { ...githubRelease, tag_name: "v0.7.2" }; },
    }),
  });
  assert.deepEqual(await checker.check("0.7.0", "darwin-arm64", "5.7"), {
    status: "unknown",
    authority: "mcp_registry",
    reason: "invalid_mcp_registry_bridge_release",
  });
  assert.equal((await updateCheckerFor("development").check(
    "0.7.1",
    undefined,
    undefined,
  )).authority, undefined);
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
    distribution: "fab",
    updateChecker: {
      async check() {
        throw new Error("network unavailable");
      },
    },
  });

  assert.deepEqual(await service.report(), {
    client: {
      version: "0.7.0-rc.1",
      distribution: "fab",
      pid: 42,
      platform: "darwin",
      target: "darwin-arm64",
      engineVersion: "5.8",
      executable: "/plugin/loomle",
    },
    update: { status: "unknown", authority: "fab", reason: "network_unavailable" },
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
