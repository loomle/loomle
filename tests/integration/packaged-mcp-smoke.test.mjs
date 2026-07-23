import assert from "node:assert/strict";
import { resolve } from "node:path";
import test from "node:test";
import { parseSalObject } from "@loomle/sal";
import {
  PACKAGED_SMOKE_STEP_NAMES,
  PUBLIC_TOOL_NAMES,
  PackagedSmokeAssertionError,
  PackagedSmokeConfigurationError,
  appendTextTail,
  buildAssetQuery,
  buildBlueprintDescriptionPatch,
  buildBlueprintExactQuery,
  buildBlueprintSummaryQuery,
  normalizeFixtureLocator,
  parseProjectReport,
  requireToolText,
  runPackagedMcpSmoke,
  waitForProjectStatus,
} from "./packaged-mcp-smoke.mjs";

const projectRoot = "/tmp/loomle packaged smoke/LoomleTestHost";
const fixture = Object.freeze({
  blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
  blueprintDescription: "Loomle packaged E2E baseline",
  assetType: "/Script/Engine.Blueprint",
  assetSearchText: "BP_LoomleE2E",
  assetRoot: "/Game",
});
const blueprintId = "01234567-89AB-CDEF-0123-456789ABCDEF";
const mutationDescription = "Loomle packaged E2E mutation";

test("fixture locator is mandatory and SAL builders remain self-contained", () => {
  assert.throws(
    () => normalizeFixtureLocator(),
    (error) => error instanceof PackagedSmokeConfigurationError
      && /never skips a missing fixture/.test(error.message),
  );
  assert.throws(
    () => normalizeFixtureLocator({
      blueprintAssetPath: "/Game/BP_Missing.BP_Missing",
    }),
    /fixture\.blueprintDescription/,
  );

  const normalized = normalizeFixtureLocator(fixture);
  assert.equal(Object.isFrozen(normalized), true);
  assert.equal(
    normalizeFixtureLocator({
      blueprintAssetPath: fixture.blueprintAssetPath,
      blueprintDescription: fixture.blueprintDescription,
      assetType: fixture.assetType,
    }).assetRoot,
    "/Game",
  );
  assert.match(buildAssetQuery(fixture), /^query asset\nassets /);
  assert.match(
    buildAssetQuery(fixture),
    /path = "\/Game\/LoomleTests\/BP_LoomleE2E\.BP_LoomleE2E"/,
  );
  assert.match(buildBlueprintSummaryQuery(fixture), /\nquery fixtureBlueprint\nsummary$/);
  assert.match(
    buildBlueprintExactQuery(fixture, blueprintId),
    new RegExp(`blueprint@${blueprintId}$`),
  );

  const description = "quoted \"description\" with newline\nand slash \\\\";
  const patch = buildBlueprintDescriptionPatch(
    fixture,
    blueprintId,
    description,
    { dryRun: true },
  );
  const parsed = parseSalObject(patch);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.object.kind, "patch");
  assert.equal(parsed.object.dryRun, true);
  assert.equal(parsed.object.statements[0].kind, "set");
  assert.equal(parsed.object.statements[0].value, description);
});

test("project reports parse bound identity and status without private state", () => {
  const report = parseProjectReport(projectReport({
    status: "ready",
    root: projectRoot,
  }));
  assert.deepEqual(report, {
    boundProjectId: "loomle-test-host",
    projects: [{
      projectId: "loomle-test-host",
      name: "LoomleTestHost",
      projectRoot,
      status: "ready",
      bound: true,
    }],
  });

  assert.throws(
    () => parseProjectReport("not a project report"),
    PackagedSmokeAssertionError,
  );
});

test("tool text rejects tool errors and non-text result shapes", () => {
  assert.equal(
    requireToolText(textResult("# ok"), "fixture query"),
    "# ok",
  );
  assert.throws(
    () => requireToolText(
      { ...textResult("# ERROR resolution.object_not_found"), isError: true },
      "fixture query",
    ),
    /fixture query failed.*object_not_found/,
  );
  assert.throws(
    () => requireToolText({ content: [] }, "fixture query"),
    /exactly one MCP text block/,
  );
});

test("packaged smoke covers the complete small public MCP path", async () => {
  const harness = new FakePackagedHarness();
  let clock = 0;
  const observedSteps = [];

  const result = await runPackagedMcpSmoke({
    projectRoot,
    fixture,
    mutationDescription,
    expectedServerVersion: "0.7.0-test",
    sessionFactory: () => harness.connect(),
    startEditor: async () => {
      harness.editorStatus = "ready";
      harness.startCount += 1;
    },
    stopEditor: async () => {
      harness.editorStatus = "offline";
      harness.stopCount += 1;
    },
    now: () => clock++,
    sleep: async () => undefined,
    onStep: (step) => observedSteps.push(step),
  });

  assert.equal(result.status, "passed");
  assert.equal(result.projectId, "loomle-test-host");
  assert.equal(result.blueprintId, blueprintId);
  assert.deepEqual(
    result.steps.map((step) => step.name),
    PACKAGED_SMOKE_STEP_NAMES,
  );
  assert.ok(result.steps.every((step) => step.status === "passed"));
  assert.equal(
    observedSteps.filter((step) => step.status === "started").length,
    PACKAGED_SMOKE_STEP_NAMES.length,
  );
  assert.equal(harness.sessions.length, 2);
  assert.ok(harness.sessions.every((session) => session.closed));
  assert.equal(harness.startCount, 1);
  assert.equal(harness.stopCount, 1);
  assert.equal(harness.description, fixture.blueprintDescription);

  const toolNames = harness.calls.map((call) => call.name);
  assert.ok(PUBLIC_TOOL_NAMES.every((name) => toolNames.includes(name)));
  assert.ok(toolNames.every((name) => PUBLIC_TOOL_NAMES.includes(name)));
  assert.equal(toolNames.some((name) => name.includes(".")), false);
  assert.equal(
    harness.calls.filter((call) => call.name === "sal_patch").length,
    3,
  );
  assert.equal(
    harness.calls.filter(
      (call) => call.name === "project"
        && call.args.projectRoot === resolve(projectRoot),
    ).length,
    2,
    "each MCP session binds through the public project tool",
  );
});

test("missing fixture is a hard failure and still stops the owned Editor", async () => {
  const harness = new FakePackagedHarness({ assetMissing: true });

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      sessionFactory: () => harness.connect(),
      startEditor: async () => {
        harness.editorStatus = "ready";
        harness.startCount += 1;
      },
      stopEditor: async () => {
        harness.editorStatus = "offline";
        harness.stopCount += 1;
      },
      now: monotonicClock(),
      sleep: async () => undefined,
    }),
    (error) => error instanceof PackagedSmokeAssertionError
      && /required fixture asset was not returned/.test(error.message),
  );

  assert.equal(harness.startCount, 1);
  assert.equal(harness.stopCount, 1);
  assert.equal(harness.sessions.length, 1);
  assert.equal(harness.sessions[0].closed, true);
  assert.equal(
    harness.calls.some((call) => call.name === "sal_patch"),
    false,
    "fixture failures are not converted into skipped mutation scenarios",
  );
});

test("an apply readback failure still restores the fixture description", async () => {
  const harness = new FakePackagedHarness({ corruptAppliedReadback: true });

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      sessionFactory: () => harness.connect(),
      startEditor: async () => {
        harness.editorStatus = "ready";
      },
      stopEditor: async () => {
        harness.editorStatus = "offline";
      },
      now: monotonicClock(),
      sleep: async () => undefined,
    }),
    /BlueprintDescription after apply mismatch/,
  );

  assert.equal(harness.description, fixture.blueprintDescription);
  const patches = harness.calls.filter((call) => call.name === "sal_patch");
  assert.equal(patches.length, 3, "dry-run, apply, and restoration all ran");
  assert.match(
    patches.at(-1).args.text,
    /Loomle packaged E2E baseline/,
  );
});

test("an apply transport failure after mutation still restores the fixture", async () => {
  const harness = new FakePackagedHarness({ throwAfterApply: true });

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      sessionFactory: () => harness.connect(),
      startEditor: async () => {
        harness.editorStatus = "ready";
      },
      stopEditor: async () => {
        harness.editorStatus = "offline";
      },
      now: monotonicClock(),
      sleep: async () => undefined,
    }),
    /simulated transport failure after apply/,
  );

  assert.equal(harness.description, fixture.blueprintDescription);
  const patches = harness.calls.filter((call) => call.name === "sal_patch");
  assert.equal(patches.length, 3, "dry-run, uncertain apply, and restoration ran");
  assert.match(patches.at(-1).args.text, /Loomle packaged E2E baseline/);
});

test("an aborted main phase restores through an independent cleanup signal", async () => {
  const controller = new AbortController();
  const reason = new Error("runner phase timed out");
  const harness = new FakePackagedHarness({
    abortAfterApply: () => controller.abort(reason),
  });

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      signal: controller.signal,
      cleanupTimeoutMs: 100,
      sessionFactory: () => harness.connect(controller.signal),
      startEditor: async () => {
        harness.editorStatus = "ready";
      },
      stopEditor: async () => {
        harness.editorStatus = "offline";
      },
      now: monotonicClock(),
      sleep: async () => undefined,
    }),
    (error) => error === reason,
  );

  assert.equal(harness.description, fixture.blueprintDescription);
  const patches = harness.calls.filter((call) => call.name === "sal_patch");
  assert.equal(patches.length, 3);
  assert.equal(patches[1].requestSignal, controller.signal);
  assert.notEqual(patches[2].requestSignal, controller.signal);
  assert.equal(patches[2].requestSignal.aborted, false);
});

test("tool inventory drift fails before starting Editor", async () => {
  const harness = new FakePackagedHarness({
    tools: [...PUBLIC_TOOL_NAMES, "rpc.invoke"],
  });
  let editorStarted = false;

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      sessionFactory: () => harness.connect(),
      startEditor: async () => {
        editorStarted = true;
      },
      stopEditor: async () => undefined,
    }),
    /unexpected public tool inventory.*rpc\.invoke/,
  );

  assert.equal(editorStarted, false);
  assert.equal(harness.calls.length, 0);
  assert.equal(harness.sessions[0].closed, true);
});

test("public project polling times out with the last observed status", async () => {
  let clock = 0;
  const session = {
    async callTool(name) {
      assert.equal(name, "project");
      return textResult(projectReport({ status: "offline", root: projectRoot }));
    },
  };

  await assert.rejects(
    waitForProjectStatus(session, {
      projectRoot,
      expectedStatus: "ready",
      timeoutMs: 3,
      pollIntervalMs: 1,
      now: () => clock++,
      sleep: async () => undefined,
    }),
    /last status: offline/,
  );
});

test("an AbortSignal stops smoke work before opening a Client session", async () => {
  const controller = new AbortController();
  const reason = new Error("runner phase timed out");
  controller.abort(reason);
  let connected = false;

  await assert.rejects(
    runPackagedMcpSmoke({
      projectRoot,
      fixture,
      mutationDescription,
      signal: controller.signal,
      sessionFactory: async () => {
        connected = true;
        throw new Error("must not connect");
      },
      startEditor: async () => undefined,
      stopEditor: async () => undefined,
    }),
    (error) => error === reason,
  );
  assert.equal(connected, false);
});

test("stderr tail remains UTF-8-safe and bounded", () => {
  const tail = appendTextTail("prefix", `-${"界".repeat(30)}`, 32);
  assert.ok(Buffer.byteLength(tail) <= 32);
  assert.equal(tail.includes("\uFFFD"), false);
  assert.match(tail, /界+$/);
});

class FakePackagedHarness {
  constructor({
    assetMissing = false,
    corruptAppliedReadback = false,
    throwAfterApply = false,
    abortAfterApply,
    tools = PUBLIC_TOOL_NAMES,
  } = {}) {
    this.assetMissing = assetMissing;
    this.corruptAppliedReadback = corruptAppliedReadback;
    this.throwAfterApply = throwAfterApply;
    this.abortAfterApply = abortAfterApply;
    this.didThrowAfterApply = false;
    this.tools = tools;
    this.editorStatus = "offline";
    this.description = fixture.blueprintDescription;
    this.calls = [];
    this.sessions = [];
    this.startCount = 0;
    this.stopCount = 0;
  }

  async connect(defaultSignal) {
    const harness = this;
    const session = {
      serverVersion: { name: "loomle", version: "0.7.0-test" },
      bound: false,
      closed: false,
      async listTools() {
        return harness.tools.map((name) => ({ name }));
      },
      async callTool(name, args, options) {
        const requestSignal = options?.signal ?? defaultSignal;
        harness.calls.push({
          session: harness.sessions.indexOf(session),
          name,
          args,
          requestSignal,
        });
        if (requestSignal?.aborted) throw requestSignal.reason;
        if (!PUBLIC_TOOL_NAMES.includes(name)) {
          throw new Error(`fake received private tool ${name}`);
        }
        if (name === "project") {
          if (args.projectRoot !== undefined) session.bound = true;
          return textResult(session.bound
            ? projectReport({ status: harness.editorStatus, root: projectRoot })
            : "bound: none\nprojects:\n  none");
        }
        if (name === "sal_schema") {
          return textResult([
            "asset",
            "  Find UE assets.",
            "",
            "blueprint",
            "  Inspect Blueprints.",
            "",
            "class",
            "  Inspect classes.",
            "",
            "graph",
            "  Inspect Graphs.",
            "",
            "state_tree",
            "  Inspect StateTrees.",
            "",
            "widget",
            "  Inspect Widgets.",
          ].join("\n"));
        }
        if (name === "sal_query") {
          return harness.query(args.text);
        }
        if (name === "sal_patch") {
          return harness.patch(args.text, requestSignal);
        }
        if (name === "editor_context") {
          return textResult("# surface: unknown\n# selection: unavailable");
        }
        throw new Error(`unhandled fake tool ${name}`);
      },
      async close() {
        session.closed = true;
      },
      getStderr() {
        return "";
      },
    };
    this.sessions.push(session);
    return session;
  }

  query(text) {
    if (/^query asset$/m.test(text)) {
      return textResult(this.assetMissing
        ? "# fixture asset search returned zero results"
        : assetObjectText());
    }
    if (/^summary$/m.test(text)) {
      return textResult(blueprintObjectText(this.description, { complete: false }));
    }
    if (new RegExp(`^blueprint@${blueprintId}$`, "m").test(text)) {
      const description = this.corruptAppliedReadback
          && this.description === mutationDescription
        ? "corrupted readback"
        : this.description;
      return textResult(blueprintObjectText(description, { complete: true }));
    }
    return {
      ...textResult("# unknown query"),
      isError: true,
    };
  }

  patch(text, requestSignal) {
    const parsed = parseSalObject(text);
    assert.deepEqual(parsed.diagnostics, []);
    assert.equal(parsed.object.kind, "patch");
    const set = parsed.object.statements.find(
      (statement) => statement.kind === "set",
    );
    assert.ok(set);
    if (!parsed.object.dryRun) {
      this.description = set.value;
      if (this.abortAfterApply && !this.didThrowAfterApply) {
        this.didThrowAfterApply = true;
        this.abortAfterApply();
        throw requestSignal?.reason
          ?? new Error("apply response interrupted by phase abort");
      }
      if (this.throwAfterApply && !this.didThrowAfterApply) {
        this.didThrowAfterApply = true;
        throw new Error("simulated transport failure after apply");
      }
    }
    return textResult([
      "# BlueprintDescription mutation",
      "###",
      "SAL result",
      "operation: patch",
      `dryRun: ${parsed.object.dryRun}`,
      "valid: true",
      `applied: ${!parsed.object.dryRun}`,
      "###",
    ].join("\n"));
  }
}

function projectReport({ status, root }) {
  return [
    "bound: loomle-test-host",
    "projects:",
    "- loomle-test-host",
    "  name: LoomleTestHost",
    `  projectRoot: ${root}`,
    `  status: ${status}`,
    "  bound: true",
  ].join("\n");
}

function assetObjectText() {
  return [
    "fixtureAsset = asset(",
    `  path: ${JSON.stringify(fixture.blueprintAssetPath)},`,
    `  type: ${JSON.stringify(fixture.assetType)},`,
    "  domains: [asset, blueprint],",
    "  loaded: false,",
    "  score: 100",
    ")",
  ].join("\n");
}

function blueprintObjectText(description, { complete }) {
  return [
    "fixtureAsset = asset(",
    `  path: ${JSON.stringify(fixture.blueprintAssetPath)},`,
    `  type: ${JSON.stringify(fixture.assetType)}`,
    ")",
    "fixtureBlueprint = blueprint(",
    "  asset: fixtureAsset,",
    `  id: ${JSON.stringify(blueprintId)},`,
    "  type: BPTYPE_Normal,",
    "  Status: BS_UpToDate,",
    `  ParentClass: "/Script/Engine.Actor"${complete ? "," : ""}`,
    ...(complete
      ? [`  BlueprintDescription: ${JSON.stringify(description)}`]
      : []),
    ")",
    ...(complete ? [] : ["# variables: 0", "# graphs: 1"]),
  ].join("\n");
}

function textResult(text) {
  return { content: [{ type: "text", text }] };
}

function monotonicClock() {
  let value = 0;
  return () => value++;
}
