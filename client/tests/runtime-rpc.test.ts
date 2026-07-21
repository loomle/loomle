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

test("reports request timeouts and cancels the matching runtime invocation", async () => {
  const requestSocket = new MockRuntimeSocket(() => undefined);
  let connections = 0;
  const client = new RuntimeRpcClient("mock", async () => {
    connections += 1;
    return requestSocket;
  }, 5);
  const keepAlive = setTimeout(() => {}, 100);

  try {
    await assert.rejects(
      client.invoke("sal.query", {}),
      (error: unknown) => error instanceof RuntimeRpcError
        && error.code === "runtime.request_timeout"
        && error.retryable
        && error.message.includes("5ms"),
    );
    await waitFor(() => requestSocket.requests.length === 2);
    const invokeParams = requestSocket.requests[0].params as Record<string, unknown>;
    const cancelParams = requestSocket.requests[1].params as Record<string, unknown>;
    assert.equal(requestSocket.requests[1].method, "rpc.cancel");
    assert.equal(cancelParams.cancellationToken, invokeParams.cancellationToken);
    assert.match(String(cancelParams.cancellationToken), /^[0-9a-f-]{36}$/);
    assert.equal(connections, 1);
  } finally {
    clearTimeout(keepAlive);
    client.close();
  }
});

test("propagates AbortSignal cancellation over the live Bridge connection", async () => {
  const requestSocket = new MockRuntimeSocket(() => undefined);
  let connections = 0;
  const client = new RuntimeRpcClient("mock", async () => {
    connections += 1;
    return requestSocket;
  });
  const controller = new AbortController();

  const invocation = client.invoke("sal.query", { object: { kind: "query" } }, controller.signal);
  await waitFor(() => requestSocket.requests.length === 1);
  controller.abort();

  await assert.rejects(
    invocation,
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.request_cancelled"
      && !error.retryable,
  );
  await waitFor(() => requestSocket.requests.length === 2);
  const invokeParams = requestSocket.requests[0].params as Record<string, unknown>;
  const cancelParams = requestSocket.requests[1].params as Record<string, unknown>;
  assert.equal(requestSocket.requests[1].method, "rpc.cancel");
  assert.equal(cancelParams.cancellationToken, invokeParams.cancellationToken);
  assert.match(String(cancelParams.cancellationToken), /^[0-9a-f-]{36}$/);
  assert.equal(connections, 1);
  client.close();
});

test("does not dispatch or cancel a request whose signal is already aborted", async () => {
  const socket = new MockRuntimeSocket(() => undefined);
  let connections = 0;
  const client = new RuntimeRpcClient("mock", async () => {
    connections += 1;
    return socket;
  });
  const controller = new AbortController();
  controller.abort();

  await assert.rejects(
    client.invoke("sal.query", {}, controller.signal),
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.request_cancelled",
  );
  assert.equal(connections, 0);
  assert.deepEqual(socket.requests, []);
  client.close();
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
  const requestId = String(socket.requests[0].id);
  assert.match(requestId, new RegExp(`^loomle-ts-${process.pid}-\\d+-[0-9a-f-]+-1$`));
  assert.deepEqual(socket.requests[0], {
    jsonrpc: "2.0",
    id: requestId,
    method: "rpc.invoke",
    params: {
      tool: "sal.query",
      args: { object: { kind: "query" } },
      cancellationToken: (socket.requests[0].params as Record<string, unknown>).cancellationToken,
    },
  });
  assert.match(
    String((socket.requests[0].params as Record<string, unknown>).cancellationToken),
    /^[0-9a-f-]{36}$/,
  );
  client.close();
});

test("uses unique request ids across RuntimeRpcClient instances", async () => {
  const makeSocket = () => new MockRuntimeSocket((request) => ({
    jsonrpc: "2.0",
    id: request.id,
    result: { ok: true, payload: {} },
  }));
  const firstSocket = makeSocket();
  const secondSocket = makeSocket();
  const first = new RuntimeRpcClient("first", async () => firstSocket);
  const second = new RuntimeRpcClient("second", async () => secondSocket);

  await Promise.all([
    first.invoke("sal.query", {}),
    second.invoke("sal.query", {}),
  ]);

  const firstId = String(firstSocket.requests[0].id);
  const secondId = String(secondSocket.requests[0].id);
  assert.notEqual(firstId, secondId);
  assert.match(firstId, /-1$/);
  assert.match(secondId, /-1$/);
  first.close();
  second.close();
});

test("a client closed during connect cannot be revived by a late socket", async () => {
  let resolveFirst!: (socket: Duplex) => void;
  const firstConnection = new Promise<Duplex>((resolve) => {
    resolveFirst = resolve;
  });
  const staleSocket = new MockRuntimeSocket(() => undefined);
  const liveSocket = new MockRuntimeSocket((request) => ({
    jsonrpc: "2.0",
    id: request.id,
    result: { ok: true, payload: { endpoint: "live" } },
  }));
  let connections = 0;
  const client = new RuntimeRpcClient("mock", async () => {
    connections += 1;
    return connections === 1 ? firstConnection : liveSocket;
  });

  const staleRequest = client.invoke("sal.query", {});
  await waitFor(() => connections === 1);
  client.close();

  assert.deepEqual(await client.invoke("sal.query", {}), { endpoint: "live" });
  resolveFirst(staleSocket);
  await assert.rejects(
    staleRequest,
    (error: unknown) => error instanceof RuntimeRpcError
      && error.code === "runtime.connection_closed",
  );
  assert.equal(staleSocket.destroyed, true);
  assert.deepEqual(await client.invoke("sal.query", {}), { endpoint: "live" });
  assert.equal(connections, 2);
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

test("retries capabilities after a transient request failure", async () => {
  let capabilityRequests = 0;
  const socket = new MockRuntimeSocket((request) => {
    if (request.method === "rpc.capabilities") {
      capabilityRequests += 1;
      if (capabilityRequests === 1) {
        return {
          jsonrpc: "2.0",
          id: request.id,
          error: { code: 1003, message: "temporarily unavailable" },
        };
      }
      return {
        jsonrpc: "2.0",
        id: request.id,
        result: { tools: ["sal.query", "sal.patch", "editor.context"] },
      };
    }
    return undefined;
  });
  const client = new RuntimeRpcClient("mock", async () => socket);

  await assert.rejects(client.requireTools(["sal.query"]));
  await client.requireTools(["sal.query"]);
  assert.equal(capabilityRequests, 2);
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

async function waitFor(predicate: () => boolean): Promise<void> {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (predicate()) return;
    await new Promise<void>((resolve) => setTimeout(resolve, 1));
  }
  assert.fail("Timed out waiting for asynchronous RPC activity.");
}
