import { productVersion } from "./generated/product-version.js";
import type { SessionStatusController, SessionStatusReport } from "./runtime.js";

export const releaseManifestUrl = "https://loomle.ai/releases.json";

export interface ClientIdentity {
  version: string;
  pid: number;
  platform: NodeJS.Platform;
  target?: string;
  executable: string;
}

export interface UpdateReport {
  status: "current" | "available" | "unknown";
  version?: string;
  releaseUrl?: string;
  assetUrl?: string;
  sha256?: string;
  reason?: string;
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
  check(version: string, target: string | undefined): Promise<UpdateReport>;
}

interface StatusServiceOptions {
  version?: string;
  pid?: number;
  platform?: NodeJS.Platform;
  arch?: string;
  executable?: string;
  updateChecker?: UpdateChecker;
}

interface ManifestTarget {
  assetUrl: string;
  sha256: string;
}

interface ManifestRelease {
  version: string;
  releaseUrl: string;
  targets: Record<string, ManifestTarget>;
}

interface ReleaseManifest {
  schemaVersion: 1;
  channels: {
    stable: ManifestRelease | null;
    prerelease: ManifestRelease | null;
  };
}

interface FetchResponse {
  ok: boolean;
  status: number;
  json(): Promise<unknown>;
}

type FetchManifest = (
  url: string,
  init: { signal: AbortSignal },
) => Promise<FetchResponse>;

interface ReleaseManifestCheckerOptions {
  manifestUrl?: string;
  timeoutMs?: number;
  cacheTtlMs?: number;
  fetchManifest?: FetchManifest;
  now?: () => number;
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
    this.identity = {
      version: options.version ?? productVersion,
      pid: options.pid ?? process.pid,
      platform,
      ...(target ? { target } : {}),
      executable: options.executable ?? process.execPath,
    };
    this.checker = options.updateChecker ?? new ReleaseManifestChecker();
  }

  async report(): Promise<ClientStatusReport> {
    const [update, session] = await Promise.all([
      this.checker.check(this.identity.version, this.identity.target)
        .catch((error: unknown) => unknownUpdate(errorReason(error))),
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

export class ReleaseManifestChecker implements UpdateChecker {
  private readonly manifestUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchManifest: FetchManifest;
  private readonly now: () => number;
  private cache?: { expiresAt: number; manifest: ReleaseManifest };

  constructor(options: ReleaseManifestCheckerOptions = {}) {
    this.manifestUrl = options.manifestUrl ?? releaseManifestUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchManifest = options.fetchManifest ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(version: string, target: string | undefined): Promise<UpdateReport> {
    if (!target) return unknownUpdate("unsupported_target");
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version");

    let manifest: ReleaseManifest;
    try {
      manifest = await this.loadManifest();
    } catch (error) {
      return unknownUpdate(errorReason(error));
    }

    const channel = current.prerelease ? manifest.channels.prerelease : manifest.channels.stable;
    if (!channel) return unknownUpdate("channel_unpublished");
    const available = parseVersion(channel.version);
    if (!available) return unknownUpdate("invalid_release_version");
    const asset = channel.targets[target];
    if (!asset) return unknownUpdate("unsupported_target");
    if (compareVersions(available, current) <= 0) return { status: "current" };
    return {
      status: "available",
      version: channel.version,
      releaseUrl: channel.releaseUrl,
      assetUrl: asset.assetUrl,
      sha256: asset.sha256,
    };
  }

  private async loadManifest(): Promise<ReleaseManifest> {
    if (this.cache && this.cache.expiresAt > this.now()) return this.cache.manifest;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await this.fetchManifest(this.manifestUrl, { signal: controller.signal });
      if (!response.ok) throw new Error(`manifest_http_${response.status}`);
      const manifest = parseManifest(await response.json());
      if (!manifest) throw new Error("invalid_release_manifest");
      this.cache = { expiresAt: this.now() + this.cacheTtlMs, manifest };
      return manifest;
    } finally {
      clearTimeout(timeout);
    }
  }
}

export function platformTarget(
  platform: NodeJS.Platform,
  arch: string,
): string | undefined {
  if (platform === "darwin" && arch === "arm64") return "darwin-arm64";
  if (platform === "win32" && arch === "x64") return "win32-x64";
  return undefined;
}

function parseManifest(value: unknown): ReleaseManifest | undefined {
  if (!isRecord(value) || value.schemaVersion !== 1 || !isRecord(value.channels)) {
    return undefined;
  }
  const stable = parseRelease(value.channels.stable);
  const prerelease = parseRelease(value.channels.prerelease);
  if (stable === undefined || prerelease === undefined) return undefined;
  return {
    schemaVersion: 1,
    channels: { stable, prerelease },
  };
}

function parseRelease(value: unknown): ManifestRelease | null | undefined {
  if (value === null) return null;
  if (!isRecord(value)
    || typeof value.version !== "string"
    || typeof value.releaseUrl !== "string"
    || !parseVersion(value.version)
    || !isHttpsUrl(value.releaseUrl)
    || !isRecord(value.targets)) {
    return undefined;
  }
  const targets: Record<string, ManifestTarget> = {};
  for (const [target, candidate] of Object.entries(value.targets)) {
    if (!isRecord(candidate)
      || typeof candidate.assetUrl !== "string"
      || !isHttpsUrl(candidate.assetUrl)
      || typeof candidate.sha256 !== "string"
      || !/^[0-9a-f]{64}$/i.test(candidate.sha256)) {
      return undefined;
    }
    targets[target] = {
      assetUrl: candidate.assetUrl,
      sha256: candidate.sha256,
    };
  }
  return { version: value.version, releaseUrl: value.releaseUrl, targets };
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

function unknownUpdate(reason: string): UpdateReport {
  return { status: "unknown", reason };
}

function errorReason(error: unknown): string {
  if (error instanceof Error) {
    if (error.name === "AbortError") return "manifest_timeout";
    return error.message.replaceAll(/\s+/g, "_").toLowerCase();
  }
  return String(error).replaceAll(/\s+/g, "_").toLowerCase();
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isHttpsUrl(value: string): boolean {
  try {
    return new URL(value).protocol === "https:";
  } catch {
    return false;
  }
}
