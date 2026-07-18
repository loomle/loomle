import assert from "node:assert/strict";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import {
  RuntimeSelectionError,
  loadRuntimeRecords,
  resolveRuntime,
  type RuntimeRecord,
} from "../src/runtime-discovery.js";

async function fixture(records: unknown[]): Promise<string> {
  const home = await mkdtemp(resolve(tmpdir(), "loomle-client-discovery-"));
  const directory = resolve(home, ".loomle", "state", "runtimes");
  await mkdir(directory, { recursive: true });
  await Promise.all(records.map((record, index) => writeFile(
    resolve(directory, `${index}.json`),
    JSON.stringify(record),
  )));
  await writeFile(resolve(directory, "invalid.json"), "not json");
  return home;
}

const alpha: RuntimeRecord = {
  runtimeId: "alpha-id",
  name: "Alpha",
  projectRoot: "/Projects/Alpha",
  endpoint: "/tmp/alpha.sock",
};
const beta: RuntimeRecord = {
  runtimeId: "beta-id",
  name: "Beta",
  projectRoot: "/Projects/Beta",
  endpoint: "/tmp/beta.sock",
};

test("loads valid runtime records and ignores malformed files", async () => {
  const home = await fixture([alpha, { runtimeId: "bad", endpoint: "/tmp/bad" }]);
  assert.deepEqual(await loadRuntimeRecords(home), [alpha]);
});

test("prefers explicit runtime selection", async () => {
  const home = await fixture([alpha, beta]);
  const selected = await resolveRuntime({
    homeDirectory: home,
    cwd: "/Elsewhere",
    env: { LOOMLE_RUNTIME_ID: "beta-id" },
    endpointAvailable: async () => true,
  });
  assert.equal(selected.runtimeId, "beta-id");
});

test("selects the online runtime containing cwd", async () => {
  const home = await fixture([alpha, beta]);
  const selected = await resolveRuntime({
    homeDirectory: home,
    cwd: "/Projects/Alpha/Source/Game",
    env: {},
    endpointAvailable: async () => true,
  });
  assert.equal(selected.runtimeId, "alpha-id");
});

test("does not guess between multiple online runtimes", async () => {
  const home = await fixture([alpha, beta]);
  await assert.rejects(
    resolveRuntime({
      homeDirectory: home,
      cwd: "/Elsewhere",
      env: {},
      endpointAvailable: async () => true,
    }),
    (error: unknown) => error instanceof RuntimeSelectionError
      && error.code === "runtime.ambiguous"
      && error.candidates.length === 2,
  );
});

test("does not guess between duplicate records for the same project", async () => {
  const duplicate = { ...alpha, runtimeId: "alpha-second", endpoint: "/tmp/alpha-2.sock" };
  const home = await fixture([alpha, duplicate]);
  await assert.rejects(
    resolveRuntime({
      homeDirectory: home,
      cwd: "/Projects/Alpha/Source",
      env: {},
      endpointAvailable: async () => true,
    }),
    (error: unknown) => error instanceof RuntimeSelectionError
      && error.code === "runtime.ambiguous"
      && error.candidates.length === 2,
  );
});

test("reports an explicitly requested offline runtime", async () => {
  const home = await fixture([alpha]);
  await assert.rejects(
    resolveRuntime({
      homeDirectory: home,
      env: { LOOMLE_RUNTIME_ID: "alpha-id" },
      endpointAvailable: async () => false,
    }),
    (error: unknown) => error instanceof RuntimeSelectionError
      && error.code === "runtime.offline",
  );
});
