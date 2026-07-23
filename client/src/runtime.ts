import {
  defaultEndpointAvailable,
  discoverProjectAtRoot,
  discoverProjects,
  normalizePath,
  projectForCwd,
  projectsForRoots,
  samePath,
  type DiscoveredProject,
  type ProjectRecord,
  type RuntimeDiscoveryOptions,
  type RuntimeRecord,
} from "./runtime-discovery.js";
import {
  RuntimeRpcClient,
  RuntimeRpcError,
  type RpcInvoker,
  type RuntimeHealth,
  type RuntimeIdentity,
} from "./runtime-rpc.js";

const requiredTools = ["sal.query", "sal.patch", "editor.context"] as const;

export type ProjectStatus =
  | "ready"
  | "offline"
  | "starting"
  | "unresponsive"
  | "incompatible"
  | "multiple_editors";

export interface ProjectSelector {
  projectId?: string;
  projectRoot?: string;
}

export interface ProjectSummary {
  projectId: string;
  name: string;
  projectRoot: string;
  bound: boolean;
  status: ProjectStatus;
}

export interface ProjectReport {
  boundProjectId?: string;
  projects: readonly ProjectSummary[];
}

export interface ProjectController {
  project(selector?: ProjectSelector): Promise<ProjectReport>;
  setMcpRoots(roots: readonly string[] | undefined, supported: boolean): void;
}

interface RuntimeClient extends RpcInvoker {
  readonly endpoint: string;
  health(expected: RuntimeIdentity): Promise<RuntimeHealth>;
  requireTools(required: readonly string[]): Promise<void>;
  close(): void;
}

interface CachedClient {
  runtimeId: string;
  projectId: string;
  client: RuntimeClient;
}

interface RuntimeProbe {
  record: RuntimeRecord;
  client: RuntimeClient;
  health?: RuntimeHealth;
  error?: RuntimeRpcError;
}

interface ProjectProbe {
  summary: Omit<ProjectSummary, "bound">;
  selected?: RuntimeProbe;
  failureCode?: string;
  failureMessage?: string;
}

type RuntimeClientFactory = (endpoint: string) => RuntimeClient;
type ProjectRootDiscovery = (
  projectRoot: string,
  platform?: NodeJS.Platform,
) => Promise<ProjectRecord | undefined>;

export class DiscoveredRuntimeInvoker implements RpcInvoker, ProjectController {
  private binding?: ProjectRecord;
  private mcpRoots?: readonly string[];
  private mcpRootsSupported = false;
  private mcpRootsGeneration = 0;
  private autoBinding?: Promise<void>;
  private bindingQueue: Promise<void> = Promise.resolve();
  private readonly affinity = new Map<string, string>();
  private readonly clients = new Map<string, CachedClient>();

  constructor(
    private readonly discovery: RuntimeDiscoveryOptions = {},
    private readonly createClient: RuntimeClientFactory = (endpoint) => new RuntimeRpcClient(endpoint),
    private readonly discoverAtRoot: ProjectRootDiscovery = discoverProjectAtRoot,
  ) {}

  setMcpRoots(roots: readonly string[] | undefined, supported: boolean): void {
    this.mcpRootsGeneration += 1;
    this.mcpRootsSupported = supported;
    this.mcpRoots = roots?.map((root) => normalizePath(root, this.platform));
    if (!this.binding) this.autoBinding = undefined;
  }

  async project(selector: ProjectSelector = {}): Promise<ProjectReport> {
    const projectId = trimmed(selector.projectId);
    const projectRoot = trimmed(selector.projectRoot);
    if (projectId && projectRoot) {
      throw new RuntimeRpcError(
        "tool.invalid_arguments",
        "project accepts either projectId or projectRoot, not both.",
      );
    }
    if (projectId || projectRoot) {
      return this.enqueueBinding(async () => {
        const projects = await this.knownProjects();
        const discoveredRoot = projectRoot
          ? await this.discoverAtRoot(projectRoot, this.platform)
          : undefined;
        let selected = projectId
          ? projects.find((project) => project.projectId === projectId)
          : discoveredRoot
            ? projects.find((project) => project.projectId === discoveredRoot.projectId)
              ?? { ...discoveredRoot, runtimes: [] }
            : undefined;
        if (!selected) {
          const selectorText = projectId ? `projectId ${projectId}` : `projectRoot ${projectRoot}`;
          throw new RuntimeRpcError(
            "project.not_found",
            `No valid Loomle project matches ${selectorText}. The previous session binding was not changed.`,
          );
        }
        this.binding = projectIdentity(selected);
        return this.inspectProjects();
      });
    } else {
      await this.ensureAutoBinding();
    }
    return this.inspectProjects();
  }

  async invoke(
    tool: string,
    args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<unknown> {
    if (signal?.aborted) {
      throw new RuntimeRpcError(
        "runtime.request_cancelled",
        "Loomle runtime request was cancelled before project resolution.",
      );
    }
    await this.ensureAutoBinding();
    const binding = this.binding ? { ...this.binding } : undefined;
    if (!binding) {
      const projects = await this.knownProjects();
      throw new RuntimeRpcError(
        "project.selection_required",
        "No Unreal project is bound to this Loomle session. Call project with no arguments to inspect candidates, then bind one with projectId or projectRoot.",
        false,
        projects.length > 0
          ? projects.map((project) => `${project.name} (${project.projectId})`).join(", ")
          : "No registered Loomle projects were found.",
      );
    }

    const projects = await this.knownProjects();
    const project = projects.find((candidate) => candidate.projectId === binding.projectId)
      ?? { ...binding, runtimes: [] };
    const probe = await this.probeProject(project, true);
    if (!probe.selected) throw projectProbeError(probe);
    const client = probe.selected.client;

    // Health and selection happen within the immutable binding snapshot above.
    // A concurrent project switch affects only later requests.
    try {
      await client.requireTools(requiredTools);
      return await client.invoke(tool, args, signal);
    } catch (error) {
      if (isFatalRuntimeFailure(error)) this.discardClient(probe.selected.record, client);
      throw error;
    }
  }

  close(): void {
    for (const { client } of this.clients.values()) client.close();
    this.clients.clear();
  }

  private get platform(): NodeJS.Platform {
    return this.discovery.platform ?? process.platform;
  }

  private async inspectProjects(): Promise<ProjectReport> {
    const binding = this.binding ? { ...this.binding } : undefined;
    const projects = await this.knownProjects();
    // Project inspection reports candidates but never chooses an Editor
    // instance. Runtime affinity begins only when a UE-backed call is about to
    // execute against the bound project.
    const probes = await Promise.all(projects.map((project) => this.probeProject(project, false)));
    return {
      ...(binding ? { boundProjectId: binding.projectId } : {}),
      projects: probes.map((probe) => ({
        ...probe.summary,
        bound: binding?.projectId === probe.summary.projectId,
      })),
    };
  }

  private async knownProjects(): Promise<DiscoveredProject[]> {
    const projects = await discoverProjects(this.discovery);
    if (this.binding && !projects.some((project) => project.projectId === this.binding!.projectId)) {
      projects.push({ ...this.binding, runtimes: [] });
      projects.sort((left, right) => left.projectRoot.localeCompare(right.projectRoot));
    }
    return projects;
  }

  private async ensureAutoBinding(): Promise<void> {
    if (this.binding) return;
    const current = this.autoBinding ?? this.autoBind();
    this.autoBinding = current;
    try {
      await current;
    } finally {
      if (this.autoBinding === current) this.autoBinding = undefined;
    }
  }

  private async autoBind(): Promise<void> {
    const projects = await this.knownProjects();
    const requestedRoot = trimmed((this.discovery.env ?? process.env).LOOMLE_PROJECT_ROOT);
    if (requestedRoot) {
      const registered = projects.find((project) =>
        samePath(project.projectRoot, requestedRoot, this.platform));
      const selected = registered ?? await this.discoverAtRoot(requestedRoot, this.platform);
      if (!selected) {
        throw new RuntimeRpcError(
          "project.not_found",
          `LOOMLE_PROJECT_ROOT does not identify a valid Unreal project: ${requestedRoot}.`,
        );
      }
      if (!this.binding) this.binding = projectIdentity(selected);
      return;
    }

    if (this.mcpRootsSupported) {
      // undefined means the host advertises Roots but its authoritative list
      // is still pending or unavailable. Do not guess from cwd or global state.
      if (this.mcpRoots === undefined) return;
      const rootsGeneration = this.mcpRootsGeneration;
      const roots = this.mcpRoots;
      if (roots.length > 0) {
        const matches = projectsForRoots(projects, roots, this.platform);
        const direct = (await Promise.all(roots.map((root) =>
          this.discoverAtRoot(root, this.platform))))
          .filter((project): project is ProjectRecord => project !== undefined);
        // Roots may change while local project files are being inspected. An
        // obsolete discovery pass must never create the sticky binding.
        if (rootsGeneration !== this.mcpRootsGeneration || !this.mcpRootsSupported) return;
        for (const project of direct) {
          if (!matches.some((match) => match.projectId === project.projectId)) {
            matches.push({ ...project, runtimes: [] });
          }
        }
        if (matches.length === 1 && !this.binding) this.binding = projectIdentity(matches[0]);
        // Non-empty Roots are an authoritative workspace constraint. No match
        // is not permission to bind an unrelated globally unique project.
        return;
      }
    } else {
      const match = projectForCwd(projects, this.discovery.cwd ?? process.cwd(), this.platform);
      if (match && !this.binding) {
        this.binding = projectIdentity(match);
        return;
      }
    }

    if (projects.length === 1 && !this.binding) this.binding = projectIdentity(projects[0]);
  }

  private async enqueueBinding<T>(action: () => Promise<T>): Promise<T> {
    const operation = this.bindingQueue.then(action);
    this.bindingQueue = operation.then(() => undefined, () => undefined);
    return operation;
  }

  private async probeProject(
    project: DiscoveredProject,
    establishAffinity: boolean,
  ): Promise<ProjectProbe> {
    const endpointAvailable = this.discovery.endpointAvailable
      ?? ((record: RuntimeRecord) => defaultEndpointAvailable(record, this.platform));
    const available = (await Promise.all(project.runtimes.map(async (record) => ({
      record,
      available: await endpointAvailable(record),
    })))).filter(({ available: online }) => online).map(({ record }) => record);
    if (available.length === 0) {
      this.affinity.delete(project.projectId);
      return projectProbe(project, "offline", "project.offline");
    }

    const probes = await Promise.all(available.map((record) => this.probeRuntime(record)));
    const ready = probes.filter(isReadyProbe);
    const affinity = this.affinity.get(project.projectId);
    if (affinity) {
      const affinityProbe = probes.find((probe) => probe.record.runtimeId === affinity);
      if (!affinityProbe || isConfirmedMissingProbe(affinityProbe)) {
        // A unique per-runtime endpoint that no longer exists cannot return.
        // Release affinity only after that concrete runtime has disappeared.
        this.affinity.delete(project.projectId);
      } else if (isReadyProbe(affinityProbe)) {
        return selectedProjectProbe(project, affinityProbe);
      } else {
        // Never jump to another Editor merely because the affined Editor is
        // starting, busy, stale, shutting down, or incompatible.
        return classifyUnavailableProject(project, [affinityProbe]);
      }
    }

    const possibleEditors = probes.filter((probe) => !isConfirmedMissingProbe(probe));
    if (possibleEditors.length > 1) {
      return projectProbe(
        project,
        "multiple_editors",
        "project.multiple_editors",
        `Project ${project.name} has multiple live Editor runtimes. Close all but one Editor for this project.`,
      );
    }
    if (ready.length === 1) {
      if (establishAffinity) this.affinity.set(project.projectId, ready[0].record.runtimeId);
      return selectedProjectProbe(project, ready[0]);
    }
    return classifyUnavailableProject(project, probes);
  }

  private async probeRuntime(record: RuntimeRecord): Promise<RuntimeProbe> {
    const client = this.clientFor(record);
    try {
      return { record, client, health: await client.health(record) };
    } catch (error) {
      const runtimeError = error instanceof RuntimeRpcError
        ? error
        : new RuntimeRpcError("runtime.client_error", errorMessage(error));
      if (isFatalRuntimeFailure(runtimeError) || runtimeError.code === "runtime.request_timeout") {
        this.discardClient(record, client);
      }
      return { record, client, error: runtimeError };
    }
  }

  private clientFor(record: RuntimeRecord): RuntimeClient {
    const cached = this.clients.get(record.endpoint);
    if (cached && cached.runtimeId === record.runtimeId && cached.projectId === record.projectId) {
      return cached.client;
    }
    cached?.client.close();
    const client = this.createClient(record.endpoint);
    this.clients.set(record.endpoint, {
      runtimeId: record.runtimeId,
      projectId: record.projectId,
      client,
    });
    return client;
  }

  private discardClient(record: RuntimeRecord, client: RuntimeClient): void {
    const cached = this.clients.get(record.endpoint);
    if (cached?.client !== client) return;
    this.clients.delete(record.endpoint);
    client.close();
  }
}

function projectIdentity(project: ProjectRecord): ProjectRecord {
  return {
    projectId: project.projectId,
    name: project.name,
    projectRoot: project.projectRoot,
    ...(project.uproject ? { uproject: project.uproject } : {}),
  };
}

function projectProbe(
  project: ProjectRecord,
  status: ProjectStatus,
  failureCode?: string,
  failureMessage?: string,
): ProjectProbe {
  return {
    summary: {
      projectId: project.projectId,
      name: project.name,
      projectRoot: project.projectRoot,
      status,
    },
    ...(failureCode ? { failureCode } : {}),
    ...(failureMessage ? { failureMessage } : {}),
  };
}

function selectedProjectProbe(project: ProjectRecord, selected: RuntimeProbe): ProjectProbe {
  return {
    summary: {
      projectId: project.projectId,
      name: project.name,
      projectRoot: project.projectRoot,
      status: "ready",
    },
    selected,
  };
}

function isReadyProbe(probe: RuntimeProbe): boolean {
  return probe.health?.lifecycle === "ready"
    && probe.health.listenerState === "listening"
    && probe.health.gameThreadProgressAgeMs >= 0
    && probe.health.gameThreadProgressAgeMs <= 2_000;
}

function isConfirmedMissingProbe(probe: RuntimeProbe): boolean {
  return probe.error?.code === "runtime.connect_failed"
    || probe.error?.code === "runtime.identity_mismatch";
}

function classifyUnavailableProject(
  project: ProjectRecord,
  probes: readonly RuntimeProbe[],
): ProjectProbe {
  const health = probes.flatMap((probe) => probe.health ? [probe.health] : []);
  if (health.some((entry) => entry.lifecycle === "starting" || entry.listenerState === "starting")) {
    return projectProbe(project, "starting", "runtime.starting");
  }
  if (health.some((entry) => entry.lifecycle === "draining"
    || entry.listenerState === "stopping")) {
    return projectProbe(project, "offline", "runtime.editor_shutting_down");
  }
  if (probes.some((probe) => probe.error?.code === "runtime.request_timeout")) {
    return projectProbe(project, "unresponsive", "runtime.editor_unresponsive");
  }
  if (health.some((entry) => entry.lifecycle === "ready"
    && entry.listenerState === "listening"
    && (entry.gameThreadProgressAgeMs < 0 || entry.gameThreadProgressAgeMs > 2_000))) {
    return projectProbe(project, "unresponsive", "runtime.editor_unresponsive");
  }
  if (probes.some((probe) => isIncompatibleProbe(probe.error))) {
    const error = probes.find((probe) => isIncompatibleProbe(probe.error))?.error;
    return projectProbe(project, "incompatible", "runtime.incompatible", error?.message);
  }
  return projectProbe(project, "offline", "project.offline");
}

function projectProbeError(probe: ProjectProbe): RuntimeRpcError {
  const code = probe.failureCode ?? "project.offline";
  const name = probe.summary.name;
  const defaults: Record<string, string> = {
    "project.offline": `The bound project ${name} has no healthy Loomle Editor runtime. The session remains bound; start or restart Unreal Editor and retry.`,
    "project.multiple_editors": `The bound project ${name} has multiple live or unresolved Editor runtimes. Close all but one Editor for this project.`,
    "runtime.starting": `The Editor for ${name} is still starting. Wait for LoomleBridge to become ready and retry.`,
    "runtime.editor_unresponsive": `The Editor for ${name} did not answer Loomle's health probe. Wait for the Editor to become responsive and retry.`,
    "runtime.editor_shutting_down": `The Editor for ${name} is shutting down. Wait for it to close or restart before retrying.`,
    "runtime.incompatible": `The Editor for ${name} uses an incompatible LoomleBridge protocol.`,
  };
  return new RuntimeRpcError(
    code,
    probe.failureMessage ?? defaults[code] ?? `The bound project ${name} is unavailable.`,
    new Set(["project.offline", "runtime.starting", "runtime.editor_unresponsive", "runtime.editor_shutting_down"]).has(code),
  );
}

function isIncompatibleProbe(error: RuntimeRpcError | undefined): boolean {
  return error !== undefined && new Set([
    "runtime.incompatible",
    "runtime.identity_mismatch",
    "runtime.invalid_health",
  ]).has(error.code);
}

function isFatalRuntimeFailure(error: unknown): boolean {
  if (!(error instanceof RuntimeRpcError)) return true;
  return new Set<string>([
    "runtime.connect_failed",
    "runtime.connection_error",
    "runtime.connection_closed",
    "runtime.write_failed",
    "runtime.invalid_json",
    "runtime.invalid_response",
    "runtime.invalid_invoke_result",
    "runtime.request_timeout",
    "runtime.invalid_health",
    "runtime.identity_mismatch",
    "runtime.incompatible",
  ]).has(error.code);
}

function trimmed(value: string | undefined): string | undefined {
  const result = value?.trim();
  return result ? result : undefined;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
