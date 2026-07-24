import { readFile, readdir, stat } from "node:fs/promises";
import { homedir } from "node:os";
import { basename, extname, posix, resolve, win32 } from "node:path";

export interface RuntimeRecord {
  runtimeId: string;
  projectId: string;
  name?: string;
  projectRoot: string;
  uproject?: string;
  endpoint: string;
  pid?: number;
  protocolVersion?: number;
  pluginPath?: string;
  pluginInstallScope?: string;
  pluginManagedBy?: string;
  pluginVersion?: string;
  startedAt?: string;
  lastSeenAt?: string;
}

export interface ProjectRecord {
  projectId: string;
  name: string;
  projectRoot: string;
  uproject?: string;
  pluginPath?: string;
  pluginInstallScope?: string;
  pluginManagedBy?: string;
  pluginVersion?: string;
  lastSeenAt?: string;
}

export interface DiscoveredProject extends ProjectRecord {
  runtimes: readonly RuntimeRecord[];
}

export interface RuntimeDiscoveryOptions {
  homeDirectory?: string;
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
  endpointAvailable?: (record: RuntimeRecord) => Promise<boolean>;
}

export async function loadRuntimeRecords(
  homeDirectory = homedir(),
  platform: NodeJS.Platform = process.platform,
): Promise<RuntimeRecord[]> {
  const records = await loadJsonDirectory(
    resolve(homeDirectory, ".loomle", "state", "runtimes"),
    (value, fallbackId) => parseRuntimeRecord(value, fallbackId, platform),
  );
  return records.sort(compareRuntimeRecords);
}

export async function loadProjectRecords(
  homeDirectory = homedir(),
  platform: NodeJS.Platform = process.platform,
): Promise<ProjectRecord[]> {
  const records = await loadJsonDirectory(
    resolve(homeDirectory, ".loomle", "state", "projects"),
    (value, fallbackId) => parseProjectRecord(value, fallbackId, platform),
  );
  return records.sort(compareProjectRecords);
}

export async function discoverProjects(
  options: RuntimeDiscoveryOptions = {},
): Promise<DiscoveredProject[]> {
  const [registered, runtimes] = await Promise.all([
    loadProjectRecords(options.homeDirectory, options.platform),
    loadRuntimeRecords(options.homeDirectory, options.platform),
  ]);
  const projects = new Map<string, ProjectRecord>();

  for (const project of registered) projects.set(project.projectId, project);
  for (const runtime of runtimes) {
    const existing = projects.get(runtime.projectId);
    if (existing) continue;
    projects.set(runtime.projectId, {
      projectId: runtime.projectId,
      name: runtime.name ?? projectName(runtime.uproject, runtime.projectRoot),
      projectRoot: runtime.projectRoot,
      ...(runtime.uproject ? { uproject: runtime.uproject } : {}),
      ...(runtime.pluginPath ? { pluginPath: runtime.pluginPath } : {}),
      ...(runtime.pluginInstallScope ? { pluginInstallScope: runtime.pluginInstallScope } : {}),
      ...(runtime.pluginManagedBy ? { pluginManagedBy: runtime.pluginManagedBy } : {}),
      ...(runtime.pluginVersion ? { pluginVersion: runtime.pluginVersion } : {}),
      ...(runtime.lastSeenAt ? { lastSeenAt: runtime.lastSeenAt } : {}),
    });
  }

  return [...projects.values()].map((project) => ({
    ...project,
    runtimes: runtimes.filter((runtime) => runtime.projectId === project.projectId),
  })).sort(compareProjectRecords);
}

export async function discoverProjectAtRoot(
  projectRoot: string,
  platform: NodeJS.Platform = process.platform,
): Promise<ProjectRecord | undefined> {
  const resolvedRoot = resolvePlatformPath(projectRoot, platform);
  let entries;
  try {
    entries = await readdir(resolvedRoot, { withFileTypes: true });
  } catch {
    return undefined;
  }
  const projectFiles = entries.filter((entry) =>
    entry.isFile() && extname(entry.name).toLowerCase() === ".uproject");
  if (projectFiles.length !== 1) return undefined;

  const uproject = resolvePlatformPath(
    platform === "win32"
      ? win32.join(resolvedRoot, projectFiles[0].name)
      : posix.join(resolvedRoot, projectFiles[0].name),
    platform,
  );
  return {
    projectId: stableProjectId(resolvedRoot, platform),
    name: basename(projectFiles[0].name, extname(projectFiles[0].name)),
    projectRoot: resolvedRoot,
    uproject,
  };
}

export function stableProjectId(
  projectRoot: string,
  platform: NodeJS.Platform = process.platform,
): string {
  // normalizePath folds case only on Windows, matching UE's platform path
  // identity. POSIX paths remain case-sensitive so distinct projects cannot
  // collapse into one binding.
  return fnvProjectId(normalizePath(projectRoot, platform));
}

function fnvProjectId(normalizedProjectRoot: string): string {
  const bytes = Buffer.from(normalizedProjectRoot, "utf8");
  let hash = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  const mask = 0xffffffffffffffffn;
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * prime) & mask;
  }
  return hash.toString(16).padStart(16, "0");
}

export function projectsForRoots(
  projects: readonly DiscoveredProject[],
  roots: readonly string[],
  platform: NodeJS.Platform = process.platform,
): DiscoveredProject[] {
  const matches = new Map<string, DiscoveredProject>();
  for (const root of roots) {
    const containing = projects
      .filter((project) => pathContains(project.projectRoot, root, platform))
      .sort((left, right) => normalizePath(right.projectRoot, platform).length
        - normalizePath(left.projectRoot, platform).length);
    if (containing.length > 0) {
      const length = normalizePath(containing[0].projectRoot, platform).length;
      for (const project of containing) {
        if (normalizePath(project.projectRoot, platform).length === length) {
          matches.set(project.projectId, project);
        }
      }
      continue;
    }
    for (const project of projects) {
      if (pathContains(root, project.projectRoot, platform)) {
        matches.set(project.projectId, project);
      }
    }
  }
  return [...matches.values()].sort(compareProjectRecords);
}

export function projectForCwd(
  projects: readonly DiscoveredProject[],
  cwd: string,
  platform: NodeJS.Platform = process.platform,
): DiscoveredProject | undefined {
  const matches = projects
    .filter((project) => pathContains(project.projectRoot, cwd, platform))
    .sort((left, right) => normalizePath(right.projectRoot, platform).length
      - normalizePath(left.projectRoot, platform).length);
  if (matches.length === 0) return undefined;
  const length = normalizePath(matches[0].projectRoot, platform).length;
  const mostSpecific = matches.filter((project) =>
    normalizePath(project.projectRoot, platform).length === length);
  return mostSpecific.length === 1 ? mostSpecific[0] : undefined;
}

export async function defaultEndpointAvailable(
  record: RuntimeRecord,
  platform: NodeJS.Platform = process.platform,
): Promise<boolean> {
  if (!isProcessAlive(record.pid)) return false;
  if (platform === "win32") {
    return record.endpoint.startsWith("\\\\.\\pipe\\") || record.endpoint.startsWith("//./pipe/");
  }
  try {
    return (await stat(record.endpoint)).isSocket();
  } catch {
    return false;
  }
}

export function samePath(
  left: string,
  right: string,
  platform: NodeJS.Platform = process.platform,
): boolean {
  return normalizePath(left, platform) === normalizePath(right, platform);
}

export function normalizePath(
  value: string,
  platform: NodeJS.Platform = process.platform,
): string {
  if (platform === "win32") {
    // UE's FPaths::NormalizeFilename uses forward slashes before hashing the
    // stable project identity. Match it even though Node uses backslashes.
    return resolvePlatformPath(value, platform).replaceAll("\\", "/").toLowerCase();
  }
  return resolvePlatformPath(value, platform);
}

function resolvePlatformPath(value: string, platform: NodeJS.Platform): string {
  return platform === "win32" ? win32.resolve(value) : posix.resolve(value);
}

async function loadJsonDirectory<T>(
  directory: string,
  parse: (value: unknown, fallbackId: string) => T | undefined,
): Promise<T[]> {
  let entries;
  try {
    entries = await readdir(directory, { withFileTypes: true });
  } catch (error) {
    if (isMissing(error)) return [];
    throw error;
  }
  const records: T[] = [];
  await Promise.all(entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
    .map(async (entry) => {
      try {
        const text = stripBom(await readFile(resolve(directory, entry.name), "utf8"));
        const record = parse(JSON.parse(text) as unknown, entry.name.slice(0, -5));
        if (record !== undefined) records.push(record);
      } catch {
        // A partially written or foreign record is not a discovery candidate.
      }
    }));
  return records;
}

function parseRuntimeRecord(
  value: unknown,
  fallbackId: string,
  platform: NodeJS.Platform,
): RuntimeRecord | undefined {
  if (!isRecord(value)) return undefined;
  const projectRoot = stringField(value, "projectRoot");
  const endpoint = stringField(value, "endpoint");
  if (!projectRoot || !endpoint) return undefined;
  const runtimeId = stringField(value, "runtimeId") ?? fallbackId;
  const storedProjectId = stringField(value, "projectId") ?? fallbackId;
  const projectId = validatedProjectId(storedProjectId, projectRoot, platform);
  if (!projectId) return undefined;
  return {
    runtimeId,
    projectId,
    ...(stringField(value, "name") ? { name: stringField(value, "name") } : {}),
    projectRoot,
    ...(stringField(value, "uproject") ? { uproject: stringField(value, "uproject") } : {}),
    endpoint,
    ...(numberField(value, "pid") !== undefined ? { pid: numberField(value, "pid") } : {}),
    ...(numberField(value, "protocolVersion") !== undefined
      ? { protocolVersion: numberField(value, "protocolVersion") } : {}),
    ...(stringField(value, "pluginPath") ? { pluginPath: stringField(value, "pluginPath") } : {}),
    ...(stringField(value, "pluginInstallScope")
      ? { pluginInstallScope: stringField(value, "pluginInstallScope") } : {}),
    ...(stringField(value, "pluginManagedBy")
      ? { pluginManagedBy: stringField(value, "pluginManagedBy") } : {}),
    ...(stringField(value, "pluginVersion")
      ? { pluginVersion: stringField(value, "pluginVersion") } : {}),
    ...(stringField(value, "startedAt") ? { startedAt: stringField(value, "startedAt") } : {}),
    ...(stringField(value, "lastSeenAt") ? { lastSeenAt: stringField(value, "lastSeenAt") } : {}),
  };
}

function parseProjectRecord(
  value: unknown,
  fallbackId: string,
  platform: NodeJS.Platform,
): ProjectRecord | undefined {
  if (!isRecord(value)) return undefined;
  const projectRoot = stringField(value, "projectRoot");
  if (!projectRoot) return undefined;
  const storedProjectId = stringField(value, "projectId") ?? fallbackId;
  const projectId = validatedProjectId(storedProjectId, projectRoot, platform);
  if (!projectId) return undefined;
  return {
    projectId,
    name: stringField(value, "name") ?? projectName(stringField(value, "uproject"), projectRoot),
    projectRoot,
    ...(stringField(value, "uproject") ? { uproject: stringField(value, "uproject") } : {}),
    ...(stringField(value, "pluginPath") ? { pluginPath: stringField(value, "pluginPath") } : {}),
    ...(stringField(value, "pluginInstallScope")
      ? { pluginInstallScope: stringField(value, "pluginInstallScope") } : {}),
    ...(stringField(value, "pluginManagedBy")
      ? { pluginManagedBy: stringField(value, "pluginManagedBy") } : {}),
    ...(stringField(value, "pluginVersion")
      ? { pluginVersion: stringField(value, "pluginVersion") } : {}),
    ...(stringField(value, "lastSeenAt") ? { lastSeenAt: stringField(value, "lastSeenAt") } : {}),
  };
}

function validatedProjectId(
  storedProjectId: string,
  projectRoot: string,
  platform: NodeJS.Platform,
): string | undefined {
  const normalizedRoot = normalizePath(projectRoot, platform);
  const canonicalProjectId = fnvProjectId(normalizedRoot);
  const legacyProjectId = fnvProjectId(normalizedRoot.toLowerCase());
  return storedProjectId === canonicalProjectId || storedProjectId === legacyProjectId
    ? canonicalProjectId
    : undefined;
}

function pathContains(parent: string, child: string, platform: NodeJS.Platform): boolean {
  const normalizedParent = normalizePath(parent, platform);
  const normalizedChild = normalizePath(child, platform);
  return normalizedChild === normalizedParent
    || normalizedChild.startsWith(normalizedParent.endsWith("/")
      ? normalizedParent
      : `${normalizedParent}/`);
}

function projectName(uproject: string | undefined, projectRoot: string): string {
  return uproject ? basename(uproject, extname(uproject)) : basename(projectRoot);
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

function compareProjectRecords(left: ProjectRecord, right: ProjectRecord): number {
  return left.projectRoot.localeCompare(right.projectRoot) || left.projectId.localeCompare(right.projectId);
}

function compareRuntimeRecords(left: RuntimeRecord, right: RuntimeRecord): number {
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
