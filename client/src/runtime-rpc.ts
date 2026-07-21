import { randomUUID } from "node:crypto";
import { createConnection } from "node:net";
import type { Duplex } from "node:stream";

let runtimeClientSequence = 0;

export interface RpcInvoker {
  invoke(tool: string, args: Record<string, unknown>, signal?: AbortSignal): Promise<unknown>;
}

export type EndpointConnector = (endpoint: string) => Promise<Duplex>;

// Bridge game-thread dispatch can spend up to 120 seconds producing its own
// structured timeout response. The transport timeout must leave enough room
// for that response to be serialized and delivered.
export const DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS = 130_000;

export class RuntimeRpcError extends Error {
  constructor(
    readonly code: string,
    message: string,
    readonly retryable = false,
    readonly detail?: string,
  ) {
    super(message);
    this.name = "RuntimeRpcError";
  }
}

interface PendingRequest {
  resolve(value: unknown): void;
  reject(error: Error): void;
  timer: NodeJS.Timeout;
  signal?: AbortSignal;
  abortListener?: () => void;
}

interface JsonRpcResponse {
  id?: string | number | null;
  result?: unknown;
  error?: {
    code?: number;
    message?: string;
    data?: {
      code?: string;
      retryable?: boolean;
      detail?: string;
    };
  };
}

export class RuntimeRpcClient implements RpcInvoker {
  private socket?: Duplex;
  private connecting?: Promise<Duplex>;
  private buffer = "";
  private sequence = 0;
  private connectionGeneration = 0;
  private readonly instanceNonce = `${++runtimeClientSequence}-${randomUUID()}`;
  private readonly pending = new Map<string, PendingRequest>();
  private capabilitiesPromise?: Promise<ReadonlySet<string>>;

  constructor(
    readonly endpoint: string,
    private readonly connector: EndpointConnector = connectEndpoint,
    private readonly requestTimeoutMs = DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS,
  ) {}

  async invoke(
    tool: string,
    args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<unknown> {
    const cancellationToken = randomUUID();
    const result = await this.request(
      "rpc.invoke",
      { tool, args, cancellationToken },
      signal,
      cancellationToken,
    );
    if (!isRecord(result) || result.ok !== true) {
      throw new RuntimeRpcError(
        "runtime.invalid_invoke_result",
        `rpc.invoke for ${tool} returned an invalid result envelope.`,
      );
    }
    return result.payload ?? {};
  }

  async requireTools(required: readonly string[]): Promise<void> {
    const capabilities = this.capabilitiesPromise ?? this.loadCapabilities();
    this.capabilitiesPromise = capabilities;
    let tools: ReadonlySet<string>;
    try {
      tools = await capabilities;
    } catch (error) {
      if (this.capabilitiesPromise === capabilities) {
        this.capabilitiesPromise = undefined;
      }
      throw error;
    }
    const missing = required.filter((tool) => !tools.has(tool));
    if (missing.length > 0) {
      throw new RuntimeRpcError(
        "runtime.incompatible",
        `The selected Loomle runtime does not support: ${missing.join(", ")}. Upgrade to a LoomleBridge version that provides these tools.`,
        false,
      );
    }
  }

  async request(
    method: string,
    params: Record<string, unknown>,
    signal?: AbortSignal,
    cancellationToken?: string,
  ): Promise<unknown> {
    throwIfAborted(signal);
    const socket = await this.ensureConnected();
    throwIfAborted(signal);
    const id = `loomle-ts-${process.pid}-${this.instanceNonce}-${++this.sequence}`;
    const line = `${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`;

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        const pending = this.takePending(id);
        if (!pending) return;
        if (cancellationToken) this.sendCancellation(cancellationToken);
        pending.reject(new RuntimeRpcError(
          "runtime.request_timeout",
          `Loomle runtime did not answer ${method} within ${this.requestTimeoutMs}ms.`,
          true,
        ));
      }, this.requestTimeoutMs);
      timer.unref?.();
      const abortListener = signal ? () => {
        const pending = this.takePending(id);
        if (!pending) return;
        if (cancellationToken) this.sendCancellation(cancellationToken);
        pending.reject(new RuntimeRpcError(
          "runtime.request_cancelled",
          `Loomle runtime request ${method} was cancelled.`,
          false,
        ));
      } : undefined;
      this.pending.set(id, { resolve, reject, timer, signal, abortListener });
      if (signal && abortListener) {
        signal.addEventListener("abort", abortListener, { once: true });
      }

      if (signal?.aborted && abortListener) {
        abortListener();
        return;
      }

      socket.write(line, (error?: Error | null) => {
        if (!error) return;
        const pending = this.takePending(id);
        if (!pending) return;
        pending.reject(new RuntimeRpcError(
          "runtime.write_failed",
          "Failed to write a request to the Loomle runtime.",
          true,
          error.message,
        ));
      });
    });
  }

  close(): void {
    this.connectionGeneration += 1;
    const error = new RuntimeRpcError(
      "runtime.connection_closed",
      "The Loomle runtime connection was closed.",
      true,
    );
    this.failPending(error);
    this.socket?.destroy();
    this.socket = undefined;
    this.connecting = undefined;
    this.capabilitiesPromise = undefined;
    this.buffer = "";
  }

  private async loadCapabilities(): Promise<ReadonlySet<string>> {
    const result = await this.request("rpc.capabilities", {});
    if (!isRecord(result) || !Array.isArray(result.tools)
      || !result.tools.every((tool) => typeof tool === "string")) {
      throw new RuntimeRpcError(
        "runtime.incompatible",
        "The selected Loomle runtime returned an invalid capability document.",
      );
    }
    return new Set(result.tools);
  }

  private async ensureConnected(): Promise<Duplex> {
    if (this.socket && !this.socket.destroyed) return this.socket;
    if (this.connecting) return this.connecting;

    const generation = this.connectionGeneration;
    let connection: Promise<Duplex>;
    connection = this.connector(this.endpoint).then((socket) => {
      if (generation !== this.connectionGeneration) {
        socket.destroy();
        throw new RuntimeRpcError(
          "runtime.connection_closed",
          "The Loomle runtime connection was closed before setup completed.",
          true,
        );
      }
      this.attach(socket);
      return socket;
    }).catch((error: unknown) => {
      if (error instanceof RuntimeRpcError) {
        throw error;
      }
      throw new RuntimeRpcError(
        "runtime.connect_failed",
        `Failed to connect to the Loomle runtime at ${this.endpoint}.`,
        true,
        errorMessage(error),
      );
    }).finally(() => {
      if (this.connecting === connection) {
        this.connecting = undefined;
      }
    });
    this.connecting = connection;
    return connection;
  }

  private attach(socket: Duplex): void {
    this.socket = socket;
    this.buffer = "";
    socket.setEncoding("utf8");
    socket.on("data", (chunk: string | Buffer) => this.onData(socket, chunk.toString()));
    socket.on("error", (error) => this.onDisconnect(socket, new RuntimeRpcError(
      "runtime.connection_error",
      "The Loomle runtime connection failed.",
      true,
      error.message,
    )));
    socket.on("close", () => this.onDisconnect(socket, new RuntimeRpcError(
      "runtime.connection_closed",
      "The Loomle runtime connection closed before all responses arrived.",
      true,
    )));
  }

  private onData(socket: Duplex, chunk: string): void {
    if (socket !== this.socket) return;
    this.buffer += chunk;
    while (true) {
      const newline = this.buffer.indexOf("\n");
      if (newline < 0) return;
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (!line) continue;

      let response: JsonRpcResponse;
      try {
        response = JSON.parse(line) as JsonRpcResponse;
      } catch (error) {
        this.onDisconnect(socket, new RuntimeRpcError(
          "runtime.invalid_json",
          "The Loomle runtime returned invalid JSON.",
          false,
          errorMessage(error),
        ));
        socket.destroy();
        return;
      }
      this.dispatch(response);
    }
  }

  private dispatch(response: JsonRpcResponse): void {
    if (response.id === undefined || response.id === null) return;
    const id = String(response.id);
    const pending = this.takePending(id);
    if (!pending) return;

    if (response.error) {
      pending.reject(new RuntimeRpcError(
        typeof response.error.data?.code === "string" && response.error.data.code.length > 0
          ? response.error.data.code
          : "runtime.rpc_error",
        response.error.message ?? "The Loomle runtime rejected the request.",
        response.error.data?.retryable ?? false,
        response.error.data?.detail,
      ));
      return;
    }
    if (!("result" in response)) {
      pending.reject(new RuntimeRpcError(
        "runtime.invalid_response",
        "The Loomle runtime response has neither result nor error.",
      ));
      return;
    }
    pending.resolve(response.result);
  }

  private onDisconnect(socket: Duplex, error: RuntimeRpcError): void {
    if (socket !== this.socket) return;
    this.socket = undefined;
    this.capabilitiesPromise = undefined;
    this.buffer = "";
    this.failPending(error);
  }

  private failPending(error: RuntimeRpcError): void {
    for (const id of [...this.pending.keys()]) {
      const pending = this.takePending(id);
      if (!pending) continue;
      pending.reject(error);
    }
  }

  private takePending(id: string): PendingRequest | undefined {
    const pending = this.pending.get(id);
    if (!pending) return undefined;
    this.pending.delete(id);
    clearTimeout(pending.timer);
    if (pending.signal && pending.abortListener) {
      pending.signal.removeEventListener("abort", pending.abortListener);
    }
    return pending;
  }

  private sendCancellation(cancellationToken: string): void {
    // The Bridge keeps reading this connection while provider work runs and
    // handles rpc.cancel synchronously, so the control message can overtake
    // the queued/running game-thread request without a second connection.
    const socket = this.socket;
    if (!socket || socket.destroyed) {
      return;
    }
    const cancelRequestId = `loomle-ts-${process.pid}-${this.instanceNonce}-cancel-${randomUUID()}`;
    const line = `${JSON.stringify({
      jsonrpc: "2.0",
      id: cancelRequestId,
      method: "rpc.cancel",
      params: { cancellationToken },
    })}\n`;

    socket.write(line, (error?: Error | null) => {
      if (error && socket === this.socket) {
        socket.destroy(error);
      }
    });
  }
}

export async function connectEndpoint(endpoint: string): Promise<Duplex> {
  return new Promise((resolve, reject) => {
    const socket = createConnection({ path: endpoint });
    const onError = (error: Error) => reject(error);
    socket.once("error", onError);
    socket.once("connect", () => {
      socket.off("error", onError);
      resolve(socket);
    });
  });
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function throwIfAborted(signal?: AbortSignal): void {
  if (!signal?.aborted) return;
  throw new RuntimeRpcError(
    "runtime.request_cancelled",
    "Loomle runtime request was cancelled before dispatch.",
    false,
  );
}
