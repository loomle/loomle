import assert from "node:assert/strict";
import { mkdir, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import {
  discoverProjectAtRoot,
  discoverProjects,
  loadProjectRecords,
  loadRuntimeRecords,
  projectForCwd,
  projectsForRoots,
  stableProjectId,
  type ProjectRecord,
  type RuntimeRecord,
} from "../src/runtime-discovery.js";

async function fixture(
  projects: unknown[] = [],
  runtimes: unknown[] = [],
): Promise<string> {
  const home = await mkdtemp(resolve(tmpdir(), "loomle-client-discovery-"));
  for (const [kind, records] of [["projects", projects], ["runtimes", runtimes]] as const) {
    const directory = resolve(home, ".loomle", "state", kind);
    await mkdir(directory, { recursive: true });
    await Promise.all(records.map((record, index) => writeFile(
      resolve(directory, `${index}.json`),
      JSON.stringify(record),
    )));
    await writeFile(resolve(directory, "invalid.json"), "not json");
  }
  return home;
}

const alphaProject: ProjectRecord = {
  projectId: stableProjectId("/Projects/Alpha"),
  name: "Alpha",
  projectRoot: "/Projects/Alpha",
  uproject: "/Projects/Alpha/Alpha.uproject",
};
const alphaRuntime: RuntimeRecord = {
  runtimeId: "alpha-runtime",
  projectId: alphaProject.projectId,
  name: "Alpha",
  projectRoot: alphaProject.projectRoot,
  endpoint: "/tmp/alpha.sock",
  protocolVersion: 2,
  pluginPath: "/UE/Engine/Plugins/Marketplace/LoomleBridge",
  pluginInstallScope: "engine",
  pluginManagedBy: "fab",
  pluginVersion: "0.7.0-rc.1",
};

test("loads project and runtime records independently and ignores malformed files", async () => {
  const home = await fixture(
    [alphaProject, { projectId: "bad" }],
    [alphaRuntime, { runtimeId: "bad", endpoint: "/tmp/bad" }],
  );
  assert.deepEqual(await loadProjectRecords(home), [alphaProject]);
  assert.deepEqual(await loadRuntimeRecords(home), [alphaRuntime]);
});

test("preserves Bridge installation facts in persistent project records", async () => {
  const project = {
    ...alphaProject,
    pluginPath: "/UE/Engine/Plugins/Marketplace/LoomleBridge",
    pluginInstallScope: "engine",
    pluginManagedBy: "fab",
    pluginVersion: "0.7.0-rc.1",
  };
  const home = await fixture([project], []);
  assert.deepEqual(await loadProjectRecords(home), [project]);
});

test("rejects records whose stored project identity does not match their root", async () => {
  const wrongProjectId = stableProjectId("/Projects/Beta");
  const home = await fixture([{
    ...alphaProject,
    projectId: wrongProjectId,
  }], [{
    ...alphaRuntime,
    projectId: wrongProjectId,
  }]);

  assert.deepEqual(await loadProjectRecords(home), []);
  assert.deepEqual(await loadRuntimeRecords(home), []);
  assert.deepEqual(await discoverProjects({ homeDirectory: home }), []);
});

test("merges persistent offline projects with transient runtime candidates", async () => {
  const betaRuntime = {
    ...alphaRuntime,
    runtimeId: "beta-runtime",
    projectId: stableProjectId("/Projects/Beta"),
    name: "Beta",
    projectRoot: "/Projects/Beta",
  };
  const home = await fixture([alphaProject], [alphaRuntime, betaRuntime]);
  const projects = await discoverProjects({ homeDirectory: home });

  assert.deepEqual(projects.map(({ projectId, runtimes }) => ({
    projectId,
    runtimes: runtimes.map((runtime) => runtime.runtimeId),
  })), [
    { projectId: alphaProject.projectId, runtimes: ["alpha-runtime"] },
    { projectId: stableProjectId("/Projects/Beta"), runtimes: ["beta-runtime"] },
  ]);
});

test("migrates legacy lowercase POSIX IDs in project and stale runtime records", async () => {
  const projectRoot = "/Projects/Game";
  const legacyProjectId = "cc93e5cab35f197b";
  const currentProjectId = "b4b194846d3b053b";
  const home = await fixture([{
    projectId: legacyProjectId,
    name: "Game",
    projectRoot,
  }], [{
    runtimeId: "legacy-runtime",
    projectId: legacyProjectId,
    name: "Game",
    projectRoot,
    endpoint: "/tmp/legacy-game.sock",
    protocolVersion: 1,
  }]);

  assert.equal((await loadProjectRecords(home, "darwin"))[0].projectId, currentProjectId);
  assert.equal((await loadRuntimeRecords(home, "darwin"))[0].projectId, currentProjectId);
  const projects = await discoverProjects({ homeDirectory: home, platform: "darwin" });
  assert.deepEqual(projects.map((project) => ({
    projectId: project.projectId,
    runtimes: project.runtimes.map((runtime) => runtime.runtimeId),
  })), [{ projectId: currentProjectId, runtimes: ["legacy-runtime"] }]);
});

test("legacy migration does not merge distinct case-sensitive POSIX projects", async () => {
  const upperRoot = "/Projects/Game";
  const lowerRoot = "/Projects/game";
  const home = await fixture([{
    projectId: "cc93e5cab35f197b",
    name: "GameUpper",
    projectRoot: upperRoot,
  }, {
    projectId: stableProjectId(lowerRoot, "linux"),
    name: "GameLower",
    projectRoot: lowerRoot,
  }]);

  const projects = await discoverProjects({ homeDirectory: home, platform: "linux" });
  assert.deepEqual(
    new Set(projects.map((project) => project.projectRoot)),
    new Set([upperRoot, lowerRoot]),
  );
  assert.notEqual(projects[0].projectId, projects[1].projectId);
});

test("discovers a valid offline project root and computes the Bridge-stable ID", async () => {
  const root = await mkdtemp(resolve(tmpdir(), "loomle-project-"));
  await writeFile(resolve(root, "Game.uproject"), "{}");
  const project = await discoverProjectAtRoot(root);

  assert.equal(project?.name, "Game");
  assert.equal(project?.projectRoot, root);
  assert.equal(project?.projectId, stableProjectId(root));
  assert.equal(
    stableProjectId("/Users/gao/Dev/ProjectOdyssey", "darwin"),
    "085fc4e32c582270",
  );
});

test("Windows project IDs match UE's normalized forward-slash path", () => {
  assert.equal(
    stableProjectId("C:\\Projects\\Game", "win32"),
    stableProjectId("c:/Projects/Game/", "win32"),
  );
  assert.equal(
    stableProjectId("C:\\Projects\\Game", "win32"),
    "991397ad6b7a080a",
  );
});

test("POSIX project IDs preserve case-sensitive project identity", () => {
  assert.equal(stableProjectId("/Projects/Game", "linux"), "b4b194846d3b053b");
  assert.equal(stableProjectId("/Projects/Game", "darwin"), "b4b194846d3b053b");
  assert.notEqual(
    stableProjectId("/Projects/Game", "linux"),
    stableProjectId("/Projects/game", "linux"),
  );
  assert.notEqual(
    stableProjectId("/Projects/Game", "darwin"),
    stableProjectId("/Projects/game", "darwin"),
  );
});

test("rejects a root that does not contain exactly one uproject", async () => {
  const root = await mkdtemp(resolve(tmpdir(), "loomle-project-invalid-"));
  assert.equal(await discoverProjectAtRoot(root), undefined);
  await writeFile(resolve(root, "One.uproject"), "{}");
  await writeFile(resolve(root, "Two.uproject"), "{}");
  assert.equal(await discoverProjectAtRoot(root), undefined);
});

test("MCP roots select one project but preserve ambiguity for a monorepo", async () => {
  const projects = [
    { ...alphaProject, runtimes: [] },
    {
      projectId: stableProjectId("/Workspace/Beta"),
      name: "Beta",
      projectRoot: "/Workspace/Beta",
      runtimes: [],
    },
  ];
  assert.deepEqual(
    projectsForRoots(projects, ["/Projects/Alpha/Source"]).map((project) => project.projectId),
    [alphaProject.projectId],
  );
  assert.deepEqual(
    projectsForRoots(projects, ["/Projects", "/Workspace"]).map((project) => project.projectId),
    [alphaProject.projectId, stableProjectId("/Workspace/Beta")],
  );
});

test("cwd matching chooses the most specific containing project", () => {
  const projects = [
    {
      ...alphaProject,
      projectId: stableProjectId("/Projects"),
      projectRoot: "/Projects",
      runtimes: [],
    },
    { ...alphaProject, runtimes: [] },
  ];
  assert.equal(projectForCwd(projects, "/Projects/Alpha/Source")?.projectId, alphaProject.projectId);
  assert.equal(projectForCwd(projects, "/Elsewhere"), undefined);
});
