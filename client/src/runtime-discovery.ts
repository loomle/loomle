import { readFile, readdir, stat } from "node:fs/promises";
import { homedir } from "node:os";
import { isAbsolute, relative, resolve } from "node:path";

export interface RuntimeRecord {
  runtimeId: string;
  projectId?: string;
  name?: string;
  projectRoot: string;
  uproject?: string;
  endpoint: string;
  pid?: number;
  protocolVersion?: number;
  lastSeenAt?: string;
}

export interface RuntimeDiscoveryOptions {
  homeDirectory?: string;
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
  endpointAvailable?: (record: RuntimeRecord) => Promise<boolean>;
}

export class RuntimeSelectionError extends Error {
  constructor(
    readonly code: "runtime.not_found" | "runtime.offline" | "runtime.ambiguous",
    message: string,
    readonly candidates: readonly RuntimeRecord[] = [],
  ) {
    super(message);
    this.name = "RuntimeSelectionError";
  }
}

export async function loadRuntimeRecords(homeDirectory = homedir()): Promise<RuntimeRecord[]> {
  const directory = resolve(homeDirectory, ".loomle", "state", "runtimes");
  let entries;
  try {
    entries = await readdir(directory, { withFileTypes: true });
  } catch (error) {
    if (isMissing(error)) return [];
    throw error;
  }

  const records = await Promise.all(entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
    .map(async (entry) => {
      try {
        const value = JSON.parse(stripBom(await readFile(resolve(directory, entry.name), "utf8"))) as unknown;
        return parseRuntimeRecord(value, entry.name.slice(0, -5));
      } catch {
        return undefined;
      }
    }));

  return records
    .filter((record): record is RuntimeRecord => record !== undefined)
    .sort(compareRecords);
}

export async function resolveRuntime(options: RuntimeDiscoveryOptions = {}): Promise<RuntimeRecord> {
  const env = options.env ?? process.env;
  const platform = options.platform ?? process.platform;
  const records = await loadRuntimeRecords(options.homeDirectory);
  const endpointAvailable = options.endpointAvailable
    ?? ((record: RuntimeRecord) => defaultEndpointAvailable(record, platform));
  const online = (await Promise.all(records.map(async (record) => ({
    record,
    online: await endpointAvailable(record),
  })))).filter((entry) => entry.online).map((entry) => entry.record);

  const requestedRuntime = trimToUndefined(env.LOOMLE_RUNTIME_ID);
  if (requestedRuntime) {
    const matches = records.filter((record) =>
      record.runtimeId === requestedRuntime || record.projectId === requestedRuntime);
    if (matches.length === 0) {
      throw new RuntimeSelectionError(
        "runtime.not_found",
        `No Loomle runtime matches LOOMLE_RUNTIME_ID=${requestedRuntime}.`,
        online,
      );
    }
    const onlineMatches = matches.filter((record) => online.includes(record));
    if (onlineMatches.length === 0) {
      throw new RuntimeSelectionError(
        "runtime.offline",
        `The requested Loomle runtime is not online: ${matches.map(runtimeLabel).join(", ")}.`,
        matches,
      );
    }
    if (onlineMatches.length === 1) return onlineMatches[0];
    throw ambiguousRuntime(onlineMatches);
  }

  const requestedProject = trimToUndefined(env.LOOMLE_PROJECT_ROOT);
  if (requestedProject) {
    const matches = records.filter((record) => samePath(record.projectRoot, requestedProject, platform));
    if (matches.length === 0) {
      throw new RuntimeSelectionError(
        "runtime.not_found",
        `No Loomle runtime matches LOOMLE_PROJECT_ROOT=${requestedProject}.`,
        online,
      );
    }
    const onlineMatches = matches.filter((record) => online.includes(record));
    if (onlineMatches.length === 0) {
      throw new RuntimeSelectionError(
        "runtime.offline",
        `The requested project is not online: ${matches.map(runtimeLabel).join(", ")}.`,
        matches,
      );
    }
    if (onlineMatches.length === 1) return onlineMatches[0];
    throw ambiguousRuntime(onlineMatches);
  }

  const cwd = options.cwd ?? process.cwd();
  const workspaceMatches = online
    .filter((record) => pathContains(record.projectRoot, cwd, platform))
    .sort((left, right) => normalizePath(right.projectRoot, platform).length
      - normalizePath(left.projectRoot, platform).length);
  if (workspaceMatches.length > 0) {
    const longestRoot = normalizePath(workspaceMatches[0].projectRoot, platform).length;
    const mostSpecific = workspaceMatches.filter((record) =>
      normalizePath(record.projectRoot, platform).length === longestRoot);
    if (mostSpecific.length === 1) return mostSpecific[0];
    throw ambiguousRuntime(mostSpecific);
  }

  if (online.length === 1) return online[0];
  if (online.length === 0) {
    throw new RuntimeSelectionError(
      "runtime.not_found",
      "No online Loomle runtime was found. Start Unreal Editor with LoomleBridge enabled.",
    );
  }

  throw ambiguousRuntime(online);
}

function ambiguousRuntime(records: readonly RuntimeRecord[]): RuntimeSelectionError {
  return new RuntimeSelectionError(
    "runtime.ambiguous",
    `Multiple Loomle runtimes are online (${records.map(runtimeLabel).join(", ")}). Set LOOMLE_PROJECT_ROOT or LOOMLE_RUNTIME_ID for this MCP server.`,
    records,
  );
}

async function defaultEndpointAvailable(record: RuntimeRecord, platform: NodeJS.Platform): Promise<boolean> {
  if (!isProcessAlive(record.pid)) return false;

  if (platform === "win32") {
    return record.endpoint.startsWith("\\\\.\\pipe\\") || record.endpoint.startsWith("//./pipe/");
  }

  try {
    const metadata = await stat(record.endpoint);
    return metadata.isSocket();
  } catch {
    return false;
  }
}

function isProcessAlive(pid: number | undefined): boolean {
  if (pid === undefined) return true;
  if (!Number.isSafeInteger(pid) || pid <= 0) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return isNodeError(error) && error.code === "EPERM";
  }
}

function parseRuntimeRecord(value: unknown, fallbackId: string): RuntimeRecord | undefined {
  if (!isRecord(value)) return undefined;
  const projectRoot = stringField(value, "projectRoot");
  const endpoint = stringField(value, "endpoint");
  if (!projectRoot || !endpoint) return undefined;

  const runtimeId = stringField(value, "runtimeId")
    ?? stringField(value, "projectId")
    ?? fallbackId;
  const pid = numberField(value, "pid");
  const protocolVersion = numberField(value, "protocolVersion");
  return {
    runtimeId,
    ...(stringField(value, "projectId") ? { projectId: stringField(value, "projectId") } : {}),
    ...(stringField(value, "name") ? { name: stringField(value, "name") } : {}),
    projectRoot,
    ...(stringField(value, "uproject") ? { uproject: stringField(value, "uproject") } : {}),
    endpoint,
    ...(pid !== undefined ? { pid } : {}),
    ...(protocolVersion !== undefined ? { protocolVersion } : {}),
    ...(stringField(value, "lastSeenAt") ? { lastSeenAt: stringField(value, "lastSeenAt") } : {}),
  };
}

function stringField(object: Record<string, unknown>, key: string): string | undefined {
  const value = object[key];
  return typeof value === "string" && value.length > 0 ? value : undefined;
}

function numberField(object: Record<string, unknown>, key: string): number | undefined {
  const value = object[key];
  return typeof value === "number" && Number.isFinite(value) ? value : undefined;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function trimToUndefined(value: string | undefined): string | undefined {
  const trimmed = value?.trim();
  return trimmed ? trimmed : undefined;
}

function pathContains(parent: string, child: string, platform: NodeJS.Platform): boolean {
  const normalizedParent = normalizePath(parent, platform);
  const normalizedChild = normalizePath(child, platform);
  const path = relative(normalizedParent, normalizedChild);
  return path === "" || (!path.startsWith("..") && !isAbsolute(path));
}

function samePath(left: string, right: string, platform: NodeJS.Platform): boolean {
  return normalizePath(left, platform) === normalizePath(right, platform);
}

function normalizePath(value: string, platform: NodeJS.Platform): string {
  const normalized = resolve(value);
  return platform === "win32" ? normalized.toLowerCase() : normalized;
}

function runtimeLabel(record: RuntimeRecord): string {
  return record.name ? `${record.name} (${record.projectRoot})` : record.projectRoot;
}

function compareRecords(left: RuntimeRecord, right: RuntimeRecord): number {
  return left.projectRoot.localeCompare(right.projectRoot) || left.runtimeId.localeCompare(right.runtimeId);
}

function stripBom(value: string): string {
  return value.charCodeAt(0) === 0xfeff ? value.slice(1) : value;
}

function isMissing(error: unknown): boolean {
  return isNodeError(error) && error.code === "ENOENT";
}

function isNodeError(error: unknown): error is NodeJS.ErrnoException {
  return error instanceof Error && "code" in error;
}
