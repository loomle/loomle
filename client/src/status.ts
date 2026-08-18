import { productVersion } from "./generated/product-version.js";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import type { SessionStatusController, SessionStatusReport } from "./runtime.js";
import {
  resolveDistributionChannel,
  type DistributionChannel,
} from "./distribution.js";

export const latestReleaseApiUrl = "https://api.github.com/repos/loomle/loomle/releases/latest";
export const fabChannelDocumentUrl = "https://loomle.ai/channels/fab.json";
export const claudeChannelDocumentUrl = "https://loomle.ai/channels/claude.json";
export const mcpRegistryLatestUrl = "https://registry.modelcontextprotocol.io/v0.1/servers/io.github.loomle%2Floomle/versions/latest";

const githubRepositoryUrl = "https://github.com/loomle/loomle";
const fabListingUrl = "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428";
const registryListingUrl = "https://registry.modelcontextprotocol.io/?q=io.github.loomle%2Floomle";
const githubApiVersion = "2022-11-28";
const supportedReleaseTargets = new Set(["darwin-arm64", "win32-x64"]);
const supportedEngineVersions = new Set(["5.7", "5.8"]);

export interface ClientIdentity {
  version: string;
  distribution: DistributionChannel;
  pid: number;
  platform: NodeJS.Platform;
  target?: string;
  engineVersion?: string;
  executable: string;
}

export interface UpdateReport {
  status: "current" | "available" | "unknown";
  authority?: Exclude<DistributionChannel, "development">;
  version?: string;
  listing?: string;
  releaseUrl?: string;
  assetUrl?: string;
  sha256?: string;
  bridge?: BridgeRecommendation;
  reason?: string;
}

export interface BridgeRecommendation {
  version: string;
  releaseUrl: string;
  assetUrl: string;
  sha256: string;
}

export interface ClientStatusReport {
  client: ClientIdentity;
  update: UpdateReport;
  session: SessionStatusReport;
}

export interface StatusProvider {
  report(): Promise<ClientStatusReport>;
}

export interface UpdateChecker {
  check(
    version: string,
    target: string | undefined,
    engineVersion: string | undefined,
  ): Promise<UpdateReport>;
}

interface StatusServiceOptions {
  version?: string;
  pid?: number;
  platform?: NodeJS.Platform;
  arch?: string;
  executable?: string;
  engineVersion?: string;
  distribution?: DistributionChannel;
  environment?: Readonly<Record<string, string | undefined>>;
  updateChecker?: UpdateChecker;
}

interface GitHubRelease {
  version: string;
  releaseUrl: string;
  assetUrl: string;
  sha256: string;
}

interface FetchResponse {
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
}

type FetchRelease = (
  url: string,
  init: { signal: AbortSignal; headers: Readonly<Record<string, string>> },
) => Promise<FetchResponse>;

interface GitHubReleaseCheckerOptions {
  latestReleaseUrl?: string;
  timeoutMs?: number;
  cacheTtlMs?: number;
  fetchRelease?: FetchRelease;
  now?: () => number;
}

interface FabChannelDocument {
  version: string;
  listing: string;
}

interface FabChannelCheckerOptions {
  channelDocumentUrl?: string;
  timeoutMs?: number;
  cacheTtlMs?: number;
  fetchDocument?: FetchRelease;
  now?: () => number;
}

interface AgentChannelCheckerOptions {
  channelDocumentUrl?: string;
  timeoutMs?: number;
  cacheTtlMs?: number;
  fetchDocument?: FetchRelease;
  now?: () => number;
}

interface RegistryChannelCheckerOptions {
  latestVersionUrl?: string;
  timeoutMs?: number;
  cacheTtlMs?: number;
  fetchRegistry?: FetchRelease;
  fetchRelease?: FetchRelease;
  now?: () => number;
}

interface AgentChannelDocument {
  version: string;
  listing: string;
  bridge: BridgeRecommendation;
}

export class ClientStatusService implements StatusProvider {
  private readonly identity: ClientIdentity;
  private readonly checker: UpdateChecker;

  constructor(
    private readonly session: Partial<SessionStatusController>,
    options: StatusServiceOptions = {},
  ) {
    const platform = options.platform ?? process.platform;
    const target = platformTarget(platform, options.arch ?? process.arch);
    const executable = options.executable ?? process.execPath;
    const engineVersion = options.engineVersion ?? installedEngineVersion(executable);
    const distribution = options.distribution ?? resolveDistributionChannel({
      executable,
      environment: options.environment,
    });
    this.identity = {
      version: options.version ?? productVersion,
      distribution,
      pid: options.pid ?? process.pid,
      platform,
      ...(target ? { target } : {}),
      ...(engineVersion ? { engineVersion } : {}),
      executable,
    };
    this.checker = options.updateChecker ?? updateCheckerFor(distribution);
  }

  async report(): Promise<ClientStatusReport> {
    const [update, session] = await Promise.all([
      this.checker.check(
        this.identity.version,
        this.identity.target,
        this.identity.engineVersion,
      )
        .catch((error: unknown) => unknownUpdate(
          errorReason(error),
          updateAuthority(this.identity.distribution),
        )),
      this.session.sessionStatus
        ? this.session.sessionStatus()
          .catch((error: unknown) => ({
            status: "unknown" as const,
            reason: errorReason(error),
          }))
        : Promise.resolve({
          status: "unknown" as const,
          reason: "session_status_unavailable",
        }),
    ]);
    return { client: this.identity, update, session };
  }
}

export class GitHubReleaseChecker implements UpdateChecker {
  private readonly latestReleaseUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchRelease: FetchRelease;
  private readonly now: () => number;
  private cache?: { expiresAt: number; value: unknown };

  constructor(options: GitHubReleaseCheckerOptions = {}) {
    this.latestReleaseUrl = options.latestReleaseUrl ?? latestReleaseApiUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchRelease = options.fetchRelease ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(
    version: string,
    target: string | undefined,
    engineVersion: string | undefined,
  ): Promise<UpdateReport> {
    if (!target || !supportedReleaseTargets.has(target)) {
      return unknownUpdate("unsupported_target", "github");
    }
    if (!engineVersion || !supportedEngineVersions.has(engineVersion)) {
      return unknownUpdate("unsupported_engine_version", "github");
    }
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version", "github");

    let release: GitHubRelease;
    try {
      release = await this.loadRelease(engineVersion);
    } catch (error) {
      return unknownUpdate(errorReason(error), "github");
    }

    const available = parseVersion(release.version);
    if (!available || available.prerelease) {
      return unknownUpdate("invalid_release_version", "github");
    }
    if (compareVersions(available, current) <= 0) {
      return { status: "current", authority: "github" };
    }
    return {
      status: "available",
      authority: "github",
      version: release.version,
      releaseUrl: release.releaseUrl,
      assetUrl: release.assetUrl,
      sha256: release.sha256,
    };
  }

  private async loadRelease(engineVersion: string): Promise<GitHubRelease> {
    if (this.cache && this.cache.expiresAt > this.now()) {
      const cachedRelease = parseGitHubRelease(this.cache.value, engineVersion);
      if (!cachedRelease) throw new Error("invalid_github_release");
      return cachedRelease;
    }
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await this.fetchRelease(this.latestReleaseUrl, {
        signal: controller.signal,
        headers: {
          Accept: "application/vnd.github+json",
          "User-Agent": `loomle-client/${productVersion}`,
          "X-GitHub-Api-Version": githubApiVersion,
        },
      });
      if (!response.ok) throw new Error(`github_release_http_${response.status}`);
      const value = await response.json();
      const release = parseGitHubRelease(value, engineVersion);
      if (!release) throw new Error("invalid_github_release");
      this.cache = { expiresAt: this.now() + this.cacheTtlMs, value };
      return release;
    } catch (error) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error("github_release_timeout");
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }
}

export class FabChannelChecker implements UpdateChecker {
  private readonly channelDocumentUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchDocument: FetchRelease;
  private readonly now: () => number;
  private cache?: { expiresAt: number; value: FabChannelDocument };

  constructor(options: FabChannelCheckerOptions = {}) {
    this.channelDocumentUrl = options.channelDocumentUrl ?? fabChannelDocumentUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchDocument = options.fetchDocument ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(
    version: string,
    target: string | undefined,
    engineVersion: string | undefined,
  ): Promise<UpdateReport> {
    if (!target || !supportedReleaseTargets.has(target)) {
      return unknownUpdate("unsupported_target", "fab");
    }
    if (!engineVersion || !supportedEngineVersions.has(engineVersion)) {
      return unknownUpdate("unsupported_engine_version", "fab");
    }
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version", "fab");

    let document: FabChannelDocument;
    try {
      document = await this.loadDocument();
    } catch (error) {
      return unknownUpdate(errorReason(error), "fab");
    }
    const available = parseVersion(document.version);
    if (!available || available.prerelease) {
      return unknownUpdate("invalid_fab_channel_version", "fab");
    }
    if (compareVersions(available, current) <= 0) {
      return { status: "current", authority: "fab" };
    }
    return {
      status: "available",
      authority: "fab",
      version: document.version,
      listing: document.listing,
    };
  }

  private async loadDocument(): Promise<FabChannelDocument> {
    if (this.cache && this.cache.expiresAt > this.now()) return this.cache.value;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await this.fetchDocument(this.channelDocumentUrl, {
        signal: controller.signal,
        headers: {
          Accept: "application/json",
          "User-Agent": `loomle-client/${productVersion}`,
        },
      });
      if (!response.ok) throw new Error(`fab_channel_http_${response.status}`);
      const document = parseFabChannelDocument(await response.json());
      if (!document) throw new Error("invalid_fab_channel_document");
      this.cache = { expiresAt: this.now() + this.cacheTtlMs, value: document };
      return document;
    } catch (error) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error("fab_channel_timeout");
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }
}

export class AgentChannelChecker implements UpdateChecker {
  private readonly channelDocumentUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchDocument: FetchRelease;
  private readonly now: () => number;
  private cache?: { expiresAt: number; value: unknown };

  constructor(options: AgentChannelCheckerOptions = {}) {
    this.channelDocumentUrl = options.channelDocumentUrl ?? claudeChannelDocumentUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchDocument = options.fetchDocument ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(
    version: string,
    target: string | undefined,
    engineVersion: string | undefined,
  ): Promise<UpdateReport> {
    if (!target || !supportedReleaseTargets.has(target)) {
      return unknownUpdate("unsupported_target", "claude");
    }
    if (!engineVersion || !supportedEngineVersions.has(engineVersion)) {
      return unknownUpdate("unsupported_engine_version", "claude");
    }
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version", "claude");

    let document: AgentChannelDocument;
    try {
      document = await this.loadDocument(engineVersion);
    } catch (error) {
      return unknownUpdate(errorReason(error), "claude");
    }
    const available = parseVersion(document.version);
    if (!available || available.prerelease) {
      return unknownUpdate("invalid_claude_channel_version", "claude");
    }
    const comparison = compareVersions(available, current);
    if (comparison < 0) {
      return { status: "current", authority: "claude" };
    }
    if (comparison === 0) {
      return { status: "current", authority: "claude", bridge: document.bridge };
    }
    return {
      status: "available",
      authority: "claude",
      version: document.version,
      listing: document.listing,
      bridge: document.bridge,
    };
  }

  private async loadDocument(engineVersion: string): Promise<AgentChannelDocument> {
    if (this.cache && this.cache.expiresAt > this.now()) {
      const cached = parseAgentChannelDocument(this.cache.value, engineVersion);
      if (!cached) throw new Error("invalid_claude_channel_document");
      return cached;
    }
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await this.fetchDocument(this.channelDocumentUrl, {
        signal: controller.signal,
        headers: {
          Accept: "application/json",
          "User-Agent": `loomle-client/${productVersion}`,
        },
      });
      if (!response.ok) throw new Error(`claude_channel_http_${response.status}`);
      const value = await response.json();
      const document = parseAgentChannelDocument(value, engineVersion);
      if (!document) throw new Error("invalid_claude_channel_document");
      this.cache = { expiresAt: this.now() + this.cacheTtlMs, value };
      return document;
    } catch (error) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error("claude_channel_timeout");
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }
}

export class RegistryChannelChecker implements UpdateChecker {
  private readonly latestVersionUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchRegistry: FetchRelease;
  private readonly fetchRelease: FetchRelease;
  private readonly now: () => number;
  private cache?: { expiresAt: number; registry: unknown; release: unknown };

  constructor(options: RegistryChannelCheckerOptions = {}) {
    this.latestVersionUrl = options.latestVersionUrl ?? mcpRegistryLatestUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchRegistry = options.fetchRegistry ?? ((url, init) => fetch(url, init));
    this.fetchRelease = options.fetchRelease ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(
    version: string,
    target: string | undefined,
    engineVersion: string | undefined,
  ): Promise<UpdateReport> {
    if (!target || !supportedReleaseTargets.has(target)) {
      return unknownUpdate("unsupported_target", "mcp_registry");
    }
    if (!engineVersion || !supportedEngineVersions.has(engineVersion)) {
      return unknownUpdate("unsupported_engine_version", "mcp_registry");
    }
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version", "mcp_registry");

    let document: AgentChannelDocument;
    try {
      document = await this.loadDocument(engineVersion);
    } catch (error) {
      return unknownUpdate(errorReason(error), "mcp_registry");
    }
    const available = parseVersion(document.version);
    if (!available || available.prerelease) {
      return unknownUpdate("invalid_mcp_registry_version", "mcp_registry");
    }
    const comparison = compareVersions(available, current);
    if (comparison < 0) {
      return { status: "current", authority: "mcp_registry" };
    }
    if (comparison === 0) {
      return { status: "current", authority: "mcp_registry", bridge: document.bridge };
    }
    return {
      status: "available",
      authority: "mcp_registry",
      version: document.version,
      listing: registryListingUrl,
      bridge: document.bridge,
    };
  }

  private async loadDocument(engineVersion: string): Promise<AgentChannelDocument> {
    if (this.cache && this.cache.expiresAt > this.now()) {
      const cachedVersion = parseRegistryVersion(this.cache.registry);
      const cachedBridge = cachedVersion
        ? parseGitHubRelease(this.cache.release, engineVersion)
        : undefined;
      if (!cachedVersion || !cachedBridge || cachedVersion !== cachedBridge.version) {
        throw new Error("invalid_mcp_registry_response");
      }
      return { version: cachedVersion, listing: registryListingUrl, bridge: cachedBridge };
    }
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const registryResponse = await this.fetchRegistry(this.latestVersionUrl, {
        signal: controller.signal,
        headers: {
          Accept: "application/json",
          "User-Agent": `loomle-client/${productVersion}`,
        },
      });
      if (!registryResponse.ok) {
        throw new Error(`mcp_registry_http_${registryResponse.status}`);
      }
      const registry = await registryResponse.json();
      const registryVersion = parseRegistryVersion(registry);
      if (!registryVersion) throw new Error("invalid_mcp_registry_response");
      const releaseUrl = `https://api.github.com/repos/loomle/loomle/releases/tags/v${registryVersion}`;
      const releaseResponse = await this.fetchRelease(releaseUrl, {
        signal: controller.signal,
        headers: {
          Accept: "application/vnd.github+json",
          "User-Agent": `loomle-client/${productVersion}`,
          "X-GitHub-Api-Version": githubApiVersion,
        },
      });
      if (!releaseResponse.ok) {
        throw new Error(`mcp_registry_bridge_http_${releaseResponse.status}`);
      }
      const release = await releaseResponse.json();
      const bridge = parseGitHubRelease(release, engineVersion);
      if (!bridge || bridge.version !== registryVersion) {
        throw new Error("invalid_mcp_registry_bridge_release");
      }
      this.cache = {
        expiresAt: this.now() + this.cacheTtlMs,
        registry,
        release,
      };
      return { version: registryVersion, listing: registryListingUrl, bridge };
    } catch (error) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error("mcp_registry_timeout");
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }
}

class NoUpdateChecker implements UpdateChecker {
  constructor(private readonly distribution: DistributionChannel) {}

  async check(): Promise<UpdateReport> {
    return unknownUpdate(
      `${this.distribution}_update_source_not_implemented`,
      updateAuthority(this.distribution),
    );
  }
}

export function updateCheckerFor(distribution: DistributionChannel): UpdateChecker {
  if (distribution === "github") return new GitHubReleaseChecker();
  if (distribution === "fab") return new FabChannelChecker();
  if (distribution === "mcp_registry") return new RegistryChannelChecker();
  if (distribution === "claude") return new AgentChannelChecker();
  return new NoUpdateChecker(distribution);
}

export function platformTarget(
  platform: NodeJS.Platform,
  arch: string,
): string | undefined {
  if (platform === "darwin" && arch === "arm64") return "darwin-arm64";
  if (platform === "win32" && arch === "x64") return "win32-x64";
  return undefined;
}

function parseGitHubRelease(
  value: unknown,
  engineVersion: string,
): GitHubRelease | undefined {
  if (!isRecord(value)
    || value.draft !== false
    || value.prerelease !== false
    || typeof value.tag_name !== "string"
    || typeof value.html_url !== "string"
    || !Array.isArray(value.assets)) {
    return undefined;
  }

  const tagMatch = /^v(\d+\.\d+\.\d+)$/.exec(value.tag_name);
  if (!tagMatch) return undefined;
  const version = tagMatch[1];
  const assetName = `loomle-bridge-${version}-ue${engineVersion}.zip`;
  const expectedReleaseUrl = `${githubRepositoryUrl}/releases/tag/${value.tag_name}`;
  const expectedAssetUrl = `${githubRepositoryUrl}/releases/download/${value.tag_name}/${assetName}`;
  if (value.html_url !== expectedReleaseUrl) return undefined;

  const matches = value.assets.filter((candidate) => (
    isRecord(candidate) && candidate.name === assetName
  ));
  if (matches.length !== 1) return undefined;
  const asset = matches[0];
  if (asset.state !== "uploaded"
    || typeof asset.browser_download_url !== "string"
    || asset.browser_download_url !== expectedAssetUrl
    || typeof asset.digest !== "string"
    || !/^sha256:[0-9a-f]{64}$/i.test(asset.digest)
    || typeof asset.size !== "number"
    || !Number.isSafeInteger(asset.size)
    || asset.size <= 0) {
    return undefined;
  }

  return {
    version,
    releaseUrl: value.html_url,
    assetUrl: asset.browser_download_url,
    sha256: asset.digest.slice("sha256:".length).toLowerCase(),
  };
}

function parseFabChannelDocument(value: unknown): FabChannelDocument | undefined {
  if (!isRecord(value)) return undefined;
  const keys = Object.keys(value).sort();
  if (keys.length !== 5
    || keys[0] !== "channel"
    || keys[1] !== "listingUrl"
    || keys[2] !== "publishedAt"
    || keys[3] !== "schemaVersion"
    || keys[4] !== "version"
    || value.schemaVersion !== 1
    || value.channel !== "fab"
    || typeof value.version !== "string"
    || typeof value.publishedAt !== "string"
    || !isIsoTimestamp(value.publishedAt)
    || value.listingUrl !== fabListingUrl) {
    return undefined;
  }
  const version = parseVersion(value.version);
  if (!version || version.prerelease) return undefined;
  return { version: value.version, listing: fabListingUrl };
}

function parseAgentChannelDocument(
  value: unknown,
  engineVersion: string,
): AgentChannelDocument | undefined {
  if (!isRecord(value)
    || !hasExactKeys(value, [
      "schemaVersion",
      "channel",
      "version",
      "publishedAt",
      "listingUrl",
      "bridge",
    ])
    || value.schemaVersion !== 1
    || value.channel !== "claude"
    || typeof value.version !== "string"
    || typeof value.publishedAt !== "string"
    || !isIsoTimestamp(value.publishedAt)
    || typeof value.listingUrl !== "string") {
    return undefined;
  }
  const version = parseVersion(value.version);
  if (!version || version.prerelease) return undefined;
  let listing;
  try {
    listing = new URL(value.listingUrl);
  } catch {
    return undefined;
  }
  if (listing.protocol !== "https:"
    || !["claude.ai", "claude.com"].includes(listing.hostname)
    || listing.username
    || listing.password
    || listing.hash) {
    return undefined;
  }
  const bridge = parseBridgeRecommendation(value.bridge, value.version, engineVersion);
  if (!bridge) return undefined;
  return { version: value.version, listing: value.listingUrl, bridge };
}

function parseBridgeRecommendation(
  value: unknown,
  version: string,
  engineVersion: string,
): BridgeRecommendation | undefined {
  if (!isRecord(value)
    || !hasExactKeys(value, ["source", "tag", "assets"])
    || value.source !== "github_release"
    || value.tag !== `v${version}`
    || !isRecord(value.assets)
    || !hasExactKeys(value.assets, ["ue5.7", "ue5.8"])) {
    return undefined;
  }
  const asset = value.assets[`ue${engineVersion}`];
  if (!isRecord(asset)
    || !hasExactKeys(asset, ["url", "sha256"])
    || typeof asset.url !== "string"
    || typeof asset.sha256 !== "string"
    || !/^[0-9a-f]{64}$/.test(asset.sha256)) {
    return undefined;
  }
  const name = `loomle-bridge-${version}-ue${engineVersion}.zip`;
  const expectedAssetUrl = `${githubRepositoryUrl}/releases/download/v${version}/${name}`;
  if (asset.url !== expectedAssetUrl) return undefined;
  return {
    version,
    releaseUrl: `${githubRepositoryUrl}/releases/tag/v${version}`,
    assetUrl: asset.url,
    sha256: asset.sha256,
  };
}

function parseRegistryVersion(value: unknown): string | undefined {
  if (!isRecord(value) || !isRecord(value.server)) return undefined;
  const server = value.server;
  if (server.name !== "io.github.loomle/loomle"
    || typeof server.version !== "string"
    || !Array.isArray(server.packages)) {
    return undefined;
  }
  const version = parseVersion(server.version);
  if (!version || version.prerelease) return undefined;
  const identifier = `${githubRepositoryUrl}/releases/download/v${server.version}/loomle-mcp-registry-${server.version}.mcpb`;
  const matches = server.packages.filter((candidate) => (
    isRecord(candidate)
    && candidate.registryType === "mcpb"
    && candidate.identifier === identifier
    && typeof candidate.fileSha256 === "string"
    && /^[0-9a-f]{64}$/.test(candidate.fileSha256)
    && isRecord(candidate.transport)
    && candidate.transport.type === "stdio"
  ));
  return matches.length === 1 ? server.version : undefined;
}

export function installedEngineVersion(executable: string): string | undefined {
  const descriptorPath = resolve(
    dirname(executable),
    "..",
    "..",
    "..",
    "LoomleBridge.uplugin",
  );
  try {
    const descriptor = JSON.parse(readFileSync(descriptorPath, "utf8")) as unknown;
    if (!isRecord(descriptor) || typeof descriptor.EngineVersion !== "string") {
      return undefined;
    }
    const match = /^(5\.(?:7|8))(?:\.\d+)?$/.exec(descriptor.EngineVersion);
    return match?.[1];
  } catch {
    return undefined;
  }
}

interface ParsedVersion {
  major: number;
  minor: number;
  patch: number;
  prerelease?: readonly (number | string)[];
}

function parseVersion(value: string): ParsedVersion | undefined {
  const match = /^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$/.exec(value);
  if (!match) return undefined;
  const prereleaseParts = match[4]?.split(".");
  if (prereleaseParts?.some((part) => part.length === 0)) return undefined;
  const prerelease = prereleaseParts?.map((part) =>
    /^\d+$/.test(part) ? Number.parseInt(part, 10) : part);
  return {
    major: Number.parseInt(match[1], 10),
    minor: Number.parseInt(match[2], 10),
    patch: Number.parseInt(match[3], 10),
    ...(prerelease ? { prerelease } : {}),
  };
}

function compareVersions(left: ParsedVersion, right: ParsedVersion): number {
  for (const key of ["major", "minor", "patch"] as const) {
    if (left[key] !== right[key]) return left[key] < right[key] ? -1 : 1;
  }
  if (!left.prerelease && !right.prerelease) return 0;
  if (!left.prerelease) return 1;
  if (!right.prerelease) return -1;
  const length = Math.max(left.prerelease.length, right.prerelease.length);
  for (let index = 0; index < length; index += 1) {
    const leftPart = left.prerelease[index];
    const rightPart = right.prerelease[index];
    if (leftPart === undefined) return -1;
    if (rightPart === undefined) return 1;
    if (leftPart === rightPart) continue;
    if (typeof leftPart === "number" && typeof rightPart === "string") return -1;
    if (typeof leftPart === "string" && typeof rightPart === "number") return 1;
    return leftPart < rightPart ? -1 : 1;
  }
  return 0;
}

function unknownUpdate(
  reason: string,
  authority?: Exclude<DistributionChannel, "development">,
): UpdateReport {
  return { status: "unknown", ...(authority ? { authority } : {}), reason };
}

function updateAuthority(
  distribution: DistributionChannel,
): Exclude<DistributionChannel, "development"> | undefined {
  return distribution === "development" ? undefined : distribution;
}

function isIsoTimestamp(value: string): boolean {
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{3})?Z$/.test(value)) return false;
  const milliseconds = Date.parse(value);
  if (!Number.isFinite(milliseconds)) return false;
  const canonical = new Date(milliseconds).toISOString();
  return value.includes(".") ? canonical === value : canonical.replace(".000Z", "Z") === value;
}

function errorReason(error: unknown): string {
  if (error instanceof Error) {
    return error.message.replaceAll(/\s+/g, "_").toLowerCase();
  }
  return String(error).replaceAll(/\s+/g, "_").toLowerCase();
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function hasExactKeys(value: Record<string, unknown>, expected: readonly string[]): boolean {
  const actual = Object.keys(value).sort();
  const required = [...expected].sort();
  return JSON.stringify(actual) === JSON.stringify(required);
}
