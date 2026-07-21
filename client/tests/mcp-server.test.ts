import assert from "node:assert/strict";
import { guide } from "@loomle/interfaces";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { ErrorCode, McpError } from "@modelcontextprotocol/sdk/types.js";
import test from "node:test";
import { productVersion } from "../src/generated/product-version.js";
import { createMcpServer } from "../src/mcp-server.js";
import { RuntimeRpcError, type RpcInvoker } from "../src/runtime-rpc.js";
import { SalToolService } from "../src/tools.js";

class CountingRpc implements RpcInvoker {
  calls = 0;

  async invoke(): Promise<unknown> {
    this.calls += 1;
    return {};
  }
}

class CancellableRpc implements RpcInvoker {
  readonly started: Promise<void>;
  aborted = false;
  private resolveStarted!: () => void;

  constructor() {
    this.started = new Promise((resolve) => {
      this.resolveStarted = resolve;
    });
  }

  invoke(
    _tool: string,
    _args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<unknown> {
    assert.ok(signal, "MCP handler must forward its cancellation signal");
    this.resolveStarted();
    return new Promise((_, reject) => {
      const abort = () => {
        this.aborted = true;
        reject(new RuntimeRpcError("runtime.request_cancelled", "cancelled"));
      };
      if (signal.aborted) abort();
      else signal.addEventListener("abort", abort, { once: true });
    });
  }
}

test("publishes the interface guide exactly once and rejects unknown tools before dispatch", async () => {
  const rpc = new CountingRpc();
  const server = await createMcpServer(new SalToolService(rpc));
  const client = new Client({ name: "loomle-test", version: "1.0.0" });
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await server.connect(serverTransport);
  await client.connect(clientTransport);

  try {
    assert.deepEqual(client.getServerVersion(), {
      name: "loomle",
      version: productVersion,
    });
    assert.equal(client.getInstructions(), undefined);
    const tools = await client.listTools();
    const schema = tools.tools.find((tool) => tool.name === "sal_schema");
    assert.equal(schema?.description, guide);
    assert.equal(
      tools.tools.filter((tool) => tool.description?.includes(guide)).length,
      1,
    );
    await assert.rejects(
      client.callTool({ name: "missing_tool", arguments: {} }),
      (error: unknown) => error instanceof McpError
        && error.code === ErrorCode.InvalidParams
        && error.message.includes("Unknown Loomle tool: missing_tool"),
    );
    assert.equal(rpc.calls, 0);
  } finally {
    await client.close();
    await server.close();
  }
});

test("forwards MCP request cancellation to the runtime invocation", async () => {
  const rpc = new CancellableRpc();
  const server = await createMcpServer(new SalToolService(rpc));
  const client = new Client({ name: "loomle-test", version: "1.0.0" });
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await server.connect(serverTransport);
  await client.connect(clientTransport);
  const controller = new AbortController();

  try {
    const call = client.callTool({
      name: "sal_query",
      arguments: { text: "query asset\nassets \"BP_Door\"" },
    }, undefined, { signal: controller.signal });
    await rpc.started;
    controller.abort();
    await assert.rejects(call, (error: unknown) => (
      error instanceof McpError && error.message.includes("AbortError")
    ));
    await waitFor(() => rpc.aborted);
  } finally {
    await client.close();
    await server.close();
  }
});

async function waitFor(predicate: () => boolean): Promise<void> {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (predicate()) return;
    await new Promise<void>((resolve) => setTimeout(resolve, 1));
  }
  assert.fail("Timed out waiting for MCP cancellation.");
}
