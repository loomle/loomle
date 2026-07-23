import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import {
  chmod,
  mkdir,
  mkdtemp,
  readFile,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";

import {
  RunnerUsageError,
  buildAutomationArguments,
  buildPackagedEditorArguments,
  buildRuntimeEnvironment,
  buildResultDocument,
  buildCrashRoots,
  classifyEditorLog,
  classifyPackagedE2EOutcome,
  classifyRunOutcome,
  collectRunMetadata,
  createGracefulInterruptHandler,
  createRuntimeStateHome,
  createOwnedEditorController,
  decideWorkspaceCleanup,
  hashCandidateArchive,
  installGracefulSignalHandlers,
  loadFixtureManifest,
  parseAutomationReportText,
  parseRunnerArgs,
  runAutomation,
  runPackagedE2E,
  summarizeAutomationReportObject,
  terminateOwnedProcess,
  validateBundledClient,
  validatePackagedPluginDirectoryCandidate,
  validatePluginDirectoryCandidate,
  waitForStableCrashSnapshot,
} from "./ue-test-runner.mjs";

const VALID_ARGUMENTS = [
  "--profile",
  "automation",
  "--ue-root",
  "UE_5.7",
  "--project-template",
  "fixtures/LoomleTestHost",
  "--plugin-dir",
  "build/LoomleBridge",
  "--output-dir",
  "results/automation",
  "--target",
  "darwin-arm64",
];

test("parses an explicit Automation compiled-plugin invocation", () => {
  const cwd = resolve("runner-fixture");
  const options = parseRunnerArgs([
    ...VALID_ARGUMENTS,
    "--phase-deadline-seconds=90.5",
    "--shutdown-grace-seconds",
    "3",
  ], { cwd });

  assert.deepEqual(options, {
    profile: "automation",
    ueRoot: resolve(cwd, "UE_5.7"),
    projectTemplate: resolve(cwd, "fixtures/LoomleTestHost"),
    pluginDir: resolve(cwd, "build/LoomleBridge"),
    outputDir: resolve(cwd, "results/automation"),
    target: "darwin-arm64",
    phaseDeadlineMs: 90500,
    shutdownGraceMs: 3000,
  });
});

test("parses the packaged_e2e profile through the same candidate contract", () => {
  const options = parseRunnerArgs(
    VALID_ARGUMENTS.map((argument) => (
      argument === "automation" ? "packaged_e2e" : argument
    )),
    { cwd: "/workspace" },
  );

  assert.equal(options.profile, "packaged_e2e");
  assert.equal(options.pluginDir, resolve("/workspace", "build/LoomleBridge"));
  assert.equal(options.projectTemplate, resolve(
    "/workspace",
    "fixtures/LoomleTestHost",
  ));
});

test("accepts an archive instead of a compiled plugin directory", () => {
  const argumentsWithArchive = [...VALID_ARGUMENTS];
  const directoryOption = argumentsWithArchive.indexOf("--plugin-dir");
  argumentsWithArchive.splice(
    directoryOption,
    2,
    "--plugin-archive",
    "candidate.zip",
  );

  const options = parseRunnerArgs(argumentsWithArchive, { cwd: "/workspace" });
  assert.equal(options.pluginDir, undefined);
  assert.equal(options.pluginArchive, resolve("/workspace", "candidate.zip"));
});

test("rejects ambiguous, incomplete, and unsupported invocations", () => {
  assert.throws(
    () => parseRunnerArgs([
      ...VALID_ARGUMENTS,
      "--plugin-archive",
      "candidate.zip",
    ]),
    /exactly one of --plugin-dir or --plugin-archive/,
  );
  assert.throws(
    () => parseRunnerArgs(
      VALID_ARGUMENTS.filter((argument) => argument !== "--plugin-dir"
        && argument !== "build/LoomleBridge"),
    ),
    /exactly one of --plugin-dir or --plugin-archive/,
  );
  assert.throws(
    () => parseRunnerArgs(
      VALID_ARGUMENTS.map((argument) => (
        argument === "darwin-arm64" ? "plan9-x64" : argument
      )),
    ),
    /unsupported target/,
  );
  const windowsOptions = parseRunnerArgs(
    VALID_ARGUMENTS.map((argument) => (
      argument === "darwin-arm64" ? "win32-x64" : argument
    )),
  );
  assert.equal(windowsOptions.target, "win32-x64");
  assert.throws(
    () => parseRunnerArgs([
      ...VALID_ARGUMENTS,
      "--phase-deadline-seconds",
      "0",
    ]),
    /must be a positive number/,
  );
  assert.throws(
    () => parseRunnerArgs([
      ...VALID_ARGUMENTS,
      "--unknown",
      "value",
    ]),
    RunnerUsageError,
  );
});

test("rejects a raw source plugin directory before launching Unreal", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-raw-plugin-"));
  const pluginDir = join(root, "LoomleBridge");
  await mkdir(join(pluginDir, "Source", "LoomleBridge"), { recursive: true });
  await writeFile(
    join(pluginDir, "LoomleBridge.uplugin"),
    `${JSON.stringify({
      FileVersion: 3,
      Modules: [{
        Name: "LoomleBridge",
        Type: "Editor",
        PlatformAllowList: ["Mac"],
      }],
    }, null, 2)}\n`,
  );

  try {
    await assert.rejects(
      validatePluginDirectoryCandidate(pluginDir, "darwin-arm64"),
      (error) => error instanceof RunnerUsageError
        && /not compiled for darwin-arm64/.test(error.message)
        && /UnrealEditor-LoomleBridge\.dylib/.test(error.message)
        && /not raw source/.test(error.message),
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("accepts a compiled plugin directory and validates every Mac Editor module", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-built-plugin-"));
  const pluginDir = join(root, "LoomleBridge");
  const binariesDir = join(pluginDir, "Binaries", "Mac");
  await mkdir(binariesDir, { recursive: true });
  await writeFile(
    join(pluginDir, "LoomleBridge.uplugin"),
    `${JSON.stringify({
      FileVersion: 3,
      Modules: [
        {
          Name: "LoomleBridge",
          Type: "Editor",
          PlatformAllowList: ["Mac"],
        },
        {
          Name: "LoomleEditorTools",
          Type: "EditorNoCommandlet",
          PlatformAllowList: ["Mac"],
        },
      ],
    }, null, 2)}\n`,
  );
  await writeFile(
    join(binariesDir, "UnrealEditor-LoomleBridge.dylib"),
    "compiled LoomleBridge",
  );

  try {
    await assert.rejects(
      validatePluginDirectoryCandidate(pluginDir, "darwin-arm64"),
      /UnrealEditor-LoomleEditorTools\.dylib/,
    );

    await writeFile(
      join(binariesDir, "UnrealEditor-LoomleEditorTools.dylib"),
      "compiled LoomleEditorTools",
    );
    assert.equal(
      await validatePluginDirectoryCandidate(pluginDir, "darwin-arm64"),
      join(pluginDir, "LoomleBridge.uplugin"),
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("accepts a compiled Win64 plugin and its target-bundled Client", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-win64-plugin-"));
  const pluginDir = join(root, "LoomleBridge");
  const binariesDir = join(pluginDir, "Binaries", "Win64");
  const clientPath = join(
    pluginDir,
    "Resources",
    "Loomle",
    "win32-x64",
    "loomle.exe",
  );
  await mkdir(binariesDir, { recursive: true });
  await mkdir(join(pluginDir, "Resources", "Loomle", "win32-x64"), {
    recursive: true,
  });
  await writeFile(
    join(pluginDir, "LoomleBridge.uplugin"),
    `${JSON.stringify({
      FileVersion: 3,
      VersionName: "0.7.0-test",
      Modules: [{
        Name: "LoomleBridge",
        Type: "Editor",
        PlatformAllowList: ["Win64"],
      }],
    }, null, 2)}\n`,
  );
  await writeFile(
    join(binariesDir, "UnrealEditor-LoomleBridge.dll"),
    "compiled LoomleBridge",
  );
  await writeFile(clientPath, "windows client");

  try {
    assert.deepEqual(
      await validatePackagedPluginDirectoryCandidate(
        pluginDir,
        "win32-x64",
      ),
      {
        descriptorPath: join(pluginDir, "LoomleBridge.uplugin"),
        versionName: "0.7.0-test",
        clientExecutable: clientPath,
      },
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("packaged candidate requires VersionName and its target-bundled Client", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-packaged-candidate-"));
  const pluginDir = join(root, "LoomleBridge");
  const binaryPath = join(
    pluginDir,
    "Binaries",
    "Mac",
    "UnrealEditor-LoomleBridge.dylib",
  );
  const clientPath = join(
    pluginDir,
    "Resources",
    "Loomle",
    "darwin-arm64",
    "loomle",
  );
  const descriptorPath = join(pluginDir, "LoomleBridge.uplugin");
  await mkdir(join(pluginDir, "Binaries", "Mac"), { recursive: true });
  await mkdir(join(pluginDir, "Resources", "Loomle", "darwin-arm64"), {
    recursive: true,
  });
  await writeFile(binaryPath, "compiled");
  await writeFile(clientPath, "#!/bin/sh\nexit 0\n");
  await chmod(clientPath, 0o755);
  const descriptor = {
    FileVersion: 3,
    Modules: [{
      Name: "LoomleBridge",
      Type: "Editor",
      PlatformAllowList: ["Mac"],
    }],
  };
  await writeFile(descriptorPath, `${JSON.stringify(descriptor, null, 2)}\n`);

  try {
    assert.equal(
      await validateBundledClient(pluginDir, "darwin-arm64"),
      clientPath,
    );
    await assert.rejects(
      validatePackagedPluginDirectoryCandidate(pluginDir, "darwin-arm64"),
      /non-empty VersionName/,
    );

    descriptor.VersionName = "0.7.0-test";
    await writeFile(descriptorPath, `${JSON.stringify(descriptor, null, 2)}\n`);
    assert.deepEqual(
      await validatePackagedPluginDirectoryCandidate(
        pluginDir,
        "darwin-arm64",
      ),
      {
        descriptorPath,
        versionName: "0.7.0-test",
        clientExecutable: clientPath,
      },
    );

    await rm(clientPath);
    await assert.rejects(
      validatePackagedPluginDirectoryCandidate(pluginDir, "darwin-arm64"),
      /Resources\/Loomle\/darwin-arm64\/loomle/,
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("loads schemaVersion 1 fixture manifest and derives search scope", async () => {
  const project = await mkdtemp(join(tmpdir(), "loomle-fixture-manifest-"));
  const manifestPath = join(project, "LoomleE2E.fixture.json");
  await writeFile(manifestPath, `${JSON.stringify({
    schemaVersion: 1,
    blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
    blueprintDescription: "Loomle packaged E2E baseline",
    assetType: "/Script/Engine.Blueprint",
  }, null, 2)}\n`);

  try {
    assert.deepEqual(await loadFixtureManifest(project), {
      schemaVersion: 1,
      blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
      blueprintDescription: "Loomle packaged E2E baseline",
      assetType: "/Script/Engine.Blueprint",
      assetSearchText: "BP_LoomleE2E",
      assetRoot: "/Game",
    });

    await writeFile(manifestPath, '{"schemaVersion":2}\n');
    await assert.rejects(
      loadFixtureManifest(project),
      /unsupported packaged E2E fixture schemaVersion 2/,
    );
  } finally {
    await rm(project, { recursive: true, force: true });
  }
});

test("refuses an existing output path without deleting its contents", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-ue-runner-output-"));
  const outputDir = join(root, "existing-result");
  const sentinel = join(outputDir, "keep.txt");
  await mkdir(outputDir);
  await writeFile(sentinel, "keep");

  try {
    await assert.rejects(
      runAutomation({
        profile: "automation",
        target: "darwin-arm64",
        ueRoot: join(root, "ue"),
        projectTemplate: join(root, "project-template"),
        pluginDir: join(root, "plugin"),
        outputDir,
        phaseDeadlineMs: 1000,
        shutdownGraceMs: 100,
      }, {
        platform: "darwin",
      }),
      (error) => error instanceof RunnerUsageError
        && /must not already exist/.test(error.message),
    );
    assert.equal(await readFile(sentinel, "utf8"), "keep");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("hashes an archive exactly once through the prepare-phase helper", async () => {
  const archivePath = "/candidate/LoomleBridge.zip";
  const metadata = await collectRunMetadata({
    repoRoot: "/repository-that-does-not-exist",
    target: "darwin-arm64",
    pluginArchive: archivePath,
  });
  assert.equal(metadata.archiveSha256, null);
  assert.deepEqual(metadata.candidate, {
    kind: "archive",
    path: archivePath,
  });

  let calls = 0;
  const controller = new AbortController();
  const digest = await hashCandidateArchive(
    archivePath,
    controller.signal,
    async (receivedPath, receivedSignal) => {
      calls += 1;
      assert.equal(receivedPath, archivePath);
      assert.equal(receivedSignal, controller.signal);
      return "archive-digest";
    },
  );
  assert.equal(digest, "archive-digest");
  assert.equal(calls, 1);
});

test("repeated interrupts remain handled while graceful cleanup completes", () => {
  const controller = new AbortController();
  const firstSignals = [];
  const repeatedSignals = [];
  const interrupt = createGracefulInterruptHandler(controller, {
    onInterrupt: (signalName) => firstSignals.push(signalName),
    onRepeatedInterrupt: (signalName, firstSignal) => {
      repeatedSignals.push([signalName, firstSignal]);
    },
  });

  assert.equal(interrupt("SIGINT"), true);
  assert.equal(controller.signal.aborted, true);
  assert.match(controller.signal.reason.message, /received SIGINT/);
  assert.equal(interrupt("SIGTERM"), false);
  assert.equal(interrupt("SIGINT"), false);
  assert.deepEqual(firstSignals, ["SIGINT"]);
  assert.deepEqual(repeatedSignals, [
    ["SIGTERM", "SIGINT"],
    ["SIGINT", "SIGINT"],
  ]);
});

test("CLI signal listeners stay installed through repeated cleanup signals", () => {
  const emitter = new EventEmitter();
  const controller = new AbortController();
  const repeatedSignals = [];
  const dispose = installGracefulSignalHandlers({
    emitter,
    controller,
    onRepeatedInterrupt: (signalName) => repeatedSignals.push(signalName),
  });

  assert.equal(emitter.listenerCount("SIGINT"), 1);
  assert.equal(emitter.listenerCount("SIGTERM"), 1);
  emitter.emit("SIGINT");
  emitter.emit("SIGTERM");
  emitter.emit("SIGINT");
  assert.equal(controller.signal.aborted, true);
  assert.deepEqual(repeatedSignals, ["SIGTERM", "SIGINT"]);
  assert.equal(emitter.listenerCount("SIGINT"), 1);
  assert.equal(emitter.listenerCount("SIGTERM"), 1);

  dispose();
  assert.equal(emitter.listenerCount("SIGINT"), 0);
  assert.equal(emitter.listenerCount("SIGTERM"), 0);
});

test("owned process-group cleanup outlives its leader and escalates to SIGKILL", async () => {
  const child = {
    pid: 41230,
    exitCode: null,
    signalCode: null,
  };
  let resolveCompletion;
  const completion = new Promise((resolvePromise) => {
    resolveCompletion = resolvePromise;
  });
  let groupAlive = true;
  let clock = 0;
  const signals = [];

  const result = await terminateOwnedProcess({
    child,
    ownedPid: child.pid,
    processGroupId: child.pid,
    completion,
    shutdownGraceMs: 10,
    signalProcess({ signalName }) {
      signals.push(signalName);
      if (signalName === "SIGTERM") {
        child.signalCode = "SIGTERM";
        resolveCompletion({
          exitCode: null,
          signalCode: "SIGTERM",
          spawnError: null,
        });
      } else {
        groupAlive = false;
      }
      return true;
    },
    groupExists: () => groupAlive,
    sleep: async (milliseconds) => {
      clock += milliseconds;
    },
    now: () => clock,
  });

  assert.deepEqual(signals, ["SIGTERM", "SIGKILL"]);
  assert.equal(result.stopped, true);
  assert.equal(result.signalDelivered, true);
  assert.equal(result.completionResult.signalCode, "SIGTERM");
});

test("owned process-group cleanup fails closed when SIGKILL leaves members", async () => {
  const child = {
    pid: 41231,
    exitCode: null,
    signalCode: null,
  };
  let clock = 0;
  const signals = [];
  const result = await terminateOwnedProcess({
    child,
    ownedPid: child.pid,
    processGroupId: child.pid,
    completion: Promise.resolve({
      exitCode: null,
      signalCode: "SIGTERM",
      spawnError: null,
    }),
    shutdownGraceMs: 10,
    signalProcess({ signalName }) {
      signals.push(signalName);
      return true;
    },
    groupExists: () => true,
    sleep: async (milliseconds) => {
      clock += milliseconds;
    },
    now: () => clock,
  });

  assert.deepEqual(signals, ["SIGTERM", "SIGKILL"]);
  assert.equal(result.stopped, false);
});

test("watches only the temporary project and isolated runtime home for crashes", async () => {
  const projectDirectory = resolve("/tmp/loomle-run/project");
  const runtimeStateDirectory = resolve("/tmp/loomle-run/runtime-state");
  const roots = await buildCrashRoots({
    projectDirectory,
    runtimeStateDirectory,
    engineVersion: "5.7",
    platform: "darwin",
  });

  assert.deepEqual(roots, [
    join(projectDirectory, "Saved", "Crashes"),
    join(
      runtimeStateDirectory,
      "Library",
      "Application Support",
      "Epic",
      "UnrealEngine",
      "5.7",
      "Saved",
      "Crashes",
    ),
    join(
      runtimeStateDirectory,
      "Library",
      "Application Support",
      "Epic",
      "CrashReportClient",
      "Saved",
      "Crashes",
    ),
  ]);
});

test("uses a short macOS runtime HOME without losing durable artifacts", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-runtime-home-"));
  const runtimeStateDirectory = join(
    root,
    "an-intentionally-long-output-directory",
    "runtime-state",
  );
  let runtimeHome = null;

  try {
    runtimeHome = await createRuntimeStateHome({
      runtimeStateDirectory,
      platform: "darwin",
    });
    const endpointPath = join(
      runtimeHome.home,
      ".loomle",
      "state",
      "endpoints",
      "00000000-0000-0000-0000-000000000000.sock",
    );
    assert.ok(
      Buffer.byteLength(endpointPath) < 104,
      `macOS Unix socket path must fit sockaddr_un: ${endpointPath}`,
    );

    await writeFile(join(runtimeHome.home, "durable.txt"), "preserved");
    assert.equal(
      await readFile(join(runtimeStateDirectory, "durable.txt"), "utf8"),
      "preserved",
    );
  } finally {
    if (runtimeHome?.aliasRoot) {
      await rm(runtimeHome.aliasRoot, { recursive: true, force: true });
    }
    await rm(root, { recursive: true, force: true });
  }
});

test("waits for a recursively observed crash snapshot to become quiet", async () => {
  let clock = 0;
  let calls = 0;
  const snapshots = [
    new Map([["/crash/one", { signature: "partial" }]]),
    new Map([["/crash/one", { signature: "complete" }]]),
    new Map([["/crash/one", { signature: "complete" }]]),
    new Map([["/crash/one", { signature: "complete" }]]),
  ];
  const result = await waitForStableCrashSnapshot(["/crash"], {
    deadlineMs: 100,
    quietMs: 20,
    pollIntervalMs: 10,
    now: () => clock,
    sleep: async (milliseconds) => {
      clock += milliseconds;
    },
    snapshot: async () => snapshots[Math.min(calls++, snapshots.length - 1)],
  });

  assert.equal(result.stable, true);
  assert.equal(result.snapshot.get("/crash/one").signature, "complete");
  assert.ok(calls >= 4);
});

test("retains the temporary workspace when the owned Editor cannot terminate", () => {
  assert.deepEqual(
    decideWorkspaceCleanup(
      "/tmp/loomle-ue-automation-owned",
      { terminationFailed: true },
    ),
    {
      action: "retain",
      reason: "editor_termination_failed",
    },
  );
  assert.deepEqual(
    decideWorkspaceCleanup(
      "/tmp/loomle-ue-automation-owned",
      { terminationFailed: false },
    ),
    {
      action: "remove",
      reason: null,
    },
  );
});

test("builds a direct Unreal Automation command line", () => {
  const argumentsList = buildAutomationArguments({
    projectPath: "/tmp/LoomleTestHost.uproject",
    reportDirectory: "/tmp/result/automation-report",
  });

  assert.equal(argumentsList[0], "/tmp/LoomleTestHost.uproject");
  assert.ok(argumentsList.includes("-ExecCmds=Automation RunTests Loomle;Quit"));
  assert.ok(argumentsList.includes("-TestExit=Automation Test Queue Empty"));
  assert.ok(
    argumentsList.includes(
      "-ReportExportPath=/tmp/result/automation-report",
    ),
  );
});

test("builds packaged Editor args and one shared isolated runtime environment", () => {
  const args = buildPackagedEditorArguments({
    projectPath: "/tmp/LoomleTestHost.uproject",
  });
  assert.equal(args[0], "/tmp/LoomleTestHost.uproject");
  assert.equal(args.some((argument) => argument.includes("Automation")), false);

  const environment = buildRuntimeEnvironment({
    environment: { PATH: "/bin", HOME: "/real-home" },
    runtimeStateDirectory: "/tmp/loomle-runtime",
    projectDirectory: "/tmp/loomle-project",
    platform: "darwin",
  });
  assert.equal(environment.HOME, "/tmp/loomle-runtime");
  assert.equal(environment.USERPROFILE, "/tmp/loomle-runtime");
  assert.equal(environment.TMPDIR, "/tmp/loomle-runtime/tmp");
  assert.equal(environment.TMP, "/tmp/loomle-runtime/tmp");
  assert.equal(environment.TEMP, "/tmp/loomle-runtime/tmp");
  assert.equal(environment.CODEX_HOME, "/tmp/loomle-runtime/codex");
  assert.equal(
    environment.XDG_CONFIG_HOME,
    "/tmp/loomle-runtime/xdg/config",
  );
  assert.equal(
    environment.APPDATA,
    "/tmp/loomle-runtime/AppData/Roaming",
  );
  assert.equal(
    environment.LOCALAPPDATA,
    "/tmp/loomle-runtime/AppData/Local",
  );
  assert.equal(environment.LOOMLE_PROJECT_ROOT, "/tmp/loomle-project");
  assert.equal(environment.PATH, "/bin");
});

test("owned Editor controller starts and stops exactly once", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-owned-editor-"));
  const child = new EventEmitter();
  child.pid = 41234;
  child.exitCode = null;
  child.signalCode = null;
  let spawnCount = 0;
  let terminateCount = 0;
  const controller = createOwnedEditorController({
    executable: "/UE/UnrealEditor-Cmd",
    arguments: ["/project/Test.uproject"],
    workingDirectory: root,
    environment: {},
    logPath: join(root, "editor.log"),
    shutdownGraceMs: 10,
    platform: "darwin",
    spawnProcess() {
      spawnCount += 1;
      queueMicrotask(() => child.emit("spawn"));
      return child;
    },
    async terminateProcess({ ownedPid, processGroupId }) {
      terminateCount += 1;
      assert.equal(ownedPid, child.pid);
      assert.equal(processGroupId, child.pid);
      return {
        stopped: true,
        completionResult: {
          exitCode: null,
          signalCode: "SIGTERM",
          spawnError: null,
        },
      };
    },
  });

  try {
    const first = await controller.start();
    const second = await controller.start();
    assert.equal(first.pid, child.pid);
    assert.equal(second.pid, child.pid);
    assert.equal(spawnCount, 1);

    const stopped = await controller.stop();
    const stoppedAgain = await controller.stop();
    assert.equal(stopped.stopRequested, true);
    assert.equal(stoppedAgain.signalCode, "SIGTERM");
    assert.equal(terminateCount, 1);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("owned Editor controller records an exit that precedes requested shutdown", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-owned-editor-exit-"));
  const child = new EventEmitter();
  child.pid = 41235;
  child.exitCode = null;
  child.signalCode = null;
  let drainCount = 0;
  const controller = createOwnedEditorController({
    executable: "/UE/UnrealEditor-Cmd",
    arguments: ["/project/Test.uproject"],
    workingDirectory: root,
    environment: {},
    logPath: join(root, "editor.log"),
    shutdownGraceMs: 10,
    platform: "darwin",
    spawnProcess() {
      queueMicrotask(() => child.emit("spawn"));
      return child;
    },
    async terminateProcess() {
      assert.fail("an already-exited Editor must not be signalled");
    },
    async drainExitedProcessGroup({ ownedPid, processGroupId }) {
      drainCount += 1;
      assert.equal(ownedPid, child.pid);
      assert.equal(processGroupId, child.pid);
      return { stopped: true };
    },
  });

  try {
    await controller.start();
    child.exitCode = 7;
    child.emit("close", 7, null);
    await new Promise((resolveMicrotask) => queueMicrotask(resolveMicrotask));

    const stopped = await controller.stop();
    assert.equal(stopped.stopRequested, true);
    assert.equal(stopped.exitedBeforeStop, true);
    assert.equal(stopped.exitCode, 7);
    assert.equal(drainCount, 1);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("owned Editor controller treats an ESRCH-at-stop race as unexpected", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-owned-editor-race-"));
  const child = new EventEmitter();
  child.pid = 41236;
  child.exitCode = null;
  child.signalCode = null;
  const controller = createOwnedEditorController({
    executable: "/UE/UnrealEditor-Cmd",
    arguments: ["/project/Test.uproject"],
    workingDirectory: root,
    environment: {},
    logPath: join(root, "editor.log"),
    shutdownGraceMs: 10,
    platform: "darwin",
    spawnProcess() {
      queueMicrotask(() => child.emit("spawn"));
      return child;
    },
    async terminateProcess() {
      child.exitCode = 0;
      child.emit("close", 0, null);
      return {
        stopped: true,
        completionResult: {
          exitCode: 0,
          signalCode: null,
          spawnError: null,
        },
        signalDelivered: false,
      };
    },
  });

  try {
    await controller.start();
    const stopped = await controller.stop();
    assert.equal(stopped.exitedBeforeStop, true);
    assert.equal(stopped.exitCode, 0);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("classifies only concrete assertion, fatal, and critical log records", () => {
  const hazards = classifyEditorLog([
    "LogTemp: Display: assertion helpers loaded",
    "LogAutomation: Display: fatal error handling test started",
    "LogOutputDevice: Error: Assertion failed: GeneratedClass->GetDefaultObject(false)",
    "LogCore: Error: Fatal error: UObject invariant failed",
    "LogMac: Error: Critical error: signal 11",
  ].join("\n"));

  assert.deepEqual(
    hazards.map(({ kind, lineNumber }) => ({ kind, lineNumber })),
    [
      { kind: "assertion", lineNumber: 3 },
      { kind: "fatal_error", lineNumber: 4 },
      { kind: "critical_error", lineNumber: 5 },
    ],
  );
});

test("summarizes the PascalCase UE 5.7 Automation report", () => {
  const summary = summarizeAutomationReportObject({
    Succeeded: 2,
    SucceededWithWarnings: 1,
    Failed: 1,
    NotRun: 1,
    InProcess: 0,
    Tests: [
      { FullTestPath: "Loomle.Bridge.Query", State: "Success" },
      { FullTestPath: "Loomle.Bridge.Patch", State: "Success" },
      { FullTestPath: "Loomle.Bridge.Warning", State: "SuccessWithWarnings" },
      { FullTestPath: "Loomle.Bridge.Failure", State: "Fail" },
      { FullTestPath: "Loomle.Bridge.Skipped", State: "NotRun" },
    ],
  });

  assert.deepEqual(summary, {
    valid: true,
    total: 5,
    passed: 3,
    failed: 1,
    notRun: 1,
    inProcess: 0,
    categoryTests: 5,
  });
});

test("parses UE 5.7 Automation reports with a UTF-8 BOM", () => {
  assert.deepEqual(
    parseAutomationReportText("\uFEFF{\"Succeeded\":1,\"Failed\":0}"),
    { Succeeded: 1, Failed: 0 },
  );
});

test("rejects missing, empty, wrong-category, and failing Automation results", () => {
  const editor = {
    exitCode: 0,
    signalCode: null,
  };
  const cases = [
    {
      report: { present: false },
      code: "automation_report_missing",
    },
    {
      report: {
        present: true,
        valid: true,
        total: 0,
        passed: 0,
        failed: 0,
        notRun: 0,
        inProcess: 0,
        categoryTests: 0,
      },
      code: "automation_zero_tests",
    },
    {
      report: {
        present: true,
        valid: true,
        total: 1,
        passed: 1,
        failed: 0,
        notRun: 0,
        inProcess: 0,
        categoryTests: 0,
      },
      code: "automation_category_missing",
    },
    {
      report: {
        present: true,
        valid: true,
        total: 2,
        passed: 1,
        failed: 1,
        notRun: 0,
        inProcess: 0,
        categoryTests: 2,
      },
      code: "automation_tests_failed",
    },
  ];

  for (const { report, code } of cases) {
    const outcome = classifyRunOutcome({
      editor,
      automationReport: report,
    });
    assert.equal(outcome.status, "failed");
    assert.equal(outcome.firstFailingPhase, "automation");
    assert.equal(outcome.diagnostics[0].code, code);
  }
});

test("classifies passing, timed-out, nonzero, and crashed runs", () => {
  const passingReport = {
    present: true,
    valid: true,
    total: 2,
    passed: 2,
    failed: 0,
    notRun: 0,
    inProcess: 0,
    categoryTests: 2,
  };
  assert.deepEqual(
    classifyRunOutcome({
      editor: { exitCode: 0, signalCode: null },
      automationReport: passingReport,
    }),
    {
      status: "passed",
      firstFailingPhase: null,
      diagnostics: [],
    },
  );

  const timedOut = classifyRunOutcome({
    editor: { timedOut: true, deadlineMs: 1000 },
    automationReport: passingReport,
  });
  assert.equal(timedOut.status, "timed_out");
  assert.equal(timedOut.diagnostics[0].code, "phase_timeout");

  const nonzero = classifyRunOutcome({
    editor: { exitCode: 7, signalCode: null },
    automationReport: passingReport,
  });
  assert.equal(nonzero.status, "failed");
  assert.equal(nonzero.diagnostics[0].code, "editor_exit_nonzero");

  const crashed = classifyRunOutcome({
    editor: { exitCode: 0, signalCode: null },
    automationReport: passingReport,
    logHazards: [{
      kind: "assertion",
      line: "Assertion failed: false",
      lineNumber: 42,
    }],
  });
  assert.equal(crashed.status, "crashed");
  assert.equal(crashed.diagnostics[0].code, "editor_log_assertion");
});

test("classifies packaged smoke failures and crash evidence", () => {
  const failed = classifyPackagedE2EOutcome({
    smokeError: new Error("fixture missing"),
  });
  assert.equal(failed.status, "failed");
  assert.equal(failed.firstFailingPhase, "packaged_smoke");
  assert.equal(failed.diagnostics[0].code, "packaged_smoke_failed");

  const crashed = classifyPackagedE2EOutcome({
    smokeError: new Error("transport closed"),
    newCrashReports: [{ sourcePath: "/tmp/Crash-123" }],
  });
  assert.equal(crashed.status, "crashed");
  assert.equal(crashed.diagnostics[0].code, "crash_report_created");

  const unexpectedExit = classifyPackagedE2EOutcome({
    editor: {
      exitedBeforeStop: true,
      exitCode: 7,
      signalCode: null,
    },
  });
  assert.equal(unexpectedExit.status, "failed");
  assert.equal(
    unexpectedExit.diagnostics[0].code,
    "editor_exited_unexpectedly",
  );
});

test("packaged_e2e outer finally stops Editor and writes durable failure artifacts", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-packaged-profile-"));
  const outputDir = join(root, "result");
  const temporaryRoot = join(root, "owned-workspace");
  const runtimeAliasRoot = join(root, "runtime-alias");
  const projectDirectory = join(temporaryRoot, "project");
  await Promise.all([
    mkdir(projectDirectory, { recursive: true }),
    mkdir(runtimeAliasRoot, { recursive: true }),
  ]);
  await writeFile(join(temporaryRoot, ".keep"), "retained");
  await writeFile(join(runtimeAliasRoot, ".keep"), "alias");
  let startCount = 0;
  let stopCount = 0;
  let stopped = false;
  const editorState = () => ({
    executable: "/UE/UnrealEditor-Cmd",
    pid: 9345,
    processGroupId: 9345,
    exitCode: stopped ? 0 : null,
    signalCode: null,
    stopRequested: stopped,
    terminationFailed: false,
  });
  let clock = 0;

  try {
    const result = await runPackagedE2E({
      profile: "packaged_e2e",
      target: "darwin-arm64",
      ueRoot: join(root, "UE"),
      projectTemplate: join(root, "template"),
      pluginDir: join(root, "candidate"),
      outputDir,
      phaseDeadlineMs: 1000,
      shutdownGraceMs: 100,
    }, {
      repoRoot: join(root, "no-repository"),
      platform: "darwin",
      now: () => new Date(1_000 + clock++),
      prepareCandidate: async () => ({
        executable: "/UE/UnrealEditor-Cmd",
        temporaryRoot,
        runtimeAliasRoot,
        projectDirectory,
        projectPath: join(projectDirectory, "LoomleTestHost.uproject"),
        crashRoots: [],
        crashSnapshot: new Map(),
        archiveSha256: null,
        versionName: "0.7.0-test",
        clientExecutable: "/candidate/Resources/Loomle/darwin-arm64/loomle",
        fixture: {
          schemaVersion: 1,
          blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
          blueprintDescription: "Loomle packaged E2E baseline",
          assetType: "/Script/Engine.Blueprint",
          assetSearchText: "BP_LoomleE2E",
          assetRoot: "/Game/LoomleTests",
        },
        candidate: {
          kind: "directory",
          path: join(root, "candidate"),
          pluginName: "LoomleBridge",
          versionName: "0.7.0-test",
        },
      }),
      editorControllerFactory: () => ({
        async start() {
          startCount += 1;
          return editorState();
        },
        async stop() {
          if (!stopped) {
            stopped = true;
            stopCount += 1;
          }
          return editorState();
        },
        state: editorState,
      }),
      smokeRunner: async ({
        startEditor,
        stderrSink,
        onStep,
        signal,
      }) => {
        assert.equal(signal.aborted, false);
        await startEditor();
        stderrSink("candidate stderr\n");
        onStep({
          name: "initialize_and_list_tools",
          status: "failed",
          durationMs: 3,
          error: "simulated smoke failure",
        });
        throw new Error("simulated smoke failure");
      },
    });

    assert.equal(result.status, "failed");
    assert.equal(result.firstFailingPhase, "packaged_smoke");
    assert.deepEqual(Object.keys(result.phases), [
      "prepare",
      "packaged_smoke",
      "collect",
      "cleanup",
    ]);
    assert.equal(result.packagedE2e.steps.length, 1);
    assert.equal(result.packagedE2e.steps[0].status, "failed");
    assert.equal(result.productVersion, "0.7.0-test");
    assert.equal(result.candidate.versionName, "0.7.0-test");
    assert.equal(startCount, 1);
    assert.equal(stopCount, 1, "outer finally owns cleanup after smoke failure");
    assert.equal(
      await readFile(join(outputDir, "client.stderr.log"), "utf8"),
      "candidate stderr\n",
    );
    assert.deepEqual(
      JSON.parse(await readFile(join(outputDir, "result.json"), "utf8")),
      result,
    );
    await assert.rejects(
      readFile(join(temporaryRoot, ".keep"), "utf8"),
      (error) => error?.code === "ENOENT",
    );
    await assert.rejects(
      readFile(join(runtimeAliasRoot, ".keep"), "utf8"),
      (error) => error?.code === "ENOENT",
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("packaged_e2e retains its workspace when aborted smoke cleanup will not settle", async () => {
  const root = await mkdtemp(join(tmpdir(), "loomle-packaged-unsettled-"));
  const outputDir = join(root, "result");
  const temporaryRoot = join(root, "owned-workspace");
  const runtimeAliasRoot = join(root, "runtime-alias");
  const projectDirectory = join(temporaryRoot, "project");
  await Promise.all([
    mkdir(projectDirectory, { recursive: true }),
    mkdir(runtimeAliasRoot, { recursive: true }),
  ]);
  await writeFile(join(temporaryRoot, ".keep"), "retained");
  await writeFile(join(runtimeAliasRoot, ".keep"), "alias");
  let stopped = false;
  const editorState = () => ({
    executable: "/UE/UnrealEditor-Cmd",
    pid: null,
    processGroupId: null,
    exitCode: null,
    signalCode: null,
    stopRequested: stopped,
    exitedBeforeStop: false,
    terminationFailed: false,
  });

  try {
    const result = await runPackagedE2E({
      profile: "packaged_e2e",
      target: "darwin-arm64",
      ueRoot: join(root, "UE"),
      projectTemplate: join(root, "template"),
      pluginDir: join(root, "candidate"),
      outputDir,
      phaseDeadlineMs: 10,
      shutdownGraceMs: 5,
    }, {
      repoRoot: join(root, "no-repository"),
      platform: "darwin",
      prepareCandidate: async () => ({
        executable: "/UE/UnrealEditor-Cmd",
        temporaryRoot,
        runtimeAliasRoot,
        projectDirectory,
        projectPath: join(projectDirectory, "LoomleTestHost.uproject"),
        crashRoots: [],
        crashSnapshot: new Map(),
        archiveSha256: null,
        versionName: "0.7.0-test",
        clientExecutable: "/candidate/Resources/Loomle/darwin-arm64/loomle",
        fixture: {
          schemaVersion: 1,
          blueprintAssetPath: "/Game/LoomleTests/BP_LoomleE2E.BP_LoomleE2E",
          blueprintDescription: "Loomle packaged E2E baseline",
          assetType: "/Script/Engine.Blueprint",
          assetSearchText: "BP_LoomleE2E",
          assetRoot: "/Game/LoomleTests",
        },
        candidate: {
          kind: "directory",
          path: join(root, "candidate"),
          pluginName: "LoomleBridge",
          versionName: "0.7.0-test",
        },
      }),
      editorControllerFactory: () => ({
        async start() {
          return editorState();
        },
        async stop() {
          stopped = true;
          return editorState();
        },
        state: editorState,
      }),
      smokeRunner: async () => new Promise(() => {}),
    });

    assert.equal(result.status, "failed");
    assert.equal(
      result.diagnostics[0].code,
      "smoke_cleanup_unsettled",
    );
    assert.equal(result.workspace.retained, true);
    assert.equal(
      result.workspace.cleanupSkippedReason,
      "smoke_cleanup_unsettled",
    );
    assert.equal(
      await readFile(join(temporaryRoot, ".keep"), "utf8"),
      "retained",
    );
    assert.equal(
      await readFile(join(runtimeAliasRoot, ".keep"), "utf8"),
      "alias",
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("builds the durable result.json contract", () => {
  const startedAt = new Date("2026-07-23T01:00:00.000Z");
  const finishedAt = new Date("2026-07-23T01:00:04.250Z");
  const result = buildResultDocument({
    startedAt,
    finishedAt,
    metadata: {
      productVersion: "0.7.0-dev.1",
      protocolVersion: 2,
      commit: "abc123",
      target: "darwin-arm64",
      archiveSha256: null,
      candidate: {
        kind: "directory",
        path: "/repo/build/LoomleBridge",
        pluginName: "LoomleBridge",
      },
    },
    phases: {
      prepare: { status: "passed", durationMs: 100 },
      automation: { status: "passed", durationMs: 4000 },
      collect: { status: "passed", durationMs: 100 },
      cleanup: { status: "passed", durationMs: 50 },
    },
    outcome: {
      status: "passed",
      firstFailingPhase: null,
      diagnostics: [],
    },
    editor: {
      executable: "/UE/UnrealEditor-Cmd",
      pid: 123,
      processGroupId: 123,
      exitCode: 0,
      signalCode: null,
    },
    automationReport: {
      present: true,
      valid: true,
      total: 68,
      passed: 68,
      failed: 0,
      notRun: 0,
      inProcess: 0,
      categoryTests: 68,
    },
    logHazards: [],
    crashReports: [],
    workspace: {
      path: "/tmp/loomle-ue-automation-owned",
      retained: true,
      cleanup: "skipped",
      cleanupSkippedReason: "editor_termination_failed",
    },
  });

  assert.equal(result.schemaVersion, 1);
  assert.equal(result.status, "passed");
  assert.equal(result.durationMs, 4250);
  assert.equal(result.productVersion, "0.7.0-dev.1");
  assert.equal(result.protocolVersion, 2);
  assert.equal(result.editor.pid, 123);
  assert.equal(result.automation.total, 68);
  assert.deepEqual(result.crashes.newReports, []);
  assert.deepEqual(result.workspace, {
    path: "/tmp/loomle-ue-automation-owned",
    retained: true,
    cleanup: "skipped",
    cleanupSkippedReason: "editor_termination_failed",
  });
  assert.equal(result.artifacts.editorLog, "editor.log");
});
