import assert from "node:assert/strict";
import { guide } from "@loomle/interfaces";
import { parseSalResultText } from "@loomle/sal";
import test from "node:test";
import { RuntimeRpcError, type RpcInvoker } from "../src/runtime-rpc.js";
import type { StatusProvider } from "../src/status.js";
import { SalToolService, toolDefinitions } from "../src/tools.js";

class MockRpc implements RpcInvoker {
  readonly calls: Array<{
    tool: string;
    args: Record<string, unknown>;
    signal?: AbortSignal;
  }> = [];

  constructor(private readonly response: unknown) {}

  async invoke(
    tool: string,
    args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<unknown> {
    this.calls.push({ tool, args, ...(signal ? { signal } : {}) });
    return this.response;
  }
}

class ThrowingRpc implements RpcInvoker {
  constructor(private readonly error: Error) {}

  async invoke(): Promise<unknown> {
    throw this.error;
  }
}

const blueprintId = "11111111-1111-1111-1111-111111111111";
const graphId = "22222222-2222-2222-2222-222222222222";
const blueprintAsset = "/Game/BP_Door.BP_Door";

const assetRootTarget = {
  alias: "assets",
  target: { kind: "target", domain: "asset" },
};

const blueprintTarget = {
  alias: "door",
  target: {
    kind: "target",
    domain: "blueprint",
    asset: blueprintAsset,
    id: blueprintId,
  },
};

const graphTarget = {
  alias: "g",
  target: {
    kind: "target",
    domain: "graph",
    asset: blueprintAsset,
    blueprintId,
    id: graphId,
  },
};

const emptyObjectResult = {
  targetContext: "domain_root",
  target: assetRootTarget,
  object: {
    statements: [
      { kind: "comment", text: "result" },
    ],
  },
  diagnostics: [],
};

function allText(result: { content: Array<{ text: string }> }): string {
  return result.content.map(({ text }) => text).join("\n\n");
}

function laterText(result: { content: Array<{ text: string }> }): string {
  return result.content.slice(1).map(({ text }) => text).join("\n\n");
}

function assertUnresolvedResultFirstBlock(
  result: { content: Array<{ text: string }> },
): void {
  assert.equal(result.content[0]?.text, "result unresolved_target\nno_objects");
  const parsed = parseSalResultText(result.content[0].text);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.result?.targetContext, "unresolved_target");
  assert.equal(parsed.result?.object, undefined);
}

test("exposes the unified editor tool", () => {
  assert.deepEqual(toolDefinitions.map((tool) => tool.name), [
    "status",
    "project",
    "sal_query",
    "sal_patch",
    "sal_schema",
    "agent_skill",
    "editor",
    "python",
  ]);
  const editor = toolDefinitions.find((tool) => tool.name === "editor");
  assert.deepEqual(editor?.annotations, {
    readOnlyHint: false,
    destructiveHint: false,
    idempotentHint: true,
    openWorldHint: false,
  });
  assert.deepEqual(
    (editor?.inputSchema.properties as Record<string, { enum?: string[] }>).operation.enum,
    ["context", "open", "close"],
  );
  const python = toolDefinitions.find((tool) => tool.name === "python");
  assert.deepEqual(python?.annotations, {
    readOnlyHint: false,
    destructiveHint: true,
    idempotentHint: false,
    openWorldHint: true,
  });
  assert.equal(Array.isArray(python?.inputSchema.oneOf), true);
  assert.equal((python?.outputSchema?.properties as Record<string, unknown>).status !== undefined, true);
  const schema = toolDefinitions.find((tool) => tool.name === "sal_schema");
  assert.deepEqual(
    (schema?.inputSchema.properties as Record<string, { enum?: string[] }>).module.enum,
    [
      "asset",
      "blueprint",
      "class",
      "graph",
      "state_tree",
      "widget",
      "level",
      "pcg",
      "pcg_component",
    ],
  );
  assert.ok(toolDefinitions.every((tool) => tool.title.length > 0));
  assert.ok(toolDefinitions.every((tool) => typeof tool.annotations.openWorldHint === "boolean"));
});

test("python run returns the agent-defined structured result without an execution id", async () => {
  const response = {
    status: "succeeded",
    stateMayHaveChanged: true,
    result: {
      assetPath: "/Game/Data/DT_Weapons.DT_Weapons",
      rowsCreated: 42,
    },
    logs: [{ type: "info", output: "created rows" }],
    logsTruncated: false,
    durationMs: 18,
  };
  const rpc = new MockRpc(response);
  const controller = new AbortController();
  const result = await new SalToolService(rpc).call("python", {
    operation: "run",
    script: "def run():\n    return {'rowsCreated': 42}",
  }, controller.signal);

  assert.equal(result.isError, undefined);
  assert.deepEqual(result.structuredContent, response);
  assert.equal(result.content[0].text, JSON.stringify(response));
  assert.deepEqual(rpc.calls, [{
    tool: "python.run",
    args: { script: "def run():\n    return {'rowsCreated': 42}" },
  }]);
});

test("python run accepts the sal.object() projection annex and projected views", async () => {
  const response = {
    status: "succeeded",
    stateMayHaveChanged: true,
    result: {
      marked: {
        status: "projected",
        relation: "exact",
        view: {
          target: {
            alias: "projection",
            target: { kind: "target", domain: "class", path: "/Script/Engine.Actor" },
          },
          object: { statements: [] },
          diagnostics: [],
          comments: [],
        },
      },
    },
    projection: { complete: true, marked: 1, projected: 1 },
    logs: [],
    logsTruncated: false,
    durationMs: 8,
  };
  const rpc = new MockRpc(response);
  const result = await new SalToolService(rpc).call("python", {
    operation: "run",
    script: "def run():\n    return {'marked': sal.object(unreal.Actor)}",
  });
  assert.equal(result.isError, undefined);
  assert.deepEqual(result.structuredContent, response);
});

test("python run returns an exact poll continuation for a detached execution", async () => {
  const response = {
    status: "running",
    executionId: "py_012345",
    stateMayHaveChanged: true,
    elapsedMs: 1004,
    continuation: {
      tool: "python",
      arguments: { operation: "poll", executionId: "py_012345" },
      pollAfterMs: 1000,
    },
  };
  const rpc = new MockRpc(response);
  const result = await new SalToolService(rpc).call("python", {
    operation: "run",
    script: "def run():\n    return {}",
  });

  assert.equal(result.isError, undefined);
  assert.deepEqual(result.structuredContent, response);
  assert.match(result.content[0].text, /Do not run the script again/);
  assert.match(result.content[0].text, /"operation":"poll"/);
});

test("python poll forwards only the returned execution id and remains cancellable", async () => {
  const response = {
    status: "succeeded",
    executionId: "py_012345",
    stateMayHaveChanged: true,
    result: {},
    logs: [],
    logsTruncated: false,
    durationMs: 2500,
  };
  const rpc = new MockRpc(response);
  const controller = new AbortController();
  const result = await new SalToolService(rpc).call("python", {
    operation: "poll",
    executionId: "py_012345",
  }, controller.signal);

  assert.deepEqual(result.structuredContent, response);
  assert.deepEqual(rpc.calls, [{
    tool: "python.poll",
    args: { executionId: "py_012345" },
    signal: controller.signal,
  }]);
});

test("python preserves an executed failure as structured content and a tool error", async () => {
  const response = {
    status: "failed",
    stateMayHaveChanged: true,
    error: {
      code: "runtime.python_execution_failed",
      phase: "execution",
      message: "boom",
      traceback: "Traceback: boom",
      retryable: false,
    },
    logs: [],
    logsTruncated: false,
    durationMs: 2,
  };
  const result = await new SalToolService(new MockRpc(response)).call("python", {
    operation: "run",
    script: "def run():\n    raise RuntimeError('boom')",
  });

  assert.equal(result.isError, true);
  assert.deepEqual(result.structuredContent, response);
});

test("python rejects mixed or incomplete operation arguments before Bridge dispatch", async () => {
  for (const args of [
    {},
    { operation: "status", executionId: "py_1" },
    { operation: "run" },
    { operation: "run", script: "def run(): return {}", executionId: "py_1" },
    { operation: "poll" },
    { operation: "poll", executionId: "py_1", script: "def run(): return {}" },
  ]) {
    const rpc = new MockRpc({});
    const result = await new SalToolService(rpc).call("python", args);
    assert.equal(result.isError, true);
    assert.match(result.content[0].text, /tool\.invalid_arguments/);
    assert.equal(rpc.calls.length, 0);
  }
});

test("status reports identity, binding, Bridge health, and Windows update guidance", async () => {
  const status: StatusProvider = {
    async report() {
      return {
        client: {
          version: "0.7.0-rc.1",
          distribution: "github",
          pid: 1234,
          platform: "win32",
          target: "win32-x64",
          executable: "C:/Loomle/loomle.exe",
        },
        update: {
          status: "available",
          authority: "github",
          version: "0.7.0-rc.2",
          releaseUrl: "https://example.test/release",
          assetUrl: "https://example.test/asset.zip",
          sha256: "abc123",
        },
        session: {
          status: "ready",
          project: {
            projectId: "alpha",
            name: "Alpha",
            projectRoot: "C:/Projects/Alpha",
          },
          bridge: {
            version: "0.7.0-rc.1",
            protocolVersion: 3,
            pluginPath: "C:/UE/Engine/Plugins/Marketplace/LoomleBridge",
          },
        },
      };
    },
  };
  const result = await new SalToolService(new MockRpc(emptyObjectResult), status)
    .call("status", {});

  assert.equal(result.isError, undefined);
  assert.match(result.content[0].text, /^client:\n  version: 0\.7\.0-rc\.1$/m);
  assert.match(result.content[0].text, /^  distribution: github$/m);
  assert.match(result.content[0].text, /^session:\n  project: alpha$/m);
  assert.match(result.content[0].text, /^bridge:\n  version: 0\.7\.0-rc\.1$/m);
  assert.match(result.content[0].text, /normal PowerShell/);
  assert.match(result.content[0].text, /Stop-Process -Id <pid>/);
});

test("status omits Client-stop guidance on macOS", async () => {
  const status: StatusProvider = {
    async report() {
      return {
        client: {
          version: "0.7.0-rc.1",
          distribution: "github",
          pid: 1234,
          platform: "darwin",
          target: "darwin-arm64",
          executable: "/Loomle/loomle",
        },
        update: { status: "available", authority: "github", version: "0.7.0-rc.2" },
        session: { status: "unbound" },
      };
    },
  };
  const result = await new SalToolService(new MockRpc(emptyObjectResult), status)
    .call("status", {});

  assert.match(result.content[0].text, /ensure affected Unreal Editors are closed/);
  assert.doesNotMatch(result.content[0].text, /Stop-Process/);
});

test("status directs Fab updates only through the Fab Library", async () => {
  const status: StatusProvider = {
    async report() {
      return {
        client: {
          version: "0.7.0",
          distribution: "fab",
          pid: 1234,
          platform: "darwin",
          target: "darwin-arm64",
          executable: "/Loomle/loomle",
        },
        update: {
          status: "available",
          authority: "fab",
          version: "0.7.1",
          listing: "https://www.fab.com/listings/f0fb545c-b1d9-4525-8642-3f170134c428",
        },
        session: { status: "unbound" },
      };
    },
  };
  const result = await new SalToolService(new MockRpc(emptyObjectResult), status)
    .call("status", {});

  assert.match(result.content[0].text, /^  distribution: fab$/m);
  assert.match(result.content[0].text, /^  authority: fab$/m);
  assert.match(result.content[0].text, /Fab Library entry in the Epic Games Launcher/);
  assert.match(result.content[0].text, /do not replace this Fab installation with the GitHub package/);
  assert.doesNotMatch(result.content[0].text, /replace the complete plugin/);
});

test("status keeps Claude Client updates separate from the recommended Bridge", async () => {
  const status: StatusProvider = {
    async report() {
      return {
        client: {
          version: "0.7.0",
          distribution: "claude",
          pid: 1234,
          platform: "darwin",
          target: "darwin-arm64",
          engineVersion: "5.8",
          executable: "/plugin/mcp/loomle.cjs",
        },
        update: {
          status: "available",
          authority: "claude",
          version: "0.7.1",
          listing: "https://claude.ai/directory/connectors/loomle",
          bridge: {
            version: "0.7.1",
            releaseUrl: "https://github.com/loomle/loomle/releases/tag/v0.7.1",
            assetUrl: "https://github.com/loomle/loomle/releases/download/v0.7.1/loomle-bridge-0.7.1-ue5.8.zip",
            sha256: "a".repeat(64),
          },
        },
        session: {
          status: "ready",
          bridge: { version: "0.7.0", protocolVersion: 3 },
        },
      };
    },
  };
  const result = await new SalToolService(new MockRpc(emptyObjectResult), status)
    .call("status", {});

  assert.match(result.content[0].text, /^recommendedBridge:\n  version: 0\.7\.1$/m);
  assert.match(result.content[0].text, /update the Loomle extension through Claude Desktop/);
  assert.match(result.content[0].text, /matching Unreal Bridge is a separate user-approved installation/);
  assert.doesNotMatch(result.content[0].text, /normal PowerShell/);
});

test("keeps the resident guide only on sal_schema", () => {
  const schema = toolDefinitions.find((tool) => tool.name === "sal_schema");
  assert.equal(schema?.description, guide);
  assert.equal(
    toolDefinitions.filter((tool) => tool.description.includes(guide)).length,
    1,
  );
  assert.ok(
    toolDefinitions
      .filter((tool) => !["sal_schema", "agent_skill"].includes(tool.name))
      .every((tool) => tool.description.length < 300),
  );
  const agentSkill = toolDefinitions.find((tool) => tool.name === "agent_skill");
  assert.match(agentSkill?.description ?? "", /debug-unreal-pie-with-python/);
  assert.match(agentSkill?.description ?? "", /runtime Actors or components/);
  assert.match(agentSkill?.description ?? "", /format-unreal-blueprints/);
  assert.match(agentSkill?.description ?? "", /near-human visual quality/);
  assert.match(agentSkill?.description ?? "", /use-unreal-python/);
  assert.match(agentSkill?.description ?? "", /partial-state recovery/);
  assert.doesNotMatch(agentSkill?.description ?? "", /# Format Unreal Blueprints/);
  const python = toolDefinitions.find((tool) => tool.name === "python");
  assert.match(python?.description ?? "", /Before run, load use-unreal-python/);
  assert.match(python?.description ?? "", /for PIE also load debug-unreal-pie-with-python/);
  assert.match(python?.description ?? "", /never replay/);
});

test("project inspects or changes only the Client session binding", async () => {
  class ProjectRpc extends MockRpc {
    readonly selectors: unknown[] = [];

    async project(selector: unknown) {
      this.selectors.push(selector);
      return {
        boundProjectId: "alpha",
        projects: [{
          projectId: "alpha",
          name: "Alpha",
          projectRoot: "/Projects/Alpha",
          status: "ready" as const,
          bound: true,
        }],
      };
    }
  }
  const rpc = new ProjectRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("project", { projectId: "alpha" });

  assert.equal(result.isError, undefined);
  assert.match(result.content[0].text, /^bound: alpha$/m);
  assert.match(result.content[0].text, /^- alpha$/m);
  assert.match(result.content[0].text, /^  status: ready$/m);
  assert.deepEqual(rpc.selectors, [{ projectId: "alpha", projectRoot: undefined }]);
  assert.equal(rpc.calls.length, 0);
});

test("project rejects two selectors without changing the binding", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("project", {
    projectId: "alpha",
    projectRoot: "/Projects/Alpha",
  });
  assert.equal(result.isError, true);
  assert.match(result.content[0].text, /tool\.invalid_arguments/);
  assert.equal(rpc.calls.length, 0);
});

test("sal_query parses and normalizes Text before invoking Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const service = new SalToolService(rpc);
  const result = await service.call("sal_query", {
    text: "assets = target { domain: asset }\n\nquery assets\nassets \"BP_Door\"",
  });

  assert.equal(result.isError, undefined);
  assert.equal(result.content[0].text, [
    "result domain_root",
    "target assets = target {domain: asset}",
    "objects",
    "# result",
  ].join("\n"));
  assert.equal("structuredContent" in result, false);
  assert.equal(rpc.calls.length, 1);
  assert.equal(rpc.calls[0].tool, "sal.query");
  assert.deepEqual(rpc.calls[0].args, {
    object: {
      kind: "query",
      target: {
        alias: "assets",
        target: { kind: "target", domain: "asset" },
      },
      operation: { kind: "assets", text: "BP_Door" },
    },
  });
  assert.equal(rpc.calls[0].signal, undefined);
});

test("sal_query forwards the MCP AbortSignal through the SAL executor", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const controller = new AbortController();
  await new SalToolService(rpc).call("sal_query", {
    text: "assets = target { domain: asset }\n\nquery assets\nassets \"BP_Door\"",
  }, controller.signal);

  assert.equal(rpc.calls.length, 1);
  assert.equal(rpc.calls[0].signal, controller.signal);
});

test("sal_query preserves multiline node diagnostics as ordered Object Text comments", async () => {
  const rpc = new MockRpc({
    targetContext: "exact_target",
    target: graphTarget,
    object: {
      statements: [
        {
          target: { kind: "local", name: "graphInfo" },
          value: {
            kind: "object",
            semanticTag: "graph_record",
            fields: { id: graphId },
          },
        },
        {
          target: { kind: "local", name: "Get_Participant_Entry" },
          value: {
            kind: "object",
            semanticTag: "node",
            fields: {
              graph: { kind: "local", name: "graphInfo" },
              id: "33333333-3333-3333-3333-333333333333",
              type: "/Script/BlueprintGraph.K2Node_CallFunction",
            },
          },
        },
        { kind: "comment", text: "Get Participant Entry" },
        {
          kind: "comment",
          text: [
            "UE node diagnostic: Error",
            "In use pin Controller no longer exists on node Get Participant Entry.",
            "Could not find a function named \"GetParticipantEntry\".",
          ].join("\n"),
        },
        {
          target: {
            kind: "member",
            object: { kind: "local", name: "Get_Participant_Entry" },
            path: ["Controller"],
          },
          value: {
            kind: "object",
            semanticTag: "pin",
            fields: {
              id: "44444444-4444-4444-4444-444444444444",
              direction: { kind: "name", name: "in" },
            },
          },
        },
      ],
    },
    diagnostics: [],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: [
      "g = target {",
      "  domain: graph,",
      "  asset: \"/Game/BP_Door.BP_Door\",",
      "  blueprintId: \"11111111-1111-1111-1111-111111111111\",",
      "  id: \"22222222-2222-2222-2222-222222222222\"",
      "}",
      "",
      "query g",
      "@33333333-3333-3333-3333-333333333333",
    ].join("\n"),
  });

  assert.equal(result.isError, undefined);
  assert.equal(result.content[0].text, [
    "result exact_target",
    `target g = target {domain: graph, asset: ${JSON.stringify(blueprintAsset)}, blueprintId: ${JSON.stringify(blueprintId)}, id: ${JSON.stringify(graphId)}}`,
    "objects",
    `graphInfo = graph_record {id: ${JSON.stringify(graphId)}}`,
    "Get_Participant_Entry = node {graph: graphInfo, id: \"33333333-3333-3333-3333-333333333333\", type: \"/Script/BlueprintGraph.K2Node_CallFunction\"}",
    "# Get Participant Entry",
    "###",
    "UE node diagnostic: Error",
    "In use pin Controller no longer exists on node Get Participant Entry.",
    "Could not find a function named \"GetParticipantEntry\".",
    "###",
    "Get_Participant_Entry.Controller = pin {id: \"44444444-4444-4444-4444-444444444444\", direction: in}",
  ].join("\n"));
  assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
});

test("invalid SAL returns unresolved Result Text and never reaches Bridge", async () => {
  for (const name of ["sal_query", "sal_patch"] as const) {
    const rpc = new MockRpc(emptyObjectResult);
    const result = await new SalToolService(rpc).call(name, { text: "not SAL" });
    assert.equal(result.isError, true);
    assertUnresolvedResultFirstBlock(result);
    assert.match(laterText(result), /ERROR language\./);
    assert.equal(rpc.calls.length, 0);
  }
});

test("sal_patch invokes the normalized mutation endpoint", async () => {
  const rpc = new MockRpc({
    targetContext: "exact_target",
    target: blueprintTarget,
    object: emptyObjectResult.object,
    diagnostics: [],
    isError: false,
    dryRun: true,
    valid: true,
    applied: false,
    operation: "patch",
  });
  const controller = new AbortController();
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      "door = target { domain: blueprint, asset: \"/Game/BP_Door.BP_Door\", id: \"11111111-1111-1111-1111-111111111111\" }",
      "",
      "patch door dry run",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  }, controller.signal);

  assert.equal(result.isError, undefined);
  assert.equal(rpc.calls[0].tool, "sal.patch");
  assert.equal(rpc.calls[0].signal, undefined);
  assert.equal((rpc.calls[0].args.object as { dryRun: boolean }).dryRun, true);
  assert.match(allText(result), /dryRun: true/);
  assert.match(allText(result), /###\nSAL result\n/);
  assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
});

test("sal_patch preserves a failed MutationResult as an agent-visible tool error", async () => {
  const rpc = new MockRpc({
    targetContext: "exact_target",
    target: blueprintTarget,
    object: emptyObjectResult.object,
    diagnostics: [
      {
        severity: "error",
        code: "graph.operation_failed",
        message: "A later ordered operation failed.",
        operation: "connect",
        ref: "@33333333-3333-3333-3333-333333333333/44444444-4444-4444-4444-444444444444",
      },
    ],
    isError: true,
    dryRun: false,
    valid: true,
    applied: true,
    operation: "patch",
  });
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      "door = target { domain: blueprint, asset: \"/Game/BP_Door.BP_Door\", id: \"11111111-1111-1111-1111-111111111111\" }",
      "",
      "patch door",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  });

  assert.equal(result.isError, true);
  assert.equal("structuredContent" in result, false);
  const text = allText(result);
  assert.match(text, /^# result/m);
  assert.match(text, /applied: true/);
  assert.match(text, /ERROR graph\.operation_failed/);
  assert.match(text, /operation: connect/);
  assert.match(text, /ref: @33333333-3333-3333-3333-333333333333\/44444444-4444-4444-4444-444444444444/);
  assert.match(text, /###\nSAL diagnostics\n/);
  assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
});

test("preserves every structured execution field inside SAL comments", async () => {
  const rpc = new MockRpc({
    targetContext: "exact_target",
    target: blueprintTarget,
    diagnostics: [],
    isError: false,
    dryRun: true,
    valid: true,
    applied: false,
    operation: "patch",
    resolvedRefs: { target: "@33333333-3333-3333-3333-333333333333" },
    planned: { operations: [{ kind: "set" }] },
    diff: { changed: ["NodeComment"] },
  });
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      "door = target { domain: blueprint, asset: \"/Game/BP_Door.BP_Door\", id: \"11111111-1111-1111-1111-111111111111\" }",
      "",
      "patch door dry run",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  });

  const text = allText(result);
  assert.match(text, /resolvedRefs: \{"target":"@33333333-3333-3333-3333-333333333333"\}/);
  assert.match(text, /planned: \{"operations":\[\{"kind":"set"\}\]\}/);
  assert.match(text, /diff: \{"changed":\["NodeComment"\]\}/);
});

test("renders every populated diagnostic locator and expectation field", async () => {
  const rpc = new MockRpc({
    targetContext: "domain_root",
    target: assetRootTarget,
    object: emptyObjectResult.object,
    diagnostics: [
      {
        severity: "error",
        code: "graph.target_mismatch",
        message: "The selected Pin belongs to another Graph.",
        path: ["object", "statements", 2],
        span: { line: 4, column: 9, length: 12 },
        domain: "graph",
        operation: "connect",
        ref: "@33333333-3333-3333-3333-333333333333/44444444-4444-4444-4444-444444444444",
        expected: { graph: graphId },
        actual: null,
        supported: ["pin", "node"],
        matches: [{ id: "55555555-5555-5555-5555-555555555555/66666666-6666-6666-6666-666666666666" }],
        suggestion: "Query the Pin with schema.",
      },
    ],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: "assets = target { domain: asset }\n\nquery assets\nassets \"BP_Door\"",
  });
  const text = allText(result);

  assert.equal(result.isError, true);
  assert.match(text, /at: line 4, column 9, length 12/);
  assert.match(text, /path: \["object","statements",2\]/);
  assert.match(text, /domain: graph/);
  assert.match(text, /operation: connect/);
  assert.match(text, /ref: @33333333-3333-3333-3333-333333333333\/44444444-4444-4444-4444-444444444444/);
  assert.match(text, /expected: \{"graph":"22222222-2222-2222-2222-222222222222"\}/);
  assert.match(text, /actual: null/);
  assert.match(text, /supported: \["pin","node"\]/);
  assert.match(text, /matches: \[\{"id":"55555555-5555-5555-5555-555555555555\/66666666-6666-6666-6666-666666666666"\}\]/);
  assert.match(text, /next: Query the Pin with schema\./);
});

test("arbitrary diagnostic text cannot break the SAL comment envelope", async () => {
  const rpc = new MockRpc({
    targetContext: "domain_root",
    target: assetRootTarget,
    object: emptyObjectResult.object,
    diagnostics: [
      {
        severity: "error",
        code: "native.message",
        message: "before\n\n###\nafter",
      },
    ],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: "assets = target { domain: asset }\n\nquery assets\nassets",
  });

  assert.equal(result.isError, true);
  assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
  assert.match(allText(result), /^# ###$/m);
});

test("sal_patch preserves Runtime RPC detail and mutation retry guidance", async () => {
  const rpc = new ThrowingRpc(new RuntimeRpcError(
    "resolution.target_not_found",
    "TARGET_NOT_FOUND",
    true,
    "The selected Graph no longer exists.",
  ));
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      `door = target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)}, id: ${JSON.stringify(blueprintId)} }`,
      "",
      "patch door",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  });
  const text = laterText(result);

  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.match(text, /ERROR resolution\.target_not_found: TARGET_NOT_FOUND/);
  assert.match(text, /detail: The selected Graph no longer exists\./);
  assert.match(text, /retryable: true/);
  assert.match(text, /Re-check the current Editor and object state before retrying/);
  assert.match(text, /Never blindly replay a Patch/);
});

test("does not expose arbitrary exception codes as public diagnostics", async () => {
  const error = Object.assign(new Error("unexpected client failure"), {
    code: "dependency.private_code",
  });
  const result = await new SalToolService(new ThrowingRpc(error)).call("editor", {});

  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.match(laterText(result), /ERROR runtime\.client_error: unexpected client failure/);
  assert.doesNotMatch(allText(result), /dependency\.private_code/);
});

test("sal_schema is local and does not call Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const service = new SalToolService(rpc);
  const graph = await service.call("sal_schema", { module: "graph" });
  const stateTree = await service.call("sal_schema", { module: "state_tree" });
  const level = await service.call("sal_schema", { module: "level" });
  const pcg = await service.call("sal_schema", { module: "pcg" });
  const pcgComponent = await service.call("sal_schema", { module: "pcg_component" });
  assert.equal(graph.isError, undefined);
  assert.match(graph.content[0].text, /^# graph$/m);
  assert.equal(stateTree.isError, undefined);
  assert.match(stateTree.content[0].text, /^# state_tree$/m);
  assert.equal(level.isError, undefined);
  assert.match(level.content[0].text, /^# level$/m);
  assert.match(level.content[0].text, /Patch Target for authored/);
  assert.equal(pcg.isError, undefined);
  assert.match(pcg.content[0].text, /^# pcg$/m);
  assert.match(pcg.content[0].text, /Patch Target for authored PCG Graph edits/);
  assert.equal(pcgComponent.isError, undefined);
  assert.match(pcgComponent.content[0].text, /^# pcg_component$/m);
  assert.match(pcgComponent.content[0].text, /accepts no Patch Target/);
  assert.equal(rpc.calls.length, 0);
});

test("agent_skill lists and loads the complete resident workflow without calling Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const service = new SalToolService(rpc);
  const list = await service.call("agent_skill", {});
  assert.equal(list.isError, undefined);
  assert.match(list.content[0].text, /^agent_skills:$/m);
  assert.match(list.content[0].text, /name: debug-unreal-pie-with-python/);
  assert.match(list.content[0].text, /name: format-unreal-blueprints/);
  assert.match(list.content[0].text, /name: use-unreal-python/);
  assert.match(list.content[0].text, /near-human visual quality/);

  const skill = await service.call("agent_skill", { name: "format-unreal-blueprints" });
  assert.equal(skill.isError, undefined);
  assert.deepEqual(
    skill.content.map(({ text }) => /^file: ([^\n]+)$/m.exec(text)?.[1]),
    [
      "SKILL.md",
      "references/golden-examples.md",
      "references/layout-rules.md",
      "references/loomle-sal-workflow.md",
      "references/topology-followups.md",
    ],
  );
  assert.match(allText(skill), /# Format Unreal Blueprints/);
  assert.match(allText(skill), /# Blueprint K2 Layout Rules/);
  assert.match(allText(skill), /# Loomle SAL Layout Workflow/);
  assert.match(allText(skill), /# Authorized Blueprint Topology Follow-ups/);
  assert.match(allText(skill), /ask for confirmation/);
  assert.match(allText(skill), /operation: "open"/);
  assert.match(allText(skill), /does\s+not prove that geometry is authoritative/);
  assert.match(allText(skill), /Never assume that\s+connecting another execution source replaces the existing incoming edge/);

  const pieSkill = await service.call("agent_skill", {
    name: "debug-unreal-pie-with-python",
  });
  assert.equal(pieSkill.isError, undefined);
  assert.deepEqual(
    pieSkill.content.map(({ text }) => /^file: ([^\n]+)$/m.exec(text)?.[1]),
    ["SKILL.md", "references/pie-python-patterns.md"],
  );
  assert.match(allText(pieSkill), /# Debug Unreal PIE with Python/);
  assert.match(allText(pieSkill), /Apply the resident `use-unreal-python` Skill first/);
  assert.match(allText(pieSkill), /Ask the user for\s+permission before requesting PIE/);
  assert.match(allText(pieSkill), /Never sleep, busy-wait/);
  assert.match(allText(pieSkill), /editor_request_begin_play/);
  assert.match(allText(pieSkill), /editor_request_end_play/);
  assert.match(allText(pieSkill), /does not advance PIE/);

  const pythonSkill = await service.call("agent_skill", {
    name: "use-unreal-python",
  });
  assert.equal(pythonSkill.isError, undefined);
  assert.deepEqual(
    pythonSkill.content.map(({ text }) => /^file: ([^\n]+)$/m.exec(text)?.[1]),
    [
      "SKILL.md",
      "references/capability-and-api-discovery.md",
      "references/continuation-and-recovery.md",
      "references/idempotent-mutation-and-verification.md",
    ],
  );
  assert.match(allText(pythonSkill), /# Use Unreal Python/);
  assert.match(allText(pythonSkill), /Call `status`/);
  assert.match(allText(pythonSkill), /stateMayHaveChanged/);
  assert.match(allText(pythonSkill), /applied`, `notApplied`, or `unknown`/);
  assert.match(allText(pythonSkill), /save the exact asset or package explicitly/);
  assert.match(allText(pythonSkill), /debug-unreal-pie-with-python/);
  assert.equal(rpc.calls.length, 0);
});

test("agent_skill rejects unknown names and extra arguments locally", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const service = new SalToolService(rpc);
  const unknown = await service.call("agent_skill", { name: "missing" });
  const extra = await service.call("agent_skill", { unexpected: true });
  assert.equal(unknown.isError, true);
  assert.match(unknown.content[0].text, /Unknown Loomle Agent Skill: missing/);
  assert.equal(extra.isError, true);
  assert.match(extra.content[0].text, /agent_skill does not accept: unexpected/);
  assert.equal(rpc.calls.length, 0);
});

test("editor context formats the validated ObjectResult", async () => {
  const rpc = new MockRpc({
    targetContext: "exact_target",
    target: {
      alias: "editorTarget",
      target: graphTarget.target,
    },
    object: {
      statements: [
        {
          target: { kind: "local", name: "eventGraph" },
          value: {
            kind: "object",
            semanticTag: "graph_record",
            fields: { id: graphId },
          },
        },
        { kind: "comment", text: "Blueprint Graph" },
      ],
    },
    diagnostics: [],
  });
  const result = await new SalToolService(rpc).call("editor", {});

  assert.equal(result.isError, undefined);
  assert.equal(result.content[0].text, [
    "result exact_target",
    `target editorTarget = target {domain: graph, asset: ${JSON.stringify(blueprintAsset)}, blueprintId: ${JSON.stringify(blueprintId)}, id: ${JSON.stringify(graphId)}}`,
    "objects",
    `eventGraph = graph_record {id: ${JSON.stringify(graphId)}}`,
    "# Blueprint Graph",
  ].join("\n"));
  assert.deepEqual(rpc.calls, [{ tool: "editor.context", args: {} }]);
});

test("editor defaults to context and matches the explicit context operation", async () => {
  const response = {
    targetContext: "exact_target",
    target: { alias: "editorTarget", target: graphTarget.target },
    object: { statements: [{ kind: "comment", text: "Blueprint Graph" }] },
    diagnostics: [],
  };
  const defaultRpc = new MockRpc(response);
  const explicitRpc = new MockRpc(response);
  const defaultResult = await new SalToolService(defaultRpc).call("editor", {});
  const explicitResult = await new SalToolService(explicitRpc).call("editor", {
    operation: "context",
  });

  assert.deepEqual(defaultResult, explicitResult);
  assert.equal(defaultResult.content.length, 1);
  assert.deepEqual(defaultRpc.calls, [{ tool: "editor.context", args: {} }]);
  assert.deepEqual(explicitRpc.calls, [{ tool: "editor.context", args: {} }]);
});

test("editor open parses a canonical SAL Target and formats the terminal outcome", async () => {
  const rpc = new MockRpc({
    subject: {
      targetContext: "exact_target",
      target: { alias: "editorTarget", target: graphTarget.target },
      diagnostics: [],
    },
    outcome: { operation: "open", status: "opened" },
  });
  const controller = new AbortController();
  const result = await new SalToolService(rpc).call("editor", {
    operation: "open",
    target: [
      "target {",
      "  domain: graph,",
      `  asset: ${JSON.stringify(blueprintAsset)},`,
      `  blueprintId: ${JSON.stringify(blueprintId)},`,
      `  id: ${JSON.stringify(graphId)}`,
      "}",
    ].join("\n"),
  }, controller.signal);

  assert.equal(result.isError, undefined);
  assert.equal(result.content.length, 2);
  assert.equal(result.content[0].text, [
    "result exact_target",
    `target editorTarget = target {domain: graph, asset: ${JSON.stringify(blueprintAsset)}, blueprintId: ${JSON.stringify(blueprintId)}, id: ${JSON.stringify(graphId)}}`,
    "no_objects",
  ].join("\n"));
  assert.equal(result.content[1].text, [
    "###",
    "Editor result",
    "operation: open",
    "status: opened",
    "###",
  ].join("\n"));
  assert.deepEqual(rpc.calls, [{
    tool: "editor.open",
    args: { target: graphTarget.target },
    signal: controller.signal,
  }]);
});

test("editor close preserves exact content identity when already closed", async () => {
  const rpc = new MockRpc({
    subject: {
      targetContext: "exact_target",
      target: { alias: "editorTarget", target: blueprintTarget.target },
      diagnostics: [],
    },
    outcome: { operation: "close", status: "already_closed" },
  });
  const result = await new SalToolService(rpc).call("editor", {
    operation: "close",
    target: `target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)}, id: ${JSON.stringify(blueprintId)} }`,
  });

  assert.equal(result.isError, undefined);
  assert.match(result.content[0].text, /^result exact_target$/m);
  assert.match(result.content[1].text, /^operation: close$/m);
  assert.match(result.content[1].text, /^status: already_closed$/m);
  assert.deepEqual(rpc.calls, [{
    tool: "editor.close",
    args: { target: blueprintTarget.target },
  }]);
});

test("editor rejects invalid argument combinations before calling Bridge", async () => {
  for (const args of [
    { target: "target { domain: blueprint }" },
    { operation: "context", target: "target { domain: blueprint }" },
    { operation: "open" },
    { operation: "close", target: "" },
    { operation: "focus" },
    { operation: "open", target: "target { domain: blueprint }", dryRun: true },
  ]) {
    const rpc = new MockRpc(emptyObjectResult);
    const result = await new SalToolService(rpc).call("editor", args);
    assert.equal(result.isError, true);
    assertUnresolvedResultFirstBlock(result);
    assert.match(laterText(result), /tool\.invalid_arguments/);
    assert.equal(rpc.calls.length, 0);
  }
});

test("editor rejects non-canonical Target Text locally with a failed outcome", async () => {
  for (const target of [
    `door = target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)}, id: ${JSON.stringify(blueprintId)} }`,
    `target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)} }`,
    "target { domain: asset, path: \"/Game/BP_Door.BP_Door\" }",
  ]) {
    const rpc = new MockRpc(emptyObjectResult);
    const result = await new SalToolService(rpc).call("editor", {
      operation: "open",
      target,
    });
    assert.equal(result.isError, true);
    assertUnresolvedResultFirstBlock(result);
    assert.equal(result.content.length, 3);
    assert.match(result.content[1].text, /^operation: open$/m);
    assert.match(result.content[1].text, /^status: failed$/m);
    assert.match(result.content[2].text, /ERROR language\./);
    assert.equal(rpc.calls.length, 0);
  }
});

test("editor keeps an exact Target when a control operation fails", async () => {
  const rpc = new MockRpc({
    subject: {
      targetContext: "exact_target",
      target: { alias: "editorTarget", target: graphTarget.target },
      diagnostics: [{
        severity: "error",
        code: "runtime.editor_blocked_by_modal",
        message: "A modal window blocks Graph focus.",
      }],
    },
    outcome: { operation: "open", status: "failed" },
  });
  const result = await new SalToolService(rpc).call("editor", {
    operation: "open",
    target: `target { domain: graph, asset: ${JSON.stringify(blueprintAsset)}, blueprintId: ${JSON.stringify(blueprintId)}, id: ${JSON.stringify(graphId)} }`,
  });

  assert.equal(result.isError, true);
  assert.match(result.content[0].text, /^result exact_target$/m);
  assert.match(result.content[1].text, /^status: failed$/m);
  assert.match(result.content[2].text, /ERROR runtime\.editor_blocked_by_modal/);
});

test("editor gives presentation-specific retry guidance for runtime failures", async () => {
  const rpc = new ThrowingRpc(new RuntimeRpcError(
    "runtime.connection_closed",
    "The Editor response was lost.",
    true,
  ));
  const result = await new SalToolService(rpc).call("editor", {
    operation: "open",
    target: `target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)}, id: ${JSON.stringify(blueprintId)} }`,
  });

  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.match(result.content[1].text, /^status: failed$/m);
  assert.match(result.content[2].text, /Call editor with no arguments/);
  assert.match(result.content[2].text, /idempotent requested postconditions/);
  assert.doesNotMatch(result.content[2].text, /blindly replay a Patch/);
});

test("editor context also gives presentation-specific retry guidance", async () => {
  const error = new RuntimeRpcError(
    "runtime.connection_closed",
    "The Editor is offline.",
    true,
  );
  const result = await new SalToolService(new ThrowingRpc(error)).call("editor", {});

  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.equal(result.content.length, 2);
  assert.match(result.content[1].text, /Call editor with no arguments/);
  assert.doesNotMatch(result.content[1].text, /blindly replay a Patch/);
});

test("editor fails closed on malformed or expanded private wrappers", async () => {
  const subject = {
    targetContext: "exact_target",
    target: { alias: "editorTarget", target: blueprintTarget.target },
    diagnostics: [],
  };
  for (const response of [
    { subject, outcome: { operation: "close", status: "opened" } },
    { subject, outcome: { operation: "close", status: "closed", phase: "done" } },
    { subject, outcome: { operation: "close", status: "closed" }, operationId: "private" },
  ]) {
    const result = await new SalToolService(new MockRpc(response)).call("editor", {
      operation: "close",
      target: `target { domain: blueprint, asset: ${JSON.stringify(blueprintAsset)}, id: ${JSON.stringify(blueprintId)} }`,
    });

    assert.equal(result.isError, true);
    assertUnresolvedResultFirstBlock(result);
    assert.match(result.content[1].text, /^status: failed$/m);
    assert.match(result.content[2].text, /ERROR runtime\.client_error/);
  }
});

test("empty result envelopes remain valid SAL Result Text", async () => {
  for (const [response, section] of [
    [
      { targetContext: "domain_root", target: assetRootTarget, diagnostics: [] },
      "no_objects",
    ],
    [
      {
        targetContext: "exact_target",
        target: blueprintTarget,
        object: { statements: [] },
        diagnostics: [],
      },
      "objects",
    ],
  ] as const) {
    const result = await new SalToolService(new MockRpc(response)).call("editor", {});
    assert.match(result.content[0].text, /^result (?:domain_root|exact_target)\n/);
    assert.match(result.content[0].text, new RegExp(`\\n${section}$`));
    assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
  }
});

test("rejects extra public tool arguments", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("editor", { unexpected: true });
  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.match(laterText(result), /tool\.invalid_arguments/);
  assert.equal(rpc.calls.length, 0);
});
