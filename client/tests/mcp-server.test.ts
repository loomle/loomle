import assert from "node:assert/strict";
import { guide } from "@loomle/interfaces";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { ErrorCode, McpError } from "@modelcontextprotocol/sdk/types.js";
import test from "node:test";
import { createMcpServer } from "../src/mcp-server.js";
import type { RpcInvoker } from "../src/runtime-rpc.js";
import { SalToolService } from "../src/tools.js";

class CountingRpc implements RpcInvoker {
  calls = 0;

  async invoke(): Promise<unknown> {
    this.calls += 1;
    return {};
  }
}

test("publishes the interface guide and rejects unknown MCP tools before service dispatch", async () => {
  const rpc = new CountingRpc();
  const server = await createMcpServer(new SalToolService(rpc));
  const client = new Client({ name: "loomle-test", version: "1.0.0" });
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await server.connect(serverTransport);
  await client.connect(clientTransport);

  try {
    assert.equal(client.getInstructions(), guide);
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
