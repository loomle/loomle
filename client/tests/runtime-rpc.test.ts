import assert from "node:assert/strict";
import { Duplex } from "node:stream";
import test from "node:test";
import {
  DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS,
  RuntimeRpcClient,
  RuntimeRpcError,
} from "../src/runtime-rpc.js";

class MockRuntimeSocket extends Duplex {
  readonly requests: Array<Record<string, unknown>> = [];

  constructor(private readonly respond: (request: Record<string, unknown>) => unknown) {
    super();
  }

  override _read(): void {}

  override _write(
    chunk: Buffer | string,
    _encoding: BufferEncoding,
    callback: (error?: Error | null) => void,
  ): void {
    const request = JSON.parse(chunk.toString().trim()) as Record<string, unknown>;
    this.requests.push(request);
    const response = this.respond(request);
    if (response !== undefined) {
      queueMicrotask(() => this.push(`${JSON.stringify(response)}\n`));
    }
    callback();
  }
}

test("leaves transport timeout headroom beyond Bridge game-thread dispatch", () => {
  assert.equal(DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS, 130_000);
  assert.ok(DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS > 120_000);
});

test("reports request timeouts as retryable runtime failures", async () => {
  const socket = new MockRuntimeSocket(() => undefined);
  const client = new RuntimeRpcClient("mock", async () => socket, 5);
  const keepAlive = setTimeout(() => {}, 100);

  try {
    await assert.rejects(
      client.request("rpc.health", {}),
      (error: unknown) => error instanceof RuntimeRpcError
        && error.code === "runtime.request_timeout"
        && error.retryable
        && error.message.includes("5ms"),
    );
  } finally {
    clearTimeout(keepAlive);
    client.close();
  }
});

test("invokes Bridge tools through rpc.invoke and unwraps payload", async () => {
  const socket = new MockRuntimeSocket((request) => ({
    jsonrpc: "2.0",
    id: request.id,
    result: { ok: true, payload: { diagnostics: [] } },
  }));
  const client = new RuntimeRpcClient("mock", async () => socket);

  assert.deepEqual(await client.invoke("sal.query", { object: { kind: "query" } }), {
    diagnostics: [],
  });
  assert.deepEqual(socket.requests[0], {
    jsonrpc: "2.0",
    id: `loomle-ts-${process.pid}-1`,
    method: "rpc.invoke",
    params: {
      tool: "sal.query",
      args: { object: { kind: "query" } },
    },
  });
  client.close();
});

test("preserves structured Bridge RPC errors", async () => {
  const socket = new MockRuntimeSocket((request) => ({
    jsonrpc: "2.0",
    id: request.id,
    error: {
      code: 1002,
      message: "TARGET_NOT_FOUND",
      data: { retryable: false, detail: "missing graph" },
    },
  }));
  const client = new RuntimeRpcClient("mock", async () => socket);

  await assert.rejects(
    client.invoke("sal.query", {}),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === 1002
      && error.detail === "missing graph"
      && !error.retryable,
  );
  client.close();
});

test("checks and caches required Bridge capabilities per connection", async () => {
  const socket = new MockRuntimeSocket((request) => {
    if (request.method === "rpc.capabilities") {
      return {
        jsonrpc: "2.0",
        id: request.id,
        result: { tools: ["sal.query", "sal.patch", "editor.context"] },
      };
    }
    return { jsonrpc: "2.0", id: request.id, result: { ok: true, payload: {} } };
  });
  const client = new RuntimeRpcClient("mock", async () => socket);

  await client.requireTools(["sal.query", "sal.patch", "editor.context"]);
  await client.requireTools(["sal.query"]);
  assert.equal(socket.requests.filter((request) => request.method === "rpc.capabilities").length, 1);
  client.close();
});

test("rejects an incompatible Bridge before invoking a tool", async () => {
  const socket = new MockRuntimeSocket((request) => ({
    jsonrpc: "2.0",
    id: request.id,
    result: { tools: ["sal.query", "sal.patch"] },
  }));
  const client = new RuntimeRpcClient("mock", async () => socket);

  await assert.rejects(
    client.requireTools(["sal.query", "sal.patch", "editor.context"]),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.incompatible"
      && error.message === (
        "The selected Loomle runtime does not support: editor.context. "
        + "Upgrade to a LoomleBridge version that provides these tools."
      ),
  );
  client.close();
});
