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

test("exposes only the six public Loomle tools", () => {
  assert.deepEqual(toolDefinitions.map((tool) => tool.name), [
    "status",
    "project",
    "sal_query",
    "sal_patch",
    "sal_schema",
    "editor_context",
  ]);
});

test("status reports identity, binding, Bridge health, and Windows update guidance", async () => {
  const status: StatusProvider = {
    async report() {
      return {
        client: {
          version: "0.7.0-rc.1",
          pid: 1234,
          platform: "win32",
          target: "win32-x64",
          executable: "C:/Loomle/loomle.exe",
        },
        update: {
          status: "available",
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
          pid: 1234,
          platform: "darwin",
          target: "darwin-arm64",
          executable: "/Loomle/loomle",
        },
        update: { status: "available", version: "0.7.0-rc.2" },
        session: { status: "unbound" },
      };
    },
  };
  const result = await new SalToolService(new MockRpc(emptyObjectResult), status)
    .call("status", {});

  assert.match(result.content[0].text, /ensure affected Unreal Editors are closed/);
  assert.doesNotMatch(result.content[0].text, /Stop-Process/);
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
      .filter((tool) => tool.name !== "sal_schema")
      .every((tool) => tool.description.length < 300),
  );
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

test("preserves Runtime RPC detail and retry guidance after unresolved Result Text", async () => {
  const rpc = new ThrowingRpc(new RuntimeRpcError(
    "resolution.target_not_found",
    "TARGET_NOT_FOUND",
    true,
    "The selected Graph no longer exists.",
  ));
  const result = await new SalToolService(rpc).call("editor_context", {});
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
  const result = await new SalToolService(new ThrowingRpc(error)).call("editor_context", {});

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
  assert.equal(graph.isError, undefined);
  assert.match(graph.content[0].text, /^# graph$/m);
  assert.equal(stateTree.isError, undefined);
  assert.match(stateTree.content[0].text, /^# state_tree$/m);
  assert.equal(rpc.calls.length, 0);
});

test("editor_context formats the same validated ObjectResult", async () => {
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
  const result = await new SalToolService(rpc).call("editor_context", {});

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
    const result = await new SalToolService(new MockRpc(response)).call("editor_context", {});
    assert.match(result.content[0].text, /^result (?:domain_root|exact_target)\n/);
    assert.match(result.content[0].text, new RegExp(`\\n${section}$`));
    assert.deepEqual(parseSalResultText(result.content[0].text).diagnostics, []);
  }
});

test("rejects extra public tool arguments", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("editor_context", { target: "guess" });
  assert.equal(result.isError, true);
  assertUnresolvedResultFirstBlock(result);
  assert.match(laterText(result), /tool\.invalid_arguments/);
  assert.equal(rpc.calls.length, 0);
});
