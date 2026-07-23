import { constants as fsConstants } from "node:fs";
import { access } from "node:fs/promises";
import { normalize, resolve } from "node:path";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { parseSalObject } from "@loomle/sal";

export const PUBLIC_TOOL_NAMES = Object.freeze([
  "project",
  "sal_query",
  "sal_patch",
  "sal_schema",
  "editor_context",
]);

export const PACKAGED_SMOKE_STEP_NAMES = Object.freeze([
  "initialize_and_list_tools",
  "bind_offline_project",
  "read_schema_offline",
  "start_editor_and_wait_ready",
  "query_fixture_asset_and_blueprint",
  "blueprint_mutation_round_trip",
  "read_editor_context",
  "reconnect_client",
  "stop_editor_and_wait_offline",
]);

const schemaModules = [
  "asset",
  "blueprint",
  "class",
  "graph",
  "state_tree",
  "widget",
];
const defaults = {
  connectTimeoutMs: 15_000,
  requestTimeoutMs: 150_000,
  statusTimeoutMs: 180_000,
  pollIntervalMs: 500,
  cleanupTimeoutMs: 15_000,
};
const MAX_STDERR_TAIL_BYTES = 64 * 1024;

export class PackagedSmokeConfigurationError extends Error {
  constructor(message, options) {
    super(message, options);
    this.name = "PackagedSmokeConfigurationError";
  }
}

export class PackagedSmokeAssertionError extends Error {
  constructor(message, options) {
    super(message, options);
    this.name = "PackagedSmokeAssertionError";
  }
}

export async function connectPackagedMcpClient({
  executablePath,
  workingDirectory,
  environment = process.env,
  arguments: executableArguments = ["mcp"],
  connectTimeoutMs = defaults.connectTimeoutMs,
  requestTimeoutMs = defaults.requestTimeoutMs,
  signal,
  stderrSink,
} = {}) {
  nonEmpty(executablePath, "clientExecutable");
  nonEmpty(workingDirectory, "clientWorkingDirectory");
  try {
    await access(executablePath, fsConstants.X_OK);
  } catch (cause) {
    throw new PackagedSmokeConfigurationError(
      `candidate Client is missing or not executable: ${executablePath}`,
      { cause },
    );
  }

  let stderr = "";
  const transport = new StdioClientTransport({
    command: resolve(executablePath),
    args: [...executableArguments],
    cwd: resolve(workingDirectory),
    env: {
      ...stringEnvironment(environment),
      LOOMLE_PROJECT_ROOT: "",
    },
    stderr: "pipe",
  });
  transport.stderr?.on("data", (chunk) => {
    const text = chunk.toString();
    stderr = appendTextTail(stderr, text);
    stderrSink?.(text);
  });
  const client = new Client({ name: "loomle-packaged-e2e", version: "1.0.0" });

  try {
    await client.connect(transport, {
      timeout: connectTimeoutMs,
      signal,
    });
  } catch (cause) {
    await client.close().catch(() => undefined);
    const detail = stderr.trim() ? `\nClient stderr:\n${stderr.trim()}` : "";
    throw new PackagedSmokeAssertionError(
      `candidate Client failed MCP initialization${detail}`,
      { cause },
    );
  }

  const requestOptions = {
    timeout: requestTimeoutMs,
    maxTotalTimeout: requestTimeoutMs,
    signal,
  };
  return {
    serverVersion: client.getServerVersion(),
    async listTools() {
      return (await client.listTools(undefined, requestOptions)).tools;
    },
    async callTool(name, args, options = {}) {
      publicTool(name);
      return client.callTool(
        { name, arguments: args },
        undefined,
        {
          ...requestOptions,
          ...(options.signal ? { signal: options.signal } : {}),
        },
      );
    },
    close: () => client.close(),
    getStderr: () => stderr,
  };
}

export async function runPackagedMcpSmoke(options = {}) {
  const config = smokeConfig(options);
  const steps = [];
  const stderrs = [];
  let session;
  let editorRunning = false;
  let projectId;
  let blueprintId;
  let serverVersion;
  let failure;

  const connect = config.sessionFactory ?? (() => connectPackagedMcpClient({
    executablePath: config.clientExecutable,
    workingDirectory: config.clientWorkingDirectory,
    environment: config.clientEnvironment,
    arguments: config.clientArguments,
    connectTimeoutMs: config.connectTimeoutMs,
    requestTimeoutMs: config.requestTimeoutMs,
    signal: config.signal,
    stderrSink: config.stderrSink,
  }));
  const step = async (name, operation) => {
    throwIfAborted(config.signal);
    const startedAt = config.now();
    config.onStep?.({ name, status: "started" });
    try {
      const result = await operation();
      throwIfAborted(config.signal);
      const record = {
        name,
        status: "passed",
        durationMs: Math.max(0, config.now() - startedAt),
      };
      steps.push(record);
      config.onStep?.(record);
      return result;
    } catch (error) {
      const record = {
        name,
        status: "failed",
        durationMs: Math.max(0, config.now() - startedAt),
        error: message(error),
      };
      steps.push(record);
      config.onStep?.(record);
      throw error;
    }
  };

  try {
    await step(PACKAGED_SMOKE_STEP_NAMES[0], async () => {
      session = await connect();
      assertSession(session);
      serverVersion = assertServer(session.serverVersion, config.expectedServerVersion);
      await assertTools(session);
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[1], async () => {
      const project = boundProject(
        await projectCall(session, { projectRoot: config.projectRoot }),
        config.projectRoot,
        "offline",
      );
      projectId = project.projectId;
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[2], async () => {
      const schema = await textCall(session, "sal_schema", {}, "sal_schema");
      for (const name of schemaModules) {
        const word = new RegExp(`(^|[^A-Za-z0-9_])${name}([^A-Za-z0-9_]|$)`, "m");
        assert(word.test(schema), `sal_schema omitted required module ${name}`);
      }
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[3], async () => {
      await config.startEditor();
      editorRunning = true;
      const project = await waitForProjectStatus(session, {
        projectRoot: config.projectRoot,
        expectedStatus: "ready",
        signal: config.signal,
        ...config.poll,
      });
      sameIdentity(projectId, project.projectId, "offline -> ready");
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[4], async () => {
      await assertFixtureAsset(session, config.fixture);
      blueprintId = await fixtureBlueprintId(session, config.fixture);
      assertDescription(
        await blueprintDescription(session, config.fixture, blueprintId),
        config.fixture.blueprintDescription,
        "fixture baseline",
      );
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[5], () => mutationRoundTrip(
      session,
      config.fixture,
      blueprintId,
      config.mutationDescription,
      config.cleanupTimeoutMs,
    ));

    await step(PACKAGED_SMOKE_STEP_NAMES[6], async () => {
      const context = await textCall(
        session,
        "editor_context",
        {},
        "editor_context",
      );
      salObject(context, "editor_context");
      assert(
        /(^|\n)\s*(?:#\s*)?surface:/m.test(context)
          && /(^|\n)\s*(?:#\s*)?selection:/m.test(context),
        "editor_context did not report both surface and selection",
      );
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[7], async () => {
      stderrs.push(session.getStderr?.() ?? "");
      await session.close();
      session = undefined;
      session = await connect();
      assertSession(session);
      assertServer(session.serverVersion, config.expectedServerVersion);
      await assertTools(session);
      const project = boundProject(
        await projectCall(session, { projectRoot: config.projectRoot }),
        config.projectRoot,
        "ready",
      );
      sameIdentity(projectId, project.projectId, "Client reconnect");
    });

    await step(PACKAGED_SMOKE_STEP_NAMES[8], async () => {
      await config.stopEditor();
      editorRunning = false;
      const project = await waitForProjectStatus(session, {
        projectRoot: config.projectRoot,
        expectedStatus: "offline",
        signal: config.signal,
        ...config.poll,
      });
      sameIdentity(projectId, project.projectId, "ready -> offline");
    });
  } catch (error) {
    failure = error;
  }

  const cleanupFailures = [];
  if (session) {
    stderrs.push(session.getStderr?.() ?? "");
    await session.close().catch((error) => cleanupFailures.push(error));
  }
  if (editorRunning) {
    await config.stopEditor().catch((error) => cleanupFailures.push(error));
  }
  if (failure && cleanupFailures.length) {
    throw new AggregateError(
      [failure, ...cleanupFailures],
      `packaged MCP smoke and cleanup failed: ${message(failure)}`,
    );
  }
  if (failure) throw failure;
  if (cleanupFailures.length) {
    throw new AggregateError(cleanupFailures, "packaged MCP smoke cleanup failed");
  }

  return {
    status: "passed",
    serverVersion,
    projectId,
    blueprintId,
    steps,
    clientStderr: stderrs,
  };
}

export function normalizeFixtureLocator(fixture) {
  if (!fixture || typeof fixture !== "object" || Array.isArray(fixture)) {
    throw new PackagedSmokeConfigurationError(
      "fixture locator is required; packaged smoke never skips a missing fixture",
    );
  }
  const blueprintAssetPath = nonEmpty(
    fixture.blueprintAssetPath,
    "fixture.blueprintAssetPath",
  );
  if (!blueprintAssetPath.startsWith("/Game/") || !blueprintAssetPath.includes(".")) {
    throw new PackagedSmokeConfigurationError(
      "fixture.blueprintAssetPath must be an exact /Game package object path",
    );
  }
  return Object.freeze({
    blueprintAssetPath,
    blueprintDescription: nonEmpty(
      fixture.blueprintDescription,
      "fixture.blueprintDescription",
    ),
    assetType: fixture.assetType === undefined
      ? "/Script/Engine.Blueprint"
      : nonEmpty(fixture.assetType, "fixture.assetType"),
    assetSearchText: fixture.assetSearchText === undefined
      ? blueprintAssetPath.slice(blueprintAssetPath.lastIndexOf(".") + 1)
      : nonEmpty(fixture.assetSearchText, "fixture.assetSearchText"),
    assetRoot: fixture.assetRoot === undefined
      ? "/Game"
      : nonEmpty(fixture.assetRoot, "fixture.assetRoot"),
  });
}

export function buildAssetQuery(fixture) {
  const f = normalizeFixtureLocator(fixture);
  return [
    "query asset",
    `assets ${quoted(f.assetSearchText)}`,
    `where root = ${quoted(f.assetRoot)} and type = ${quoted(f.assetType)} and path = ${quoted(f.blueprintAssetPath)}`,
    "page limit 10",
  ].join("\n");
}

export function buildBlueprintSummaryQuery(fixture) {
  const f = normalizeFixtureLocator(fixture);
  return [
    `fixtureBlueprint = blueprint(asset: ${quoted(f.blueprintAssetPath)})`,
    "",
    "query fixtureBlueprint",
    "summary",
  ].join("\n");
}

export function buildBlueprintExactQuery(fixture, blueprintId) {
  const f = normalizeFixtureLocator(fixture);
  stableId(blueprintId, "blueprintId");
  return [
    "fixtureBlueprint = blueprint(",
    `  asset: ${quoted(f.blueprintAssetPath)},`,
    `  id: ${quoted(blueprintId)}`,
    ")",
    "",
    "query fixtureBlueprint",
    `blueprint@${blueprintId}`,
  ].join("\n");
}

export function buildBlueprintDescriptionPatch(
  fixture,
  blueprintId,
  description,
  { dryRun = false } = {},
) {
  const f = normalizeFixtureLocator(fixture);
  stableId(blueprintId, "blueprintId");
  nonEmpty(description, "BlueprintDescription");
  return [
    "fixtureBlueprint = blueprint(",
    `  asset: ${quoted(f.blueprintAssetPath)},`,
    `  id: ${quoted(blueprintId)}`,
    ")",
    "",
    `patch fixtureBlueprint${dryRun ? " dry run" : ""}`,
    `set fixtureBlueprint.BlueprintDescription = ${quoted(description)}`,
  ].join("\n");
}

export function parseProjectReport(text) {
  const lines = String(text).split(/\r?\n/);
  const match = /^bound:\s*(\S+)\s*$/.exec(lines[0] ?? "");
  assert(match, `project returned malformed report: ${text}`);
  const projects = [];
  let current;
  for (const line of lines.slice(1)) {
    const item = /^-\s+(.+?)\s*$/.exec(line);
    if (item) {
      current = { projectId: item[1] };
      projects.push(current);
      continue;
    }
    const field = /^\s{2}([A-Za-z][A-Za-z0-9]*):\s*(.*?)\s*$/.exec(line);
    if (field && current) {
      current[field[1]] = field[2] === "true"
        ? true
        : (field[2] === "false" ? false : field[2]);
    }
  }
  return {
    boundProjectId: match[1] === "none" ? undefined : match[1],
    projects,
  };
}

export function requireToolText(result, label) {
  const blocks = Array.isArray(result?.content)
    ? result.content.filter((item) => item?.type === "text")
    : [];
  const detail = blocks.map((item) => item.text).join("\n");
  assert(result?.isError !== true, `${label} failed${detail ? `: ${detail}` : ""}`);
  assert(
    blocks.length === 1 && typeof blocks[0].text === "string",
    `${label} must return exactly one MCP text block`,
  );
  return blocks[0].text;
}

export async function waitForProjectStatus(session, {
  projectRoot,
  expectedStatus,
  timeoutMs = defaults.statusTimeoutMs,
  pollIntervalMs = defaults.pollIntervalMs,
  now = Date.now,
  sleep = delay,
  signal,
}) {
  const deadline = now() + timeoutMs;
  let report;
  do {
    throwIfAborted(signal);
    report = await projectCall(session, {});
    const project = report.projects.find(
      (item) => item.projectId === report.boundProjectId
        && item.bound === true
        && samePath(item.projectRoot, projectRoot),
    );
    if (project?.status === expectedStatus) return project;
    if (now() >= deadline) break;
    await sleep(pollIntervalMs);
    throwIfAborted(signal);
  } while (true);
  const lastStatus = report?.projects.find(
    (item) => item.projectId === report.boundProjectId,
  )?.status ?? "missing";
  throw new PackagedSmokeAssertionError(
    `timed out waiting for bound project ${projectRoot} to become ${expectedStatus}; last status: ${lastStatus}`,
  );
}

async function assertTools(session) {
  const tools = await session.listTools();
  const names = Array.isArray(tools) ? tools.map((tool) => tool?.name) : tools;
  assert(
    JSON.stringify(names) === JSON.stringify(PUBLIC_TOOL_NAMES),
    `unexpected public tool inventory: ${JSON.stringify(names)}`,
  );
}

async function assertFixtureAsset(session, fixture) {
  let text;
  try {
    text = await textCall(
      session,
      "sal_query",
      { text: buildAssetQuery(fixture) },
      "fixture asset query",
    );
  } catch (cause) {
    throw new PackagedSmokeAssertionError(
      [
        `required fixture asset is unavailable: ${fixture.blueprintAssetPath}`,
        `Cause: ${boundedText(message(cause))}`,
      ].join("\n"),
      { cause },
    );
  }
  const object = salObject(text, "fixture asset query");
  const found = object.statements.some(
    (statement) => statement?.value?.kind === "call"
      && statement.value.callee === "asset"
      && statement.value.args.path === fixture.blueprintAssetPath
      && statement.value.args.type === fixture.assetType,
  );
  assert(
    found,
    [
      `required fixture asset was not returned: ${fixture.blueprintAssetPath} (${fixture.assetType})`,
      `SAL response:\n${boundedText(text)}`,
    ].join("\n"),
  );
}

async function fixtureBlueprintId(session, fixture) {
  let text;
  try {
    text = await textCall(
      session,
      "sal_query",
      { text: buildBlueprintSummaryQuery(fixture) },
      "fixture Blueprint summary",
    );
  } catch (cause) {
    throw new PackagedSmokeAssertionError(
      [
        `required fixture Blueprint is unavailable: ${fixture.blueprintAssetPath}`,
        `Cause: ${boundedText(message(cause))}`,
      ].join("\n"),
      { cause },
    );
  }
  const object = salObject(text, "fixture Blueprint summary");
  const calls = callBindings(object);
  const blueprint = [...calls.values()].find(
    (call) => call.callee === "blueprint"
      && blueprintPath(call, calls) === fixture.blueprintAssetPath,
  );
  if (typeof blueprint?.args.id !== "string") {
    throw new PackagedSmokeAssertionError(
      `fixture Blueprint summary omitted a stable id: ${fixture.blueprintAssetPath}`,
    );
  }
  stableId(blueprint.args.id, "fixture Blueprint id");
  return blueprint.args.id;
}

async function blueprintDescription(
  session,
  fixture,
  blueprintId,
  requestSignal,
) {
  const text = await textCall(
    session,
    "sal_query",
    { text: buildBlueprintExactQuery(fixture, blueprintId) },
    "fixture Blueprint exact read",
    requestSignal,
  );
  const object = salObject(text, "fixture Blueprint exact read");
  const calls = callBindings(object);
  const blueprint = [...calls.values()].find(
    (call) => call.callee === "blueprint"
      && call.args.id === blueprintId
      && blueprintPath(call, calls) === fixture.blueprintAssetPath,
  );
  assert(
    blueprint,
    `exact read omitted fixture Blueprint ${fixture.blueprintAssetPath} id ${blueprintId}`,
  );
  return blueprint.args.BlueprintDescription;
}

async function mutationRoundTrip(
  session,
  fixture,
  blueprintId,
  mutation,
  cleanupTimeoutMs,
) {
  if (mutation === fixture.blueprintDescription) {
    throw new PackagedSmokeConfigurationError(
      "mutationDescription must differ from fixture.blueprintDescription",
    );
  }
  const patch = (description, dryRun = false, requestSignal) => textCall(
    session,
    "sal_patch",
    {
      text: buildBlueprintDescriptionPatch(
        fixture,
        blueprintId,
        description,
        { dryRun },
      ),
    },
    `BlueprintDescription ${dryRun ? "dry run" : "apply"}`,
    requestSignal,
  );

  const dryRun = await patch(mutation, true);
  mutationMetadata(dryRun, { dryRun: true, valid: true, applied: false });
  assertDescription(
    await blueprintDescription(session, fixture, blueprintId),
    fixture.blueprintDescription,
    "BlueprintDescription after dry run",
  );

  let crossedApplyBoundary = false;
  let failure;
  try {
    // The Bridge may apply before a timeout or transport failure reaches the
    // Client. Once a real Patch is dispatched, always attempt baseline restore.
    crossedApplyBoundary = true;
    const applied = await patch(mutation);
    mutationMetadata(applied, { dryRun: false, valid: true, applied: true });
    assertDescription(
      await blueprintDescription(session, fixture, blueprintId),
      mutation,
      "BlueprintDescription after apply",
    );
  } catch (error) {
    failure = error;
  } finally {
    if (crossedApplyBoundary) {
      try {
        await withCleanupDeadline(cleanupTimeoutMs, async (cleanupSignal) => {
          const restored = await patch(
            fixture.blueprintDescription,
            false,
            cleanupSignal,
          );
          mutationMetadata(restored, {
            dryRun: false,
            valid: true,
            applied: true,
          });
          assertDescription(
            await blueprintDescription(
              session,
              fixture,
              blueprintId,
              cleanupSignal,
            ),
            fixture.blueprintDescription,
            "BlueprintDescription after restore",
          );
        });
      } catch (restoreError) {
        failure = failure
          ? new AggregateError(
            [failure, restoreError],
            "mutation failed and fixture restoration also failed",
          )
          : restoreError;
      }
    }
  }
  if (failure) throw failure;
}

function mutationMetadata(text, expected) {
  const comments = salObject(text, "mutation result").statements
    .filter((statement) => statement?.kind === "comment")
    .flatMap((statement) => String(statement.text).split(/\r?\n/));
  for (const [name, value] of Object.entries(expected)) {
    assert(comments.includes(`${name}: ${value}`), `mutation result omitted ${name}: ${value}`);
  }
}

async function textCall(session, name, args, label, requestSignal) {
  publicTool(name);
  return requireToolText(
    await session.callTool(
      name,
      args,
      requestSignal ? { signal: requestSignal } : undefined,
    ),
    label,
  );
}

async function projectCall(session, args) {
  return parseProjectReport(await textCall(session, "project", args, "project"));
}

function boundProject(report, root, status) {
  const project = report.projects.find(
    (item) => item.projectId === report.boundProjectId && item.bound === true,
  );
  assert(project, `project report omitted bound candidate ${report.boundProjectId ?? "none"}`);
  assert(samePath(project.projectRoot, root), `bound project root mismatch: ${project.projectRoot}`);
  assert(project.status === status, `bound project is ${project.status}; expected ${status}`);
  return project;
}

function salObject(text, label) {
  const parsed = parseSalObject(text);
  assert(
    parsed.object && parsed.diagnostics.length === 0,
    `${label} did not return valid ordered SAL Object Text: ${JSON.stringify(parsed.diagnostics)}`,
  );
  return parsed.object;
}

function callBindings(object) {
  const calls = new Map();
  for (const statement of object.statements) {
    if (statement?.target?.kind === "local" && statement?.value?.kind === "call") {
      calls.set(statement.target.name, statement.value);
    }
  }
  return calls;
}

function blueprintPath(blueprint, calls) {
  const asset = blueprint.args.asset;
  if (typeof asset === "string") return asset;
  const call = asset?.kind === "local" ? calls.get(asset.name) : undefined;
  return call?.callee === "asset" ? call.args.path : undefined;
}

function boundedText(value, limit = 4096) {
  const text = String(value);
  return text.length <= limit
    ? text
    : `${text.slice(0, limit)}\n... ${text.length - limit} more characters`;
}

function smokeConfig(options) {
  if (!options || typeof options !== "object" || Array.isArray(options)) {
    throw new PackagedSmokeConfigurationError("smoke options must be an object");
  }
  const fixture = normalizeFixtureLocator(options.fixture);
  const sessionFactory = options.sessionFactory;
  if (sessionFactory !== undefined && typeof sessionFactory !== "function") {
    throw new PackagedSmokeConfigurationError("sessionFactory must be a function");
  }
  if (!sessionFactory) {
    nonEmpty(options.clientExecutable, "clientExecutable");
    nonEmpty(options.clientWorkingDirectory, "clientWorkingDirectory");
  }
  if (typeof options.startEditor !== "function" || typeof options.stopEditor !== "function") {
    throw new PackagedSmokeConfigurationError(
      "startEditor and stopEditor callbacks are required",
    );
  }
  const now = options.now ?? Date.now;
  const sleep = options.sleep ?? delay;
  const onStep = options.onStep;
  const signal = options.signal;
  const stderrSink = options.stderrSink;
  if (typeof now !== "function" || typeof sleep !== "function"
      || (onStep !== undefined && typeof onStep !== "function")
      || (stderrSink !== undefined && typeof stderrSink !== "function")
      || (signal !== undefined
        && (typeof signal !== "object"
          || typeof signal.addEventListener !== "function"))) {
    throw new PackagedSmokeConfigurationError(
      "now, sleep, optional onStep/stderrSink, and signal must have valid types",
    );
  }
  const numeric = (name) => positive(options[name] ?? defaults[name], name);
  const mutationDescription = nonEmpty(
    options.mutationDescription ?? `Loomle packaged E2E ${process.pid}-${Date.now()}`,
    "mutationDescription",
  );
  if (mutationDescription === fixture.blueprintDescription) {
    throw new PackagedSmokeConfigurationError(
      "mutationDescription must differ from fixture.blueprintDescription",
    );
  }
  return {
    projectRoot: resolve(nonEmpty(options.projectRoot, "projectRoot")),
    fixture,
    sessionFactory,
    startEditor: options.startEditor,
    stopEditor: options.stopEditor,
    mutationDescription,
    expectedServerVersion: options.expectedServerVersion,
    clientExecutable: options.clientExecutable,
    clientWorkingDirectory: options.clientWorkingDirectory,
    clientEnvironment: options.clientEnvironment ?? process.env,
    clientArguments: options.clientArguments ?? ["mcp"],
    connectTimeoutMs: numeric("connectTimeoutMs"),
    requestTimeoutMs: numeric("requestTimeoutMs"),
    cleanupTimeoutMs: numeric("cleanupTimeoutMs"),
    poll: {
      timeoutMs: numeric("statusTimeoutMs"),
      pollIntervalMs: numeric("pollIntervalMs"),
      now,
      sleep,
    },
    now,
    onStep,
    signal,
    stderrSink,
  };
}

function assertServer(value, expectedVersion) {
  assert(
    value?.name === "loomle"
      && typeof value.version === "string"
      && value.version.length > 0,
    `unexpected MCP server identity: ${JSON.stringify(value)}`,
  );
  if (expectedVersion !== undefined) {
    assert(
      value.version === expectedVersion,
      `candidate Client version mismatch: expected ${expectedVersion}, received ${value.version}`,
    );
  }
  return value;
}

function assertSession(value) {
  if (!value || ["listTools", "callTool", "close"].some(
    (name) => typeof value[name] !== "function",
  )) {
    throw new PackagedSmokeConfigurationError(
      "sessionFactory must return an initialized packaged MCP session",
    );
  }
}

function assertDescription(actual, expected, label) {
  assert(
    actual === expected,
    `${label} mismatch: expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`,
  );
}

function sameIdentity(expected, actual, transition) {
  assert(
    expected === actual,
    `project identity changed across ${transition}: ${expected} -> ${actual}`,
  );
}

function publicTool(name) {
  assert(
    PUBLIC_TOOL_NAMES.includes(name),
    `packaged smoke may call only public tools; rejected ${String(name)}`,
  );
}

function nonEmpty(value, name) {
  if (typeof value !== "string" || !value.trim()) {
    throw new PackagedSmokeConfigurationError(`${name} must be a non-empty string`);
  }
  return value;
}

function stableId(value, name) {
  nonEmpty(value, name);
  assert(/^[^\s.\[\]]+$/.test(value), `${name} is not a valid typed SAL id`);
  return value;
}

function positive(value, name) {
  if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) {
    throw new PackagedSmokeConfigurationError(`${name} must be positive`);
  }
  return value;
}

function assert(condition, text) {
  if (!condition) throw new PackagedSmokeAssertionError(text);
}

function samePath(left, right) {
  return typeof left === "string"
    && normalize(resolve(left)) === normalize(resolve(right));
}

function quoted(value) {
  return JSON.stringify(value);
}

function stringEnvironment(environment) {
  return Object.fromEntries(
    Object.entries(environment ?? {})
      .filter(([, value]) => value !== undefined)
      .map(([name, value]) => [name, String(value)]),
  );
}

function delay(milliseconds) {
  return new Promise((done) => setTimeout(done, milliseconds));
}

function message(error) {
  return error instanceof Error ? error.message : String(error);
}

export function appendTextTail(
  current,
  addition,
  maximumBytes = MAX_STDERR_TAIL_BYTES,
) {
  const combined = `${current ?? ""}${addition ?? ""}`;
  const bytes = Buffer.byteLength(combined);
  if (bytes <= maximumBytes) return combined;
  const buffer = Buffer.from(combined);
  return buffer.subarray(buffer.length - maximumBytes).toString("utf8")
    .replace(/^\uFFFD+/, "");
}

async function withCleanupDeadline(timeoutMs, operation) {
  const controller = new AbortController();
  const timeout = setTimeout(() => {
    controller.abort(new PackagedSmokeAssertionError(
      `timed out restoring fixture after ${timeoutMs} ms`,
    ));
  }, timeoutMs);
  timeout.unref?.();
  try {
    return await operation(controller.signal);
  } finally {
    clearTimeout(timeout);
  }
}

function throwIfAborted(signal) {
  if (!signal?.aborted) return;
  throw signal.reason instanceof Error
    ? signal.reason
    : new Error("packaged MCP smoke interrupted");
}
