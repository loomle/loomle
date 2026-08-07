import { productVersion } from "./generated/product-version.js";
import type { SessionStatusController, SessionStatusReport } from "./runtime.js";

export const latestReleaseApiUrl = "https://api.github.com/repos/loomle/loomle/releases/latest";

const githubRepositoryUrl = "https://github.com/loomle/loomle";
const githubApiVersion = "2022-11-28";
const supportedReleaseTargets = new Set(["darwin-arm64", "win32-x64"]);

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
    this.checker = options.updateChecker ?? new GitHubReleaseChecker();
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

export class GitHubReleaseChecker implements UpdateChecker {
  private readonly latestReleaseUrl: string;
  private readonly timeoutMs: number;
  private readonly cacheTtlMs: number;
  private readonly fetchRelease: FetchRelease;
  private readonly now: () => number;
  private cache?: { expiresAt: number; release: GitHubRelease };

  constructor(options: GitHubReleaseCheckerOptions = {}) {
    this.latestReleaseUrl = options.latestReleaseUrl ?? latestReleaseApiUrl;
    this.timeoutMs = options.timeoutMs ?? 2_000;
    this.cacheTtlMs = options.cacheTtlMs ?? 6 * 60 * 60 * 1_000;
    this.fetchRelease = options.fetchRelease ?? ((url, init) => fetch(url, init));
    this.now = options.now ?? Date.now;
  }

  async check(version: string, target: string | undefined): Promise<UpdateReport> {
    if (!target || !supportedReleaseTargets.has(target)) return unknownUpdate("unsupported_target");
    const current = parseVersion(version);
    if (!current) return unknownUpdate("invalid_client_version");

    let release: GitHubRelease;
    try {
      release = await this.loadRelease();
    } catch (error) {
      return unknownUpdate(errorReason(error));
    }

    const available = parseVersion(release.version);
    if (!available || available.prerelease) return unknownUpdate("invalid_release_version");
    if (compareVersions(available, current) <= 0) return { status: "current" };
    return {
      status: "available",
      version: release.version,
      releaseUrl: release.releaseUrl,
      assetUrl: release.assetUrl,
      sha256: release.sha256,
    };
  }

  private async loadRelease(): Promise<GitHubRelease> {
    if (this.cache && this.cache.expiresAt > this.now()) return this.cache.release;
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
      const release = parseGitHubRelease(await response.json());
      if (!release) throw new Error("invalid_github_release");
      this.cache = { expiresAt: this.now() + this.cacheTtlMs, release };
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

export function platformTarget(
  platform: NodeJS.Platform,
  arch: string,
): string | undefined {
  if (platform === "darwin" && arch === "arm64") return "darwin-arm64";
  if (platform === "win32" && arch === "x64") return "win32-x64";
  return undefined;
}

function parseGitHubRelease(value: unknown): GitHubRelease | undefined {
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
  const assetName = `loomle-bridge-${version}.zip`;
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
    return error.message.replaceAll(/\s+/g, "_").toLowerCase();
  }
  return String(error).replaceAll(/\s+/g, "_").toLowerCase();
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
