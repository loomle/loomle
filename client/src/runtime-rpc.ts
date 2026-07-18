import { createConnection } from "node:net";
import type { Duplex } from "node:stream";

export interface RpcInvoker {
  invoke(tool: string, args: Record<string, unknown>): Promise<unknown>;
}

export type EndpointConnector = (endpoint: string) => Promise<Duplex>;

// Bridge game-thread dispatch can spend up to 120 seconds producing its own
// structured timeout response. The transport timeout must leave enough room
// for that response to be serialized and delivered.
export const DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS = 130_000;

export class RuntimeRpcError extends Error {
  constructor(
    readonly code: number | string,
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
}

interface JsonRpcResponse {
  id?: string | number | null;
  result?: unknown;
  error?: {
    code?: number;
    message?: string;
    data?: {
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
  private readonly pending = new Map<string, PendingRequest>();
  private capabilitiesPromise?: Promise<ReadonlySet<string>>;

  constructor(
    readonly endpoint: string,
    private readonly connector: EndpointConnector = connectEndpoint,
    private readonly requestTimeoutMs = DEFAULT_RUNTIME_REQUEST_TIMEOUT_MS,
  ) {}

  async invoke(tool: string, args: Record<string, unknown>): Promise<unknown> {
    const result = await this.request("rpc.invoke", { tool, args });
    if (!isRecord(result) || result.ok !== true) {
      throw new RuntimeRpcError(
        "runtime.invalid_invoke_result",
        `rpc.invoke for ${tool} returned an invalid result envelope.`,
      );
    }
    return result.payload ?? {};
  }

  async requireTools(required: readonly string[]): Promise<void> {
    this.capabilitiesPromise ??= this.loadCapabilities();
    const tools = await this.capabilitiesPromise;
    const missing = required.filter((tool) => !tools.has(tool));
    if (missing.length > 0) {
      throw new RuntimeRpcError(
        "runtime.incompatible",
        `The selected Loomle runtime does not support: ${missing.join(", ")}. Update LoomleBridge to the matching 0.7 version.`,
        false,
      );
    }
  }

  async request(method: string, params: Record<string, unknown>): Promise<unknown> {
    const socket = await this.ensureConnected();
    const id = `loomle-ts-${process.pid}-${++this.sequence}`;
    const line = `${JSON.stringify({ jsonrpc: "2.0", id, method, params })}\n`;

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new RuntimeRpcError(
          "runtime.request_timeout",
          `Loomle runtime did not answer ${method} within ${this.requestTimeoutMs}ms.`,
          true,
        ));
      }, this.requestTimeoutMs);
      timer.unref?.();
      this.pending.set(id, { resolve, reject, timer });

      socket.write(line, (error?: Error | null) => {
        if (!error) return;
        const pending = this.pending.get(id);
        if (!pending) return;
        this.pending.delete(id);
        clearTimeout(pending.timer);
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

    this.connecting = this.connector(this.endpoint).then((socket) => {
      this.attach(socket);
      return socket;
    }).catch((error: unknown) => {
      throw new RuntimeRpcError(
        "runtime.connect_failed",
        `Failed to connect to the Loomle runtime at ${this.endpoint}.`,
        true,
        errorMessage(error),
      );
    }).finally(() => {
      this.connecting = undefined;
    });
    return this.connecting;
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
    const pending = this.pending.get(id);
    if (!pending) return;
    this.pending.delete(id);
    clearTimeout(pending.timer);

    if (response.error) {
      pending.reject(new RuntimeRpcError(
        response.error.code ?? "runtime.rpc_error",
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
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
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
