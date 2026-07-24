import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import { protocolVersion } from "../src/generated/protocol-version.js";
import {
  normalizePath,
  stableProjectId,
  type ProjectRecord,
  type RuntimeRecord,
} from "../src/runtime-discovery.js";
import { DiscoveredRuntimeInvoker } from "../src/runtime.js";
import {
  RuntimeRpcError,
  type RpcInvoker,
  type RuntimeHealth,
  type RuntimeIdentity,
} from "../src/runtime-rpc.js";

interface Fixture {
  home: string;
  projectDirectory: string;
  runtimeDirectory: string;
}

async function fixture(): Promise<Fixture> {
  const home = await mkdtemp(resolve(tmpdir(), "loomle-client-runtime-"));
  const projectDirectory = resolve(home, ".loomle", "state", "projects");
  const runtimeDirectory = resolve(home, ".loomle", "state", "runtimes");
  await mkdir(projectDirectory, { recursive: true });
  await mkdir(runtimeDirectory, { recursive: true });
  return { home, projectDirectory, runtimeDirectory };
}

async function addProject(fixture: Fixture, id: string, root = `/Projects/${id}`): Promise<void> {
  await writeFile(resolve(fixture.projectDirectory, `${id}.json`), JSON.stringify({
    projectId: idFor(id, root),
    name: id,
    projectRoot: root,
    uproject: `${root}/${id}.uproject`,
  }));
}

async function setRuntimes(fixture: Fixture, runtimes: readonly RuntimeRecord[]): Promise<void> {
  await rm(fixture.runtimeDirectory, { recursive: true, force: true });
  await mkdir(fixture.runtimeDirectory, { recursive: true });
  await Promise.all(runtimes.map((runtime) => writeFile(
    resolve(fixture.runtimeDirectory, `${runtime.runtimeId}.json`),
    JSON.stringify(runtime),
  )));
}

function runtime(projectId: string, runtimeId = `${projectId}-runtime`): RuntimeRecord {
  const projectRoot = `/Projects/${projectId}`;
  return {
    runtimeId,
    projectId: idFor(projectId, projectRoot),
    name: projectId,
    projectRoot,
    endpoint: `${runtimeId}-endpoint`,
    protocolVersion,
  };
}

function idFor(name: string, root = `/Projects/${name}`): string {
  return stableProjectId(root);
}

function readyHealth(record: RuntimeRecord, age = 1): RuntimeHealth {
  return {
    runtimeId: record.runtimeId,
    projectId: record.projectId,
    projectRoot: record.projectRoot,
    protocolVersion,
    lifecycle: "ready",
    listenerState: "listening",
    gameThreadProgressSequence: 10,
    gameThreadProgressAgeMs: age,
  };
}

class MockRuntimeClient implements RpcInvoker {
  closeCount = 0;
  healthCalls = 0;
  invokeCalls: string[] = [];
  healthResult: RuntimeHealth | RuntimeRpcError;
  invokeResult: unknown;

  constructor(
    readonly endpoint: string,
    record: RuntimeRecord,
    options: { health?: RuntimeHealth | RuntimeRpcError; invoke?: unknown } = {},
  ) {
    this.healthResult = options.health ?? readyHealth(record);
    this.invokeResult = options.invoke ?? { endpoint };
  }

  async health(_expected: RuntimeIdentity): Promise<RuntimeHealth> {
    this.healthCalls += 1;
    if (this.healthResult instanceof RuntimeRpcError) throw this.healthResult;
    return this.healthResult;
  }

  async requireTools(): Promise<void> {}

  async invoke(tool: string): Promise<unknown> {
    this.invokeCalls.push(tool);
    if (this.invokeResult instanceof Error) throw this.invokeResult;
    return this.invokeResult;
  }

  close(): void {
    this.closeCount += 1;
  }
}

function manager(
  home: string,
  clients: ReadonlyMap<string, MockRuntimeClient>,
  cwd = "/Elsewhere",
): DiscoveredRuntimeInvoker {
  return new DiscoveredRuntimeInvoker({
    homeDirectory: home,
    cwd,
    env: {},
    endpointAvailable: async () => true,
  }, (endpoint) => {
    const client = clients.get(endpoint);
    assert.ok(client, `unexpected endpoint ${endpoint}`);
    return client;
  });
}

test("project inspects online and offline projects and binds one sticky session", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const alphaRuntime = runtime("Alpha");
  await setRuntimes(state, [alphaRuntime]);
  const alphaClient = new MockRuntimeClient(alphaRuntime.endpoint, alphaRuntime);
  const invoker = manager(state.home, new Map([[alphaRuntime.endpoint, alphaClient]]));

  const initial = await invoker.project();
  assert.equal(initial.boundProjectId, undefined);
  assert.deepEqual(initial.projects.map(({ projectId, status }) => ({ projectId, status })), [
    { projectId: idFor("Alpha"), status: "ready" },
    { projectId: idFor("Beta"), status: "offline" },
  ]);

  const bound = await invoker.project({ projectId: idFor("Beta") });
  assert.equal(bound.boundProjectId, idFor("Beta"));
  assert.equal(bound.projects.find((project) => project.projectId === idFor("Beta"))?.bound, true);
  invoker.close();
});

test("session status reports only the bound project and its native Bridge facts", async () => {
  const state = await fixture();
  const record = {
    ...runtime("Alpha"),
    pluginPath: "/UE/Engine/Plugins/Marketplace/LoomleBridge",
    pluginInstallScope: "engine",
    pluginManagedBy: "fab",
    pluginVersion: "0.7.0-rc.1",
  };
  await addProject(state, "Alpha");
  await setRuntimes(state, [record]);
  const client = new MockRuntimeClient(record.endpoint, record);
  const invoker = manager(state.home, new Map([[record.endpoint, client]]));

  await invoker.project({ projectId: idFor("Alpha") });
  assert.deepEqual(await invoker.sessionStatus(), {
    status: "ready",
    project: {
      projectId: idFor("Alpha"),
      name: "Alpha",
      projectRoot: "/Projects/Alpha",
    },
    bridge: {
      version: "0.7.0-rc.1",
      protocolVersion,
      pluginPath: "/UE/Engine/Plugins/Marketplace/LoomleBridge",
      installScope: "engine",
      managedBy: "fab",
    },
  });
  invoker.close();
});

test("session status distinguishes unbound and bound-offline sessions", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const invoker = manager(state.home, new Map());

  assert.deepEqual(await invoker.sessionStatus(), { status: "unbound" });
  await invoker.project({ projectId: idFor("Alpha") });
  assert.deepEqual(await invoker.sessionStatus(), {
    status: "offline",
    project: {
      projectId: idFor("Alpha"),
      name: "Alpha",
      projectRoot: "/Projects/Alpha",
    },
    reason: "project.offline",
  });
  invoker.close();
});

test("an invalid selector leaves the previous project binding unchanged", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const invoker = manager(state.home, new Map());
  await invoker.project({ projectId: idFor("Alpha") });

  await assert.rejects(
    invoker.project({ projectId: "Missing" }),
    (error: unknown) => error instanceof RuntimeRpcError && error.code === "project.not_found",
  );
  assert.equal((await invoker.project()).boundProjectId, idFor("Alpha"));
  invoker.close();
});

test("an offline legacy root binding reconnects to the new canonical runtime identity", async () => {
  const state = await fixture();
  const parent = await mkdtemp(resolve(tmpdir(), "loomle-legacy-project-"));
  const root = resolve(parent, "Game");
  await mkdir(root);
  await writeFile(resolve(root, "Game.uproject"), "{}");
  const currentProjectId = stableProjectId(root, "darwin");
  const legacyProjectId = stableProjectId(root.toLowerCase(), "darwin");
  await writeFile(resolve(state.projectDirectory, `${legacyProjectId}.json`), JSON.stringify({
    projectId: legacyProjectId,
    name: "Game",
    projectRoot: root,
    uproject: resolve(root, "Game.uproject"),
  }));
  const record: RuntimeRecord = {
    runtimeId: "new-runtime",
    projectId: currentProjectId,
    name: "Game",
    projectRoot: root,
    endpoint: "new-runtime-endpoint",
    protocolVersion,
  };
  const client = new MockRuntimeClient(record.endpoint, record);
  const discoveredProject: ProjectRecord = {
    projectId: currentProjectId,
    name: "Game",
    projectRoot: root,
    uproject: resolve(root, "Game.uproject"),
  };
  const invoker = new DiscoveredRuntimeInvoker({
    homeDirectory: state.home,
    cwd: "/Elsewhere",
    env: {},
    platform: "darwin",
    endpointAvailable: async () => true,
  }, () => client, async (candidateRoot) => (
    candidateRoot === root ? discoveredProject : undefined
  ));

  const offline = await invoker.project({ projectRoot: root });
  assert.equal(offline.boundProjectId, currentProjectId);
  await setRuntimes(state, [record]);
  assert.deepEqual(await invoker.invoke("sal.query", {}), { endpoint: record.endpoint });
  invoker.close();
});

test("an offline bound project never falls through to another online project", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const betaRuntime = runtime("Beta");
  await setRuntimes(state, [betaRuntime]);
  const betaClient = new MockRuntimeClient(betaRuntime.endpoint, betaRuntime);
  const invoker = manager(state.home, new Map([[betaRuntime.endpoint, betaClient]]));
  await invoker.project({ projectId: idFor("Alpha") });
  betaClient.healthCalls = 0;

  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "project.offline"
      && error.retryable,
  );
  assert.equal(betaClient.healthCalls, 0);
  assert.deepEqual(betaClient.invokeCalls, []);
  invoker.close();
});

test("Editor restart preserves project intent and adopts its new sole runtime", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const firstRuntime = runtime("Alpha", "first");
  const secondRuntime = runtime("Alpha", "second");
  await setRuntimes(state, [firstRuntime]);
  const first = new MockRuntimeClient(firstRuntime.endpoint, firstRuntime);
  const second = new MockRuntimeClient(secondRuntime.endpoint, secondRuntime);
  const invoker = manager(state.home, new Map([
    [firstRuntime.endpoint, first],
    [secondRuntime.endpoint, second],
  ]), "/Projects/Alpha/Source");

  assert.deepEqual(await invoker.invoke("sal.query", {}), { endpoint: firstRuntime.endpoint });
  await setRuntimes(state, [secondRuntime]);
  assert.deepEqual(await invoker.invoke("sal.query", {}), { endpoint: secondRuntime.endpoint });
  assert.deepEqual(first.invokeCalls, ["sal.query"]);
  assert.deepEqual(second.invokeCalls, ["sal.query"]);
  invoker.close();
});

test("two healthy Editors for one project require selection unless affinity is still healthy", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const firstRuntime = runtime("Alpha", "first");
  const secondRuntime = runtime("Alpha", "second");
  await setRuntimes(state, [firstRuntime, secondRuntime]);
  const first = new MockRuntimeClient(firstRuntime.endpoint, firstRuntime);
  const second = new MockRuntimeClient(secondRuntime.endpoint, secondRuntime);
  const invoker = manager(state.home, new Map([
    [firstRuntime.endpoint, first],
    [secondRuntime.endpoint, second],
  ]), "/Projects/Alpha");

  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "project.multiple_editors",
  );
  invoker.close();

  await setRuntimes(state, [firstRuntime]);
  const affinityManager = manager(state.home, new Map([
    [firstRuntime.endpoint, first],
    [secondRuntime.endpoint, second],
  ]), "/Projects/Alpha");
  await affinityManager.invoke("sal.query", {});
  await setRuntimes(state, [firstRuntime, secondRuntime]);
  assert.deepEqual(await affinityManager.invoke("sal.query", {}), { endpoint: firstRuntime.endpoint });
  affinityManager.close();
});

test("an unresponsive affined Editor never falls through to another ready Editor", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const firstRuntime = runtime("Alpha", "first");
  const secondRuntime = runtime("Alpha", "second");
  await setRuntimes(state, [firstRuntime]);
  const first = new MockRuntimeClient(firstRuntime.endpoint, firstRuntime);
  const second = new MockRuntimeClient(secondRuntime.endpoint, secondRuntime);
  const invoker = manager(state.home, new Map([
    [firstRuntime.endpoint, first],
    [secondRuntime.endpoint, second],
  ]), "/Projects/Alpha");

  await invoker.invoke("sal.query", {});
  first.healthResult = readyHealth(firstRuntime, 2_001);
  await setRuntimes(state, [firstRuntime, secondRuntime]);
  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.editor_unresponsive",
  );
  assert.deepEqual(first.invokeCalls, ["sal.query"]);
  assert.deepEqual(second.invokeCalls, []);
  invoker.close();
});

test("without affinity any possibly live Editor conflicts with another ready Editor", async () => {
  for (const stateKind of ["stale", "starting", "incompatible"] as const) {
    const state = await fixture();
    await addProject(state, "Alpha");
    const firstRuntime = runtime("Alpha", `${stateKind}-first`);
    const secondRuntime = runtime("Alpha", `${stateKind}-second`);
    await setRuntimes(state, [firstRuntime, secondRuntime]);
    const firstHealth = stateKind === "stale"
      ? readyHealth(firstRuntime, 2_001)
      : stateKind === "starting"
        ? { ...readyHealth(firstRuntime), lifecycle: "starting" as const }
        : new RuntimeRpcError("runtime.incompatible", "old Bridge protocol");
    const first = new MockRuntimeClient(firstRuntime.endpoint, firstRuntime, { health: firstHealth });
    const second = new MockRuntimeClient(secondRuntime.endpoint, secondRuntime);
    const invoker = manager(state.home, new Map([
      [firstRuntime.endpoint, first],
      [secondRuntime.endpoint, second],
    ]), "/Projects/Alpha");

    await assert.rejects(
      invoker.invoke("sal.query", {}),
      (error: unknown) => error instanceof RuntimeRpcError
        && error.code === "project.multiple_editors",
    );
    assert.deepEqual(second.invokeCalls, []);
    invoker.close();
  }
});

test("a confirmed missing runtime does not block the sole ready Editor", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const missingRuntime = runtime("Alpha", "missing");
  const readyRuntime = runtime("Alpha", "ready");
  await setRuntimes(state, [missingRuntime, readyRuntime]);
  const missing = new MockRuntimeClient(missingRuntime.endpoint, missingRuntime, {
    health: new RuntimeRpcError("runtime.connect_failed", "endpoint is gone"),
  });
  const ready = new MockRuntimeClient(readyRuntime.endpoint, readyRuntime);
  const invoker = manager(state.home, new Map([
    [missingRuntime.endpoint, missing],
    [readyRuntime.endpoint, ready],
  ]), "/Projects/Alpha");

  assert.deepEqual(await invoker.invoke("sal.query", {}), { endpoint: readyRuntime.endpoint });
  assert.deepEqual(ready.invokeCalls, ["sal.query"]);
  invoker.close();
});

test("unbound project inspection does not create hidden runtime affinity", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const firstRuntime = runtime("Alpha", "first");
  const secondRuntime = runtime("Alpha", "second");
  await setRuntimes(state, [firstRuntime]);
  const first = new MockRuntimeClient(firstRuntime.endpoint, firstRuntime);
  const second = new MockRuntimeClient(secondRuntime.endpoint, secondRuntime);
  const invoker = manager(state.home, new Map([
    [firstRuntime.endpoint, first],
    [secondRuntime.endpoint, second],
  ]));

  assert.equal((await invoker.project()).boundProjectId, undefined);
  await setRuntimes(state, [firstRuntime, secondRuntime]);
  const bound = await invoker.project({ projectId: idFor("Alpha") });
  assert.equal(
    bound.projects.find((project) => project.projectId === idFor("Alpha"))?.status,
    "multiple_editors",
  );
  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "project.multiple_editors",
  );
  invoker.close();
});

test("a stale Game Thread heartbeat reports the project as unresponsive", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const record = runtime("Alpha");
  await setRuntimes(state, [record]);
  const client = new MockRuntimeClient(record.endpoint, record, {
    health: readyHealth(record, 2_001),
  });
  const invoker = manager(state.home, new Map([[record.endpoint, client]]), "/Projects/Alpha");

  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.editor_unresponsive"
      && error.retryable,
  );
  assert.deepEqual(client.invokeCalls, []);
  invoker.close();
});

test("non-empty MCP Roots constrain auto-binding instead of falling through globally", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const record = runtime("Alpha");
  await setRuntimes(state, [record]);
  const client = new MockRuntimeClient(record.endpoint, record);

  for (const roots of [["/Unrelated"], ["/Projects", "/AnotherProject"]]) {
    const invoker = manager(state.home, new Map([[record.endpoint, client]]));
    invoker.setMcpRoots(roots, true);
    await assert.rejects(
      invoker.invoke("sal.query", {}),
      (error: unknown) => error instanceof RuntimeRpcError
        && error.code === "project.selection_required",
    );
    invoker.close();
  }
});

test("pending or failed MCP Roots do not create a sticky global binding", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  const record = runtime("Alpha");
  await setRuntimes(state, [record]);
  const client = new MockRuntimeClient(record.endpoint, record);
  const invoker = manager(state.home, new Map([[record.endpoint, client]]));

  invoker.setMcpRoots(undefined, true);
  await assert.rejects(
    invoker.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "project.selection_required",
  );
  assert.equal((await invoker.project()).boundProjectId, undefined);
  invoker.close();
});

test("a Roots change invalidates an older in-flight auto-binding pass", async () => {
  const state = await fixture();
  const alphaRoot = normalizePath("/Projects/Alpha");
  let markDiscoveryStarted!: () => void;
  const discoveryStarted = new Promise<void>((resolve) => {
    markDiscoveryStarted = resolve;
  });
  let finishDiscovery!: (project: ProjectRecord | undefined) => void;
  const delayedDiscovery = new Promise<ProjectRecord | undefined>((resolve) => {
    finishDiscovery = resolve;
  });
  const invoker = new DiscoveredRuntimeInvoker({
    homeDirectory: state.home,
    cwd: "/Elsewhere",
    env: {},
    endpointAvailable: async () => true,
  }, () => assert.fail("no runtime should be selected"), async (root) => {
    if (root !== alphaRoot) return undefined;
    markDiscoveryStarted();
    return delayedDiscovery;
  });
  invoker.setMcpRoots([alphaRoot], true);

  const oldPass = invoker.project();
  await discoveryStarted;
  invoker.setMcpRoots(undefined, true);
  finishDiscovery({
    projectId: "Alpha",
    name: "Alpha",
    projectRoot: alphaRoot,
    uproject: `${alphaRoot}/Alpha.uproject`,
  });

  assert.equal((await oldPass).boundProjectId, undefined);
  assert.equal((await invoker.project()).boundProjectId, undefined);
  invoker.close();
});

test("MCP Roots auto-bind when they identify exactly one project", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const record = runtime("Alpha");
  await setRuntimes(state, [record]);
  const client = new MockRuntimeClient(record.endpoint, record);
  const invoker = manager(state.home, new Map([[record.endpoint, client]]));
  invoker.setMcpRoots(["/Projects/Alpha/Source"], true);

  assert.deepEqual(await invoker.invoke("sal.query", {}), { endpoint: record.endpoint });
  assert.equal((await invoker.project()).boundProjectId, idFor("Alpha"));
  invoker.close();
});

test("an exact MCP Root can bind a valid project before it has a registration", async () => {
  const state = await fixture();
  const root = await mkdtemp(resolve(tmpdir(), "loomle-root-project-"));
  await writeFile(resolve(root, "Fresh.uproject"), "{}");
  const invoker = manager(state.home, new Map());
  invoker.setMcpRoots([root], true);

  const report = await invoker.project();
  assert.notEqual(report.boundProjectId, undefined);
  assert.equal(report.projects[0].name, "Fresh");
  assert.equal(report.projects[0].status, "offline");
  invoker.close();
});

test("concurrent explicit bindings return their own atomic session snapshots", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const invoker = manager(state.home, new Map());

  const [alpha, beta] = await Promise.all([
    invoker.project({ projectId: idFor("Alpha") }),
    invoker.project({ projectId: idFor("Beta") }),
  ]);
  assert.equal(alpha.boundProjectId, idFor("Alpha"));
  assert.equal(beta.boundProjectId, idFor("Beta"));
  assert.equal((await invoker.project()).boundProjectId, idFor("Beta"));
  invoker.close();
});

test("switching projects does not close or retarget an in-flight request", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const alphaRuntime = runtime("Alpha");
  const betaRuntime = runtime("Beta");
  await setRuntimes(state, [alphaRuntime, betaRuntime]);
  let finishAlpha!: (value: unknown) => void;
  const alphaResult = new Promise((resolve) => {
    finishAlpha = resolve;
  });
  const alphaClient = new MockRuntimeClient(alphaRuntime.endpoint, alphaRuntime, {
    invoke: alphaResult,
  });
  const betaClient = new MockRuntimeClient(betaRuntime.endpoint, betaRuntime);
  const invoker = manager(state.home, new Map([
    [alphaRuntime.endpoint, alphaClient],
    [betaRuntime.endpoint, betaClient],
  ]));
  await invoker.project({ projectId: idFor("Alpha") });

  const first = invoker.invoke("alpha.read", {});
  await waitFor(() => alphaClient.invokeCalls.length === 1);
  await invoker.project({ projectId: idFor("Beta") });
  assert.deepEqual(await invoker.invoke("beta.read", {}), { endpoint: betaRuntime.endpoint });
  assert.equal(alphaClient.closeCount, 0);
  finishAlpha({ project: "Alpha" });
  assert.deepEqual(await first, { project: "Alpha" });
  assert.deepEqual(alphaClient.invokeCalls, ["alpha.read"]);
  assert.deepEqual(betaClient.invokeCalls, ["beta.read"]);
  invoker.close();
});

test("different Client sessions bind different projects without shared mutable state", async () => {
  const state = await fixture();
  await addProject(state, "Alpha");
  await addProject(state, "Beta");
  const alphaRuntime = runtime("Alpha");
  const betaRuntime = runtime("Beta");
  await setRuntimes(state, [alphaRuntime, betaRuntime]);
  const clients = new Map([
    [alphaRuntime.endpoint, new MockRuntimeClient(alphaRuntime.endpoint, alphaRuntime)],
    [betaRuntime.endpoint, new MockRuntimeClient(betaRuntime.endpoint, betaRuntime)],
  ]);
  const alpha = manager(state.home, clients);
  const beta = manager(state.home, clients);
  await alpha.project({ projectId: idFor("Alpha") });
  await beta.project({ projectId: idFor("Beta") });

  assert.deepEqual(await alpha.invoke("sal.query", {}), { endpoint: alphaRuntime.endpoint });
  assert.deepEqual(await beta.invoke("sal.query", {}), { endpoint: betaRuntime.endpoint });
  assert.equal((await alpha.project()).boundProjectId, idFor("Alpha"));
  assert.equal((await beta.project()).boundProjectId, idFor("Beta"));
  alpha.close();
  beta.close();
});

async function waitFor(predicate: () => boolean): Promise<void> {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (predicate()) return;
    await new Promise<void>((resolve) => setTimeout(resolve, 1));
  }
  assert.fail("Timed out waiting for asynchronous Client activity.");
}
