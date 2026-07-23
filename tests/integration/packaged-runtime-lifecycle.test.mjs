import assert from "node:assert/strict";
import { mkdtemp } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";
import {
  PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS,
  runPackagedRuntimeLifecycle,
  stableProjectId,
} from "./packaged-runtime-lifecycle.mjs";

const fixture = Object.freeze({
  blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
  blueprintDescription: "Loomle packaged E2E baseline",
  assetType: "/Script/Engine.Blueprint",
  assetSearchText: "BP_LoomleE2E",
  assetRoot: "/Game",
});

test("stable project identity matches normalized platform path semantics", () => {
  assert.equal(
    stableProjectId("/Projects/Loomle", "darwin"),
    stableProjectId("/Projects/Loomle/", "darwin"),
  );
  assert.notEqual(
    stableProjectId("/Projects/Loomle", "darwin"),
    stableProjectId("/Projects/loomle", "darwin"),
  );
  assert.equal(
    stableProjectId("C:\\Projects\\Loomle", "win32"),
    stableProjectId("c:\\projects\\loomle", "win32"),
  );
});

test("real Client process survives restart, stale record, and in-flight disconnect", async () => {
  const stateRoot = await mkdtemp(
    join(tmpdir(), "loomle-packaged-runtime-lifecycle-"),
  );
  const scenarioEvents = [];
  const result = await runPackagedRuntimeLifecycle({
    projectRoot: resolve("test-project"),
    fixture,
    stateRoot,
    clientExecutable: process.execPath,
    clientArguments: [
      resolve("client", "dist", "main.cjs"),
      "mcp",
    ],
    clientWorkingDirectory: resolve("."),
    platform: process.platform,
    onScenario: (event) => scenarioEvents.push(event),
  });

  assert.equal(result.status, "passed");
  assert.deepEqual(
    result.scenarios.map((scenario) => scenario.name),
    PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS,
  );
  assert.ok(result.scenarios.every((scenario) => (
    scenario.status === "passed"
  )));
  assert.deepEqual(
    scenarioEvents.map(({ name, status }) => ({ name, status })),
    PACKAGED_RUNTIME_LIFECYCLE_SCENARIOS.flatMap((name) => [
      { name, status: "started" },
      { name, status: "passed" },
    ]),
  );
});
