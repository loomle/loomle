import { randomUUID } from "node:crypto";
import { createServer } from "node:net";
import {
  mkdir,
  rm,
  writeFile,
} from "node:fs/promises";
import {
  join,
  posix,
  win32,
} from "node:path";
import {
  PackagedSmokeAssertionError,
  buildAssetQuery,
  connectPackagedMcpClient,
  parseProjectReport,
  requireToolText,
  waitForProjectStatus,
} from "./packaged-mcp-smoke.mjs";

export const PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS = Object.freeze([
  "restart_adopts_new_runtime",
  "stale_runtime_record_is_ignored",
  "inflight_disconnect_is_bounded",
]);

const requiredBridgeTools = Object.freeze([
  "sal.query",
  "sal.patch",
  "editor.context",
]);
const scenarioTimeoutMs = 5_000;

/**
 * Exercises the packaged Client as a real child process against real local
 * sockets/named pipes. The fake runtime implements only the private transport
 * envelope; public tool parsing, project binding, discovery, affinity, failure
 * mapping, and reconnect behavior all run in the candidate executable.
 */
export async function runPackagedRuntimeLifecycle(options = {}) {
  const projectRoot = nonEmpty(options.projectRoot, "projectRoot");
  const platform = options.platform ?? process.platform;
  const cleanupTimeoutMs = positive(
    options.cleanupTimeoutMs ?? 15_000,
    "cleanupTimeoutMs",
  );
  const stateRoot = pathApi(platform).resolve(
    nonEmpty(options.stateRoot, "stateRoot"),
  );
  const projectId = stableProjectId(projectRoot, platform);
  const home = join(stateRoot, "home");
  const projectDirectory = join(home, ".loomle", "state", "projects");
  const runtimeDirectory = join(home, ".loomle", "state", "runtimes");
  await Promise.all([
    mkdir(projectDirectory, { recursive: true }),
    mkdir(runtimeDirectory, { recursive: true }),
  ]);
  await writeFile(
    join(projectDirectory, `${projectId}.json`),
    `${JSON.stringify({
      projectId,
      name: projectName(projectRoot),
      projectRoot,
      lastSeenAt: new Date().toISOString(),
    })}\n`,
  );

  const environment = lifecycleEnvironment(
    options.clientEnvironment ?? process.env,
    home,
    platform,
  );
  let session;
  const runtimes = [];
  const scenarios = [];
  const queryText = buildAssetQuery(options.fixture);
  let failure;
  const step = async (name, operation) => {
    const startedAt = Date.now();
    options.onScenario?.({ name, status: "started" });
    try {
      await operation();
      const result = {
        name,
        status: "passed",
        durationMs: Date.now() - startedAt,
      };
      scenarios.push(result);
      options.onScenario?.(result);
    } catch (error) {
      const result = {
        name,
        status: "failed",
        durationMs: Date.now() - startedAt,
        error: error instanceof Error ? error.message : String(error),
      };
      scenarios.push(result);
      options.onScenario?.(result);
      throw error;
    }
  };

  try {
    session = await connectPackagedMcpClient({
      executablePath: nonEmpty(options.clientExecutable, "clientExecutable"),
      workingDirectory: nonEmpty(
        options.clientWorkingDirectory,
        "clientWorkingDirectory",
      ),
      environment,
      arguments: options.clientArguments ?? ["mcp"],
      connectTimeoutMs: options.connectTimeoutMs,
      requestTimeoutMs: options.requestTimeoutMs,
      signal: options.signal,
      stderrSink: options.stderrSink,
    });
    const offline = boundProject(
      await projectCall(session, { projectRoot }),
      projectId,
    );
    assert(
      offline.status === "offline",
      `isolated lifecycle project began as ${offline.status}, expected offline`,
    );

    let first;
    let second;
    await step(PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS[0], async () => {
      first = await startRuntime({
        runtimeDirectory,
        projectId,
        projectRoot,
        platform,
        marker: "first-runtime",
      });
      runtimes.push(first);
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "ready",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });
      await assertRuntimeMarker(session, queryText, "first-runtime");

      await first.stop({ removeRecord: true });
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "offline",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });

      second = await startRuntime({
        runtimeDirectory,
        projectId,
        projectRoot,
        platform,
        marker: "second-runtime",
      });
      runtimes.push(second);
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "ready",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });
      await assertRuntimeMarker(session, queryText, "second-runtime");
      assert(
        first.runtimeId !== second.runtimeId,
        "restart reused a runtime identity",
      );
    });

    await step(PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS[1], async () => {
      await second.stop({ removeRecord: false });
      const startedAt = Date.now();
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "offline",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });
      const failure = await session.callTool("sal_query", { text: queryText });
      assert(
        failure?.isError === true,
        "a stale runtime record allowed a UE-backed query to succeed",
      );
      const detail = toolText(failure);
      assert(
        detail.includes("project.offline"),
        `stale runtime failure was not project.offline: ${detail}`,
      );
      assert(
        Date.now() - startedAt < scenarioTimeoutMs,
        "stale runtime detection exceeded its bounded deadline",
      );
    });

    await step(PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS[2], async () => {
      const blocked = await startRuntime({
        runtimeDirectory,
        projectId,
        projectRoot,
        platform,
        marker: "blocked-runtime",
        holdInvokes: true,
      });
      runtimes.push(blocked);
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "ready",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });

      const pending = session.callTool("sal_query", { text: queryText });
      await blocked.waitForInvoke(scenarioTimeoutMs);
      await blocked.stop({ removeRecord: false });
      const failed = await settleWithin(pending, scenarioTimeoutMs);
      assert(
        failed.settled,
        "an in-flight public query remained pending after runtime disconnect",
      );
      if (failed.error) throw failed.error;
      assert(
        failed.value?.isError === true,
        "an in-flight query reported success after its runtime disconnected",
      );
      assert(
        /runtime\.connection_(?:closed|error)|runtime\.connect_failed/.test(
          toolText(failed.value),
        ),
        `disconnect returned an unrelated failure: ${toolText(failed.value)}`,
      );

      const replacement = await startRuntime({
        runtimeDirectory,
        projectId,
        projectRoot,
        platform,
        marker: "replacement-runtime",
      });
      runtimes.push(replacement);
      await waitForProjectStatus(session, {
        projectRoot,
        expectedStatus: "ready",
        signal: options.signal,
        timeoutMs: scenarioTimeoutMs,
        pollIntervalMs: 25,
      });
      await assertRuntimeMarker(
        session,
        queryText,
        "replacement-runtime",
      );
      assert(
        blocked.invokeCount === 1,
        `the disconnected runtime observed ${blocked.invokeCount} invokes`,
      );
      assert(
        replacement.invokeCount === 1,
        `the replacement runtime observed ${replacement.invokeCount} invokes`,
      );
    });
  } catch (error) {
    failure = error;
  }

  const cleanupFailures = [];
  if (session) {
    await withTimeout(
      Promise.resolve().then(() => session.close()),
      cleanupTimeoutMs,
      "candidate Client cleanup exceeded its deadline",
    ).catch((error) => cleanupFailures.push(error));
  }
  await Promise.all(runtimes.map((runtime) => (
    runtime.stop({
      removeRecord: true,
      timeoutMs: cleanupTimeoutMs,
    }).catch((error) => cleanupFailures.push(error))
  )));
  await rm(stateRoot, { recursive: true, force: true })
    .catch((error) => cleanupFailures.push(error));

  if (failure && cleanupFailures.length > 0) {
    throw new AggregateError(
      [failure, ...cleanupFailures],
      "packaged runtime lifecycle and cleanup failed",
    );
  }
  if (failure) {
    throw failure;
  }
  if (cleanupFailures.length > 0) {
    throw new AggregateError(
      cleanupFailures,
      "packaged runtime lifecycle cleanup failed",
    );
  }

  return {
    status: "passed",
    projectId,
    scenarios,
  };
}

export function stableProjectId(
  projectRoot,
  platform = process.platform,
) {
  const normalized = normalizeProjectPath(projectRoot, platform);
  const bytes = Buffer.from(normalized, "utf8");
  let hash = 0xcbf29ce484222325n;
  const prime = 0x100000001b3n;
  const mask = 0xffffffffffffffffn;
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * prime) & mask;
  }
  return hash.toString(16).padStart(16, "0");
}

async function startRuntime({
  runtimeDirectory,
  projectId,
  projectRoot,
  platform,
  marker,
  holdInvokes = false,
}) {
  const runtimeId = randomUUID();
  const endpoint = platform === "win32"
    ? `\\\\.\\pipe\\loomle-lifecycle-${runtimeId}`
    // Darwin's per-user TMPDIR can make AF_UNIX paths exceed sun_path even
    // when the filename is short. /tmp is the stable, bounded transport root
    // used by the real Bridge as well.
    : `/tmp/lml-${runtimeId.replaceAll("-", "")}.sock`;
  if (platform !== "win32") {
    await rm(endpoint, { force: true });
  }

  const sockets = new Set();
  const invokeWaiters = [];
  let invokeCount = 0;
  let stopped = false;
  let closePromise;
  const server = createServer((socket) => {
    sockets.add(socket);
    socket.setEncoding("utf8");
    let buffer = "";
    socket.on("data", (chunk) => {
      buffer += chunk;
      while (true) {
        const newline = buffer.indexOf("\n");
        if (newline < 0) break;
        const line = buffer.slice(0, newline).trim();
        buffer = buffer.slice(newline + 1);
        if (!line) continue;
        handleRequest(socket, line);
      }
    });
    socket.on("close", () => sockets.delete(socket));
    socket.on("error", () => undefined);
  });
  server.on("error", () => undefined);

  const handleRequest = (socket, line) => {
    let request;
    try {
      request = JSON.parse(line);
    } catch {
      socket.destroy();
      return;
    }
    const protocolVersion = Number(request?.params?.protocolVersion ?? 2);
    if (request.method === "rpc.health") {
      respond(socket, request.id, {
        runtimeId,
        projectId,
        projectRoot,
        protocolVersion,
        lifecycle: "ready",
        listenerState: "listening",
        gameThreadProgressSequence: 1,
        gameThreadProgressAgeMs: 0,
      });
      return;
    }
    if (request.method === "rpc.capabilities") {
      respond(socket, request.id, {
        protocolVersion,
        tools: requiredBridgeTools,
      });
      return;
    }
    if (request.method === "rpc.cancel") {
      respond(socket, request.id, { cancelled: true });
      return;
    }
    if (request.method !== "rpc.invoke") {
      respondError(socket, request.id, "runtime.method_not_found");
      return;
    }

    invokeCount += 1;
    while (invokeWaiters.length > 0) invokeWaiters.shift()();
    if (holdInvokes) return;
    respond(socket, request.id, {
      ok: true,
      payload: {
        object: {
          statements: [{
            kind: "comment",
            text: `runtime: ${marker}`,
          }],
        },
        diagnostics: [],
      },
    });
  };

  await new Promise((resolveListen, rejectListen) => {
    const onError = (error) => {
      server.off("listening", onListening);
      rejectListen(error);
    };
    const onListening = () => {
      server.off("error", onError);
      resolveListen();
    };
    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(endpoint);
  });
  // Cleanup owns a bounded close below. An unexpected platform teardown must
  // not leave this test-double listener keeping the release runner alive.
  server.unref();

  const recordPath = join(runtimeDirectory, `${runtimeId}.json`);
  try {
    await writeFile(recordPath, `${JSON.stringify({
      runtimeId,
      projectId,
      name: projectName(projectRoot),
      projectRoot,
      endpoint,
      pid: process.pid,
      protocolVersion: 2,
      startedAt: new Date().toISOString(),
      lastSeenAt: new Date().toISOString(),
    })}\n`);
  } catch (error) {
    server.close();
    server.closeAllConnections?.();
    if (platform !== "win32") {
      await rm(endpoint, { force: true });
    }
    throw error;
  }

  return {
    runtimeId,
    get invokeCount() {
      return invokeCount;
    },
    async waitForInvoke(timeoutMs) {
      if (invokeCount > 0) return;
      await withTimeout(
        new Promise((resolveInvoke) => invokeWaiters.push(resolveInvoke)),
        timeoutMs,
        "fake runtime did not receive the in-flight invoke",
      );
    },
    async stop({
      removeRecord,
      timeoutMs = scenarioTimeoutMs,
    }) {
      if (!stopped) {
        stopped = true;
        closePromise = new Promise((resolveClose) => {
          server.close(resolveClose);
        });
        for (const socket of sockets) {
          socket.destroy();
          socket.unref();
        }
        server.closeAllConnections?.();
      }
      try {
        await withTimeout(
          closePromise ?? Promise.resolve(),
          timeoutMs,
          `fake runtime ${runtimeId} cleanup exceeded its deadline`,
        );
      } finally {
        if (platform !== "win32") {
          await rm(endpoint, { force: true });
        }
      }
      if (removeRecord) {
        await rm(recordPath, { force: true });
      }
    },
  };
}

function respond(socket, id, result) {
  socket.write(`${JSON.stringify({ jsonrpc: "2.0", id, result })}\n`);
}

function respondError(socket, id, code) {
  socket.write(`${JSON.stringify({
    jsonrpc: "2.0",
    id,
    error: {
      code: -32601,
      message: code,
      data: { code, retryable: false },
    },
  })}\n`);
}

async function projectCall(session, args) {
  return parseProjectReport(requireToolText(
    await session.callTool("project", args),
    "project",
  ));
}

function boundProject(report, projectId) {
  const project = report.projects.find(
    (candidate) => candidate.projectId === projectId
      && candidate.projectId === report.boundProjectId
      && candidate.bound === true,
  );
  assert(project, `project report omitted bound project ${projectId}`);
  return project;
}

async function assertRuntimeMarker(session, text, marker) {
  const result = await session.callTool("sal_query", { text });
  const output = requireToolText(result, "lifecycle sal_query");
  assert(
    output.includes(`runtime: ${marker}`),
    `query did not reach ${marker}: ${output}`,
  );
}

function toolText(result) {
  return Array.isArray(result?.content)
    ? result.content
      .filter((item) => item?.type === "text")
      .map((item) => item.text)
      .join("\n")
    : "";
}

function lifecycleEnvironment(environment, home, platform) {
  const result = {
    ...Object.fromEntries(
      Object.entries(environment).filter(([, value]) => (
        typeof value === "string"
      )),
    ),
    HOME: home,
    USERPROFILE: home,
    XDG_CONFIG_HOME: join(home, ".config"),
    XDG_CACHE_HOME: join(home, ".cache"),
    XDG_DATA_HOME: join(home, ".local", "share"),
    XDG_STATE_HOME: join(home, ".local", "state"),
    APPDATA: join(home, "AppData", "Roaming"),
    LOCALAPPDATA: join(home, "AppData", "Local"),
    LOOMLE_PROJECT_ROOT: "",
  };
  if (platform === "win32") {
    const drive = home.slice(0, 2);
    result.HOMEDRIVE = drive;
    result.HOMEPATH = home.slice(drive.length);
  }
  return result;
}

function normalizeProjectPath(projectRoot, platform) {
  const normalized = pathApi(platform).resolve(projectRoot).replaceAll("\\", "/");
  return platform === "win32" ? normalized.toLowerCase() : normalized;
}

function projectName(projectRoot) {
  return String(projectRoot).split(/[\\/]/).filter(Boolean).at(-1)
    ?? "LoomleLifecycle";
}

function pathApi(platform) {
  return platform === "win32" ? win32 : posix;
}

function assert(condition, message) {
  if (!condition) throw new PackagedSmokeAssertionError(message);
}

function nonEmpty(value, name) {
  if (typeof value !== "string" || value.trim().length === 0) {
    throw new TypeError(`${name} must be a non-empty string`);
  }
  return value;
}

function positive(value, name) {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) {
    throw new TypeError(`${name} must be positive`);
  }
  return value;
}

async function settleWithin(promise, timeoutMs) {
  let timer;
  try {
    return await Promise.race([
      Promise.resolve(promise).then(
        (value) => ({ settled: true, value }),
        (error) => ({ settled: true, error }),
      ),
      new Promise((resolveTimeout) => {
        timer = setTimeout(
          () => resolveTimeout({ settled: false }),
          timeoutMs,
        );
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

async function withTimeout(promise, timeoutMs, message) {
  const result = await settleWithin(promise, timeoutMs);
  if (!result.settled) throw new PackagedSmokeAssertionError(message);
  if (result.error) throw result.error;
  return result.value;
}
