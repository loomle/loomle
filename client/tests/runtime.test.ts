import assert from "node:assert/strict";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import { DiscoveredRuntimeInvoker } from "../src/runtime.js";

const projectRoot = "/Projects/Alpha";

async function runtimeFixture(endpoint: string): Promise<{
  home: string;
  recordPath: string;
}> {
  const home = await mkdtemp(resolve(tmpdir(), "loomle-client-runtime-"));
  const directory = resolve(home, ".loomle", "state", "runtimes");
  await mkdir(directory, { recursive: true });
  const recordPath = resolve(directory, "alpha.json");
  await writeRuntimeRecord(recordPath, endpoint);
  return { home, recordPath };
}

async function writeRuntimeRecord(path: string, endpoint: string): Promise<void> {
  await writeFile(path, JSON.stringify({
    runtimeId: "alpha-id",
    projectRoot,
    endpoint,
  }));
}

function discovery(homeDirectory: string) {
  return {
    homeDirectory,
    cwd: `${projectRoot}/Source`,
    env: { LOOMLE_RUNTIME_ID: "alpha-id" },
    endpointAvailable: async () => true,
  };
}

class ConcurrentFailureClient {
  closeCount = 0;
  readonly ready: Promise<void>;
  private resolveReady!: () => void;
  private readonly pending: Array<{ error: Error; reject(error: Error): void }> = [];

  constructor(readonly endpoint: string) {
    this.ready = new Promise((resolveReady) => {
      this.resolveReady = resolveReady;
    });
  }

  async requireTools(): Promise<void> {}

  invoke(tool: string): Promise<unknown> {
    return new Promise((_, reject) => {
      this.pending.push({ error: new Error(`failed:${tool}`), reject });
      if (this.pending.length === 2) this.resolveReady();
    });
  }

  rejectAll(): void {
    for (const pending of this.pending.splice(0)) pending.reject(pending.error);
  }

  close(): void {
    this.closeCount += 1;
  }
}

class SwitchableClient {
  closeCount = 0;
  readonly started: Promise<void>;
  private resolveStarted!: () => void;
  private rejectPending?: (error: Error) => void;

  constructor(readonly endpoint: string, private readonly waitsForClose: boolean) {
    this.started = new Promise((resolveStarted) => {
      this.resolveStarted = resolveStarted;
    });
  }

  async requireTools(): Promise<void> {}

  invoke(): Promise<unknown> {
    this.resolveStarted();
    if (!this.waitsForClose) return Promise.resolve({ endpoint: this.endpoint });
    return new Promise((_, reject) => {
      this.rejectPending = reject;
    });
  }

  close(): void {
    this.closeCount += 1;
    this.rejectPending?.(new Error(`closed:${this.endpoint}`));
    this.rejectPending = undefined;
  }
}

test("parallel failures preserve both original errors and close one shared client once", async () => {
  const { home } = await runtimeFixture("alpha-endpoint");
  const client = new ConcurrentFailureClient("alpha-endpoint");
  const invoker = new DiscoveredRuntimeInvoker(discovery(home), () => client);

  const settled = Promise.allSettled([
    invoker.invoke("first", {}),
    invoker.invoke("second", {}),
  ]);
  await client.ready;
  client.rejectAll();
  const results = await settled;

  assert.deepEqual(results.map((result) => result.status), ["rejected", "rejected"]);
  assert.deepEqual(results.map((result) => result.status === "rejected"
    ? (result.reason as Error).message
    : "fulfilled"), ["failed:first", "failed:second"]);
  assert.equal(client.closeCount, 1);
});

test("a stale failing invocation never closes a newly selected endpoint", async () => {
  const { home, recordPath } = await runtimeFixture("alpha-endpoint");
  const alpha = new SwitchableClient("alpha-endpoint", true);
  const beta = new SwitchableClient("beta-endpoint", false);
  const clients = new Map([
    [alpha.endpoint, alpha],
    [beta.endpoint, beta],
  ]);
  const invoker = new DiscoveredRuntimeInvoker(discovery(home), (endpoint) => {
    const client = clients.get(endpoint);
    assert.ok(client, `unexpected endpoint ${endpoint}`);
    return client;
  });

  const firstFailure = assert.rejects(
    invoker.invoke("first", {}),
    /closed:alpha-endpoint/,
  );
  await alpha.started;
  await writeRuntimeRecord(recordPath, beta.endpoint);

  assert.deepEqual(await invoker.invoke("second", {}), { endpoint: beta.endpoint });
  await firstFailure;
  assert.equal(alpha.closeCount, 1);
  assert.equal(beta.closeCount, 0);

  invoker.close();
  assert.equal(beta.closeCount, 1);
});
