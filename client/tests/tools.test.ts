import assert from "node:assert/strict";
import { guide } from "@loomle/interfaces";
import { parseSalObject } from "@loomle/sal";
import test from "node:test";
import { RuntimeRpcError, type RpcInvoker } from "../src/runtime-rpc.js";
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

const emptyObjectResult = {
  object: {
    statements: [
      { kind: "comment", text: "result" },
    ],
  },
  diagnostics: [],
};

test("exposes only the four public SAL tools", () => {
  assert.deepEqual(toolDefinitions.map((tool) => tool.name), [
    "sal_query",
    "sal_patch",
    "sal_schema",
    "editor_context",
  ]);
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
      .every((tool) => tool.description.length < 200),
  );
});

test("sal_query parses and normalizes Text before invoking Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const service = new SalToolService(rpc);
  const result = await service.call("sal_query", {
    text: "query asset\nassets \"BP_Door\"",
  });

  assert.equal(result.isError, undefined);
  assert.equal(result.content[0].text, "# result");
  assert.equal("structuredContent" in result, false);
  assert.equal(rpc.calls.length, 1);
  assert.equal(rpc.calls[0].tool, "sal.query");
  assert.deepEqual(rpc.calls[0].args, {
    object: {
      kind: "query",
      target: { alias: "asset", value: { kind: "name", name: "asset" } },
      operation: { kind: "assets", text: "BP_Door" },
    },
  });
  assert.equal(rpc.calls[0].signal, undefined);
});

test("sal_query forwards the MCP AbortSignal through the SAL executor", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const controller = new AbortController();
  await new SalToolService(rpc).call("sal_query", {
    text: "query asset\nassets \"BP_Door\"",
  }, controller.signal);

  assert.equal(rpc.calls.length, 1);
  assert.equal(rpc.calls[0].signal, controller.signal);
});

test("sal_query preserves multiline node diagnostics as ordered Object Text comments", async () => {
  const rpc = new MockRpc({
    object: {
      statements: [
        {
          target: { kind: "local", name: "g" },
          value: { kind: "call", callee: "graph", args: { id: "G1" } },
        },
        {
          target: { kind: "local", name: "Get_Participant_Entry" },
          value: {
            kind: "call",
            callee: "node",
            args: {
              graph: { kind: "local", name: "g" },
              id: "N1",
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
            kind: "call",
            callee: "pin",
            args: {
              id: "P1",
              direction: { kind: "name", name: "in" },
            },
          },
        },
      ],
    },
    diagnostics: [],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: "g = graph(id: \"G1\")\n\nquery g\nnode@N1",
  });

  assert.equal(result.isError, undefined);
  assert.equal(result.content[0].text, [
    "g = graph(id: \"G1\")",
    "Get_Participant_Entry = node(graph: g, id: \"N1\", type: \"/Script/BlueprintGraph.K2Node_CallFunction\")",
    "# Get Participant Entry",
    "###",
    "UE node diagnostic: Error",
    "In use pin Controller no longer exists on node Get Participant Entry.",
    "Could not find a function named \"GetParticipantEntry\".",
    "###",
    "Get_Participant_Entry.Controller = pin(id: \"P1\", direction: in)",
  ].join("\n"));
  assert.deepEqual(parseSalObject(result.content[0].text).diagnostics, []);
});

test("invalid SAL never reaches Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("sal_query", { text: "not SAL" });
  assert.equal(result.isError, true);
  assert.equal(rpc.calls.length, 0);
});

test("sal_patch invokes the normalized mutation endpoint", async () => {
  const rpc = new MockRpc({
    ...emptyObjectResult,
    isError: false,
    dryRun: true,
    valid: true,
    applied: false,
    operation: "patch",
  });
  const controller = new AbortController();
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      "door = blueprint(asset: \"/Game/BP_Door.BP_Door\")",
      "",
      "patch door dry run",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  }, controller.signal);

  assert.equal(result.isError, undefined);
  assert.equal(rpc.calls[0].tool, "sal.patch");
  assert.equal(rpc.calls[0].signal, undefined);
  assert.equal((rpc.calls[0].args.object as { dryRun: boolean }).dryRun, true);
  assert.match(result.content[0].text, /dryRun: true/);
  assert.match(result.content[0].text, /###\nSAL result\n/);
  assert.deepEqual(parseSalObject(result.content[0].text).diagnostics, []);
});

test("sal_patch preserves a failed MutationResult as an agent-visible tool error", async () => {
  const rpc = new MockRpc({
    ...emptyObjectResult,
    diagnostics: [
      {
        severity: "error",
        code: "graph.operation_failed",
        message: "A later ordered operation failed.",
        operation: "connect",
        ref: "pin@target",
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
      "door = blueprint(asset: \"/Game/BP_Door.BP_Door\")",
      "",
      "patch door",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  });

  assert.equal(result.isError, true);
  assert.equal("structuredContent" in result, false);
  assert.match(result.content[0].text, /^# result/m);
  assert.match(result.content[0].text, /applied: true/);
  assert.match(result.content[0].text, /ERROR graph\.operation_failed/);
  assert.match(result.content[0].text, /operation: connect/);
  assert.match(result.content[0].text, /ref: pin@target/);
  assert.match(result.content[0].text, /###\nSAL diagnostics\n/);
  assert.deepEqual(parseSalObject(result.content[0].text).diagnostics, []);
});

test("preserves every structured execution field inside SAL comments", async () => {
  const rpc = new MockRpc({
    diagnostics: [],
    isError: false,
    dryRun: true,
    valid: true,
    applied: false,
    operation: "patch",
    resolvedRefs: { target: "node@N1" },
    planned: { operations: [{ kind: "set" }] },
    diff: { changed: ["NodeComment"] },
  });
  const result = await new SalToolService(rpc).call("sal_patch", {
    text: [
      "door = blueprint(asset: \"/Game/BP_Door.BP_Door\")",
      "",
      "patch door dry run",
      "set door.BlueprintDescription = \"Door\"",
    ].join("\n"),
  });

  assert.match(result.content[0].text, /resolvedRefs: \{"target":"node@N1"\}/);
  assert.match(result.content[0].text, /planned: \{"operations":\[\{"kind":"set"\}\]\}/);
  assert.match(result.content[0].text, /diff: \{"changed":\["NodeComment"\]\}/);
});

test("renders every populated diagnostic locator and expectation field", async () => {
  const rpc = new MockRpc({
    diagnostics: [
      {
        severity: "error",
        code: "graph.target_mismatch",
        message: "The selected Pin belongs to another Graph.",
        path: ["object", "statements", 2],
        span: { line: 4, column: 9, length: 12 },
        domain: "graph",
        operation: "connect",
        ref: "pin@target",
        expected: { graph: "graph@expected" },
        actual: null,
        supported: ["pin", "node"],
        matches: [{ id: "pin@candidate" }],
        suggestion: "Query the Pin with schema.",
      },
    ],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: "query asset\nassets \"BP_Door\"",
  });
  const text = result.content[0].text;

  assert.equal(result.isError, true);
  assert.match(text, /at: line 4, column 9, length 12/);
  assert.match(text, /path: \["object","statements",2\]/);
  assert.match(text, /domain: graph/);
  assert.match(text, /operation: connect/);
  assert.match(text, /ref: pin@target/);
  assert.match(text, /expected: \{"graph":"graph@expected"\}/);
  assert.match(text, /actual: null/);
  assert.match(text, /supported: \["pin","node"\]/);
  assert.match(text, /matches: \[\{"id":"pin@candidate"\}\]/);
  assert.match(text, /next: Query the Pin with schema\./);
});

test("arbitrary diagnostic text cannot break the SAL comment envelope", async () => {
  const rpc = new MockRpc({
    diagnostics: [
      {
        severity: "error",
        code: "native.message",
        message: "before\n\n###\nafter",
      },
    ],
  });
  const result = await new SalToolService(rpc).call("sal_query", {
    text: "query asset\nassets",
  });

  assert.equal(result.isError, true);
  assert.deepEqual(parseSalObject(result.content[0].text).diagnostics, []);
  assert.match(result.content[0].text, /^# ###$/m);
});

test("preserves Runtime RPC detail and retry guidance in primary text", async () => {
  const rpc = new ThrowingRpc(new RuntimeRpcError(
    1002,
    "TARGET_NOT_FOUND",
    true,
    "The selected Graph no longer exists.",
  ));
  const result = await new SalToolService(rpc).call("editor_context", {});
  const text = result.content[0].text;

  assert.equal(result.isError, true);
  assert.match(text, /ERROR 1002: TARGET_NOT_FOUND/);
  assert.match(text, /detail: The selected Graph no longer exists\./);
  assert.match(text, /retryable: true/);
  assert.match(text, /Re-check the current Editor and object state before retrying/);
  assert.match(text, /Never blindly replay a Patch/);
});

test("sal_schema is local and does not call Bridge", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("sal_schema", { module: "graph" });
  assert.equal(result.isError, undefined);
  assert.match(result.content[0].text, /^# graph$/m);
  assert.equal(rpc.calls.length, 0);
});

test("editor_context formats the same validated ObjectResult", async () => {
  const rpc = new MockRpc({
    object: {
      statements: [
        {
          target: { kind: "local", name: "eventGraph" },
          value: {
            kind: "call",
            callee: "graph",
            args: { id: "graph-guid" },
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
    "eventGraph = graph(id: \"graph-guid\")",
    "# Blueprint Graph",
  ].join("\n"));
  assert.deepEqual(rpc.calls, [{ tool: "editor.context", args: {} }]);
});

test("empty result envelopes remain valid SAL comments", async () => {
  for (const response of [
    { diagnostics: [] },
    { object: { statements: [] }, diagnostics: [] },
  ]) {
    const result = await new SalToolService(new MockRpc(response)).call("editor_context", {});
    assert.equal(result.content[0].text, "###\nSAL returned no Object Text.\n###");
    assert.deepEqual(parseSalObject(result.content[0].text).diagnostics, []);
  }
});

test("rejects extra public tool arguments", async () => {
  const rpc = new MockRpc(emptyObjectResult);
  const result = await new SalToolService(rpc).call("editor_context", { target: "guess" });
  assert.equal(result.isError, true);
  assert.match(result.content[0].text, /tool\.invalid_arguments/);
  assert.equal(rpc.calls.length, 0);
});
