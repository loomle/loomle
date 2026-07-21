import { resolveRuntime, type RuntimeDiscoveryOptions } from "./runtime-discovery.js";
import { RuntimeRpcClient, RuntimeRpcError, type RpcInvoker } from "./runtime-rpc.js";

const requiredTools = ["sal.query", "sal.patch", "editor.context"] as const;

interface RuntimeClient extends RpcInvoker {
  readonly endpoint: string;
  requireTools(required: readonly string[]): Promise<void>;
  close(): void;
}

type RuntimeClientFactory = (endpoint: string) => RuntimeClient;

export class DiscoveredRuntimeInvoker implements RpcInvoker {
  private client?: RuntimeClient;

  constructor(
    private readonly discovery: RuntimeDiscoveryOptions = {},
    private readonly createClient: RuntimeClientFactory = (endpoint) => new RuntimeRpcClient(endpoint),
  ) {}

  async invoke(
    tool: string,
    args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<unknown> {
    if (signal?.aborted) {
      throw new RuntimeRpcError(
        "runtime.request_cancelled",
        "Loomle runtime request was cancelled before runtime discovery.",
      );
    }
    const runtime = await resolveRuntime(this.discovery);
    if (!this.client || this.client.endpoint !== runtime.endpoint) {
      this.client?.close();
      this.client = this.createClient(runtime.endpoint);
    }
    const client = this.client;

    // Never replay a lost request automatically. In particular, a Patch may
    // have reached UE even if its response was lost.
    try {
      await client.requireTools(requiredTools);
      return await client.invoke(tool, args, signal);
    } catch (error) {
      // Another concurrent invocation may already have discarded this client
      // or selected a different runtime. Never close that newer connection or
      // mask the original failure by dereferencing mutable shared state.
      if (this.client === client && isFatalRuntimeFailure(error)) {
        client.close();
        this.client = undefined;
      }
      throw error;
    }
  }

  close(): void {
    this.client?.close();
    this.client = undefined;
  }
}

function isFatalRuntimeFailure(error: unknown): boolean {
  if (!(error instanceof RuntimeRpcError)) return true;
  return new Set<number | string>([
    "runtime.connect_failed",
    "runtime.connection_error",
    "runtime.connection_closed",
    "runtime.write_failed",
    "runtime.invalid_json",
    "runtime.invalid_response",
    "runtime.invalid_invoke_result",
    "runtime.incompatible",
  ]).has(error.code);
}
