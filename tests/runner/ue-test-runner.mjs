#!/usr/bin/env node

import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import {
  appendFileSync,
  constants as fsConstants,
  createReadStream,
} from "node:fs";
import {
  access,
  cp,
  mkdir,
  mkdtemp,
  lstat,
  open,
  readFile,
  readdir,
  realpath,
  rm,
  stat,
  symlink,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import {
  basename,
  dirname,
  extname,
  isAbsolute,
  join,
  relative,
  resolve,
  sep,
} from "node:path";
import { createInterface } from "node:readline";
import { fileURLToPath, pathToFileURL } from "node:url";

const DEFAULT_REPO_ROOT = fileURLToPath(new URL("../../", import.meta.url));
const DEFAULT_PHASE_DEADLINE_SECONDS = 30 * 60;
const DEFAULT_SHUTDOWN_GRACE_SECONDS = 15;
const AUTOMATION_FILTER = "Loomle";
const MAX_LOG_HAZARDS = 20;
const FIXTURE_MANIFEST_NAME = "LoomleE2E.fixture.json";
const SUPPORTED_PROFILES = new Set(["automation", "packaged_e2e"]);

const TARGETS = new Map([
  ["darwin-arm64", {
    hostPlatform: "darwin",
    unrealPlatform: "Mac",
    binariesDirectory: "Mac",
    moduleBinaryName: (moduleName) => `UnrealEditor-${moduleName}.dylib`,
    bundledClientPath: join(
      "Resources",
      "Loomle",
      "darwin-arm64",
      "loomle",
    ),
    commandPaths: [
      "UnrealEditor-Cmd",
      "UnrealEditor",
      join("UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
    ],
  }],
  ["win32-x64", {
    hostPlatform: "win32",
    unrealPlatform: "Win64",
    binariesDirectory: "Win64",
    moduleBinaryName: (moduleName) => `UnrealEditor-${moduleName}.dll`,
    bundledClientPath: join(
      "Resources",
      "Loomle",
      "win32-x64",
      "loomle.exe",
    ),
    commandPaths: [
      "UnrealEditor-Cmd.exe",
      "UnrealEditor.exe",
    ],
  }],
]);

const PATH_OPTIONS = new Set([
  "ue-root",
  "project-template",
  "plugin-dir",
  "plugin-archive",
  "output-dir",
]);

const VALUE_OPTIONS = new Set([
  "profile",
  "ue-root",
  "project-template",
  "plugin-dir",
  "plugin-archive",
  "output-dir",
  "target",
  "phase-deadline-seconds",
  "shutdown-grace-seconds",
]);

const PLUGIN_COPY_IGNORES = new Set([
  ".DS_Store",
  ".git",
  "Intermediate",
  "Saved",
]);

export class RunnerUsageError extends Error {
  constructor(message) {
    super(message);
    this.name = "RunnerUsageError";
  }
}

export class PhaseTimeoutError extends Error {
  constructor(phase, deadlineMs) {
    super(`${phase} exceeded its ${deadlineMs} ms deadline`);
    this.name = "PhaseTimeoutError";
    this.phase = phase;
    this.deadlineMs = deadlineMs;
  }
}

export function runnerUsage() {
  return [
    "Usage:",
    "  node tests/runner/ue-test-runner.mjs \\",
    "    --profile <automation|packaged_e2e> \\",
    "    --ue-root <UE root> \\",
    "    --project-template <project directory> \\",
    "    (--plugin-dir <compiled plugin directory> | --plugin-archive <compiled plugin archive>) \\",
    "    --output-dir <artifact directory> \\",
    "    --target <darwin-arm64>",
    "",
    "The plugin directory/archive must be packaged or HostProject output with",
    "platform binaries. Raw plugin source is not a runnable test candidate.",
    "packaged_e2e additionally requires the bundled Client and VersionName.",
    "",
    "Optional:",
    `  --phase-deadline-seconds <seconds>  Default: ${DEFAULT_PHASE_DEADLINE_SECONDS}`,
    `  --shutdown-grace-seconds <seconds>  Default: ${DEFAULT_SHUTDOWN_GRACE_SECONDS}`,
  ].join("\n");
}

export function parseRunnerArgs(argv, { cwd = process.cwd() } = {}) {
  const values = new Map();
  let help = false;

  for (let index = 0; index < argv.length; index += 1) {
    const rawArgument = argv[index];
    if (rawArgument === "--help" || rawArgument === "-h") {
      help = true;
      continue;
    }
    if (!rawArgument.startsWith("--")) {
      throw new RunnerUsageError(`unexpected positional argument: ${rawArgument}`);
    }

    const equalsIndex = rawArgument.indexOf("=");
    const optionName = rawArgument.slice(
      2,
      equalsIndex === -1 ? undefined : equalsIndex,
    );
    if (!VALUE_OPTIONS.has(optionName)) {
      throw new RunnerUsageError(`unknown option: --${optionName}`);
    }
    if (values.has(optionName)) {
      throw new RunnerUsageError(`option may only be provided once: --${optionName}`);
    }

    let optionValue;
    if (equalsIndex !== -1) {
      optionValue = rawArgument.slice(equalsIndex + 1);
    } else {
      index += 1;
      optionValue = argv[index];
    }
    if (!optionValue || optionValue.startsWith("--")) {
      throw new RunnerUsageError(`missing value for --${optionName}`);
    }
    values.set(optionName, optionValue);
  }

  if (help) return { help: true };

  for (const requiredName of [
    "profile",
    "ue-root",
    "project-template",
    "output-dir",
    "target",
  ]) {
    if (!values.has(requiredName)) {
      throw new RunnerUsageError(`missing required option: --${requiredName}`);
    }
  }

  if (!SUPPORTED_PROFILES.has(values.get("profile"))) {
    throw new RunnerUsageError(
      `unsupported profile ${JSON.stringify(values.get("profile"))}; expected one of ${[...SUPPORTED_PROFILES].join(", ")}`,
    );
  }

  const pluginInputs = ["plugin-dir", "plugin-archive"]
    .filter((name) => values.has(name));
  if (pluginInputs.length !== 1) {
    throw new RunnerUsageError(
      "provide exactly one of --plugin-dir or --plugin-archive",
    );
  }

  const target = values.get("target");
  if (!TARGETS.has(target)) {
    throw new RunnerUsageError(
      `unsupported target ${JSON.stringify(target)}; expected one of ${[...TARGETS.keys()].join(", ")}`,
    );
  }

  const phaseDeadlineMs = parseSeconds(
    values.get("phase-deadline-seconds"),
    DEFAULT_PHASE_DEADLINE_SECONDS,
    "phase-deadline-seconds",
  );
  const shutdownGraceMs = parseSeconds(
    values.get("shutdown-grace-seconds"),
    DEFAULT_SHUTDOWN_GRACE_SECONDS,
    "shutdown-grace-seconds",
  );

  const result = {
    profile: values.get("profile"),
    target,
    phaseDeadlineMs,
    shutdownGraceMs,
  };
  for (const optionName of PATH_OPTIONS) {
    const value = values.get(optionName);
    if (value !== undefined) {
      result[toCamelCase(optionName)] = resolve(cwd, value);
    }
  }
  return result;
}

function parseSeconds(rawValue, defaultSeconds, optionName) {
  if (rawValue === undefined) return defaultSeconds * 1000;
  const seconds = Number(rawValue);
  if (!Number.isFinite(seconds) || seconds <= 0 || seconds > 24 * 60 * 60) {
    throw new RunnerUsageError(
      `--${optionName} must be a positive number no greater than 86400`,
    );
  }
  return Math.ceil(seconds * 1000);
}

function toCamelCase(value) {
  return value.replace(/-([a-z])/g, (_match, letter) => letter.toUpperCase());
}

export function buildAutomationArguments({ projectPath, reportDirectory }) {
  return [
    projectPath,
    "-Unattended",
    "-NoPause",
    "-NoP4",
    "-NoSplash",
    "-NullRHI",
    "-NoSound",
    "-UTF8Output",
    "-stdout",
    "-FullStdOutLogOutput",
    `-ExecCmds=Automation RunTests ${AUTOMATION_FILTER};Quit`,
    "-TestExit=Automation Test Queue Empty",
    `-ReportExportPath=${reportDirectory}`,
  ];
}

export function buildPackagedEditorArguments({ projectPath }) {
  return [
    projectPath,
    "-Unattended",
    "-NoPause",
    "-NoP4",
    "-NoSplash",
    "-NullRHI",
    "-NoSound",
    "-UTF8Output",
    "-stdout",
    "-FullStdOutLogOutput",
  ];
}

export function classifyEditorLog(logText) {
  const hazards = [];
  const lines = String(logText).split(/\r?\n/);
  for (let index = 0; index < lines.length; index += 1) {
    const hazardKind = classifyLogLine(lines[index]);
    if (!hazardKind) continue;
    hazards.push({
      kind: hazardKind,
      line: lines[index],
      lineNumber: index + 1,
    });
  }
  return hazards;
}

function classifyLogLine(line) {
  if (/\bAssertion failed\s*:/i.test(line)) return "assertion";
  if (/\bLowLevelFatalError\b/i.test(line) || /\bFatal error\s*[:!]/i.test(line)) {
    return "fatal_error";
  }
  if (/\bCritical error\s*[:!]/i.test(line)) return "critical_error";
  return null;
}

export function summarizeAutomationReportObject(report) {
  if (!report || typeof report !== "object" || Array.isArray(report)) {
    return {
      valid: false,
      error: "Automation report root must be an object.",
      total: 0,
      passed: 0,
      failed: 0,
      notRun: 0,
      inProcess: 0,
      categoryTests: 0,
    };
  }

  const tests = Array.isArray(report.Tests)
    ? report.Tests
    : (Array.isArray(report.tests) ? report.tests : []);
  const stateCounts = countTestStates(tests);
  const succeeded = firstNumber(report, [
    "Succeeded",
    "succeeded",
    "Passed",
    "passed",
  ]);
  const succeededWithWarnings = firstNumber(report, [
    "SucceededWithWarnings",
    "succeededWithWarnings",
    "PassedWithWarnings",
    "passedWithWarnings",
  ]);
  const explicitFailed = firstNumber(report, ["Failed", "failed"]);
  const explicitNotRun = firstNumber(report, [
    "NotRun",
    "notRun",
    "Skipped",
    "skipped",
  ]);
  const explicitInProcess = firstNumber(report, ["InProcess", "inProcess"]);

  const passed = succeeded === undefined && succeededWithWarnings === undefined
    ? stateCounts.passed
    : (succeeded ?? 0) + (succeededWithWarnings ?? 0);
  const failed = explicitFailed ?? stateCounts.failed;
  const notRun = explicitNotRun ?? stateCounts.notRun;
  const inProcess = explicitInProcess ?? stateCounts.inProcess;
  const countedTotal = passed + failed + notRun + inProcess;
  const explicitTotal = firstNumber(report, [
    "Total",
    "total",
    "TotalTests",
    "totalTests",
  ]);
  const total = Math.max(explicitTotal ?? 0, tests.length, countedTotal);
  const categoryTests = tests.filter((test) => {
    const fullPath = String(
      test?.FullTestPath
      ?? test?.fullTestPath
      ?? test?.TestDisplayName
      ?? test?.testDisplayName
      ?? "",
    );
    return fullPath === AUTOMATION_FILTER
      || fullPath.startsWith(`${AUTOMATION_FILTER}.`);
  }).length;

  return {
    valid: true,
    total,
    passed,
    failed,
    notRun,
    inProcess,
    categoryTests,
  };
}

export function parseAutomationReportText(text) {
  const value = String(text);
  return JSON.parse(value.charCodeAt(0) === 0xFEFF ? value.slice(1) : value);
}

function firstNumber(object, names) {
  for (const name of names) {
    const value = object[name];
    if (typeof value === "number" && Number.isFinite(value) && value >= 0) {
      return value;
    }
  }
  return undefined;
}

function countTestStates(tests) {
  const counts = {
    passed: 0,
    failed: 0,
    notRun: 0,
    inProcess: 0,
  };
  for (const test of tests) {
    const state = String(test?.State ?? test?.state ?? "").toLowerCase();
    if (state === "success" || state === "succeeded" || state === "passed"
        || state === "successwithwarnings" || state === "passedwithwarnings") {
      counts.passed += 1;
    } else if (state === "fail" || state === "failed" || state === "failure") {
      counts.failed += 1;
    } else if (state === "inprocess" || state === "running") {
      counts.inProcess += 1;
    } else {
      counts.notRun += 1;
    }
  }
  return counts;
}

export function classifyRunOutcome({
  prepareError = null,
  prepareTimedOut = false,
  editor = null,
  automationReport = null,
  logHazards = [],
  newCrashReports = [],
  collectErrors = [],
  cleanupError = null,
}) {
  if (prepareError) {
    return {
      status: prepareTimedOut ? "timed_out" : "failed",
      firstFailingPhase: "prepare",
      diagnostics: [{
        code: prepareTimedOut ? "phase_timeout" : "prepare_failed",
        message: prepareError.message,
      }],
    };
  }

  if (editor?.timedOut) {
    return {
      status: "timed_out",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "phase_timeout",
        message: `Automation exceeded its ${editor.deadlineMs} ms deadline.`,
      }],
    };
  }

  if (editor?.interrupted) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "runner_interrupted",
        message: "The runner was interrupted while Unreal Editor was active.",
      }],
    };
  }

  if (newCrashReports.length > 0 || logHazards.length > 0) {
    const diagnostics = [];
    for (const crash of newCrashReports) {
      diagnostics.push({
        code: "crash_report_created",
        message: `Unreal created a new crash report: ${crash.sourcePath}`,
      });
    }
    for (const hazard of logHazards) {
      diagnostics.push({
        code: `editor_log_${hazard.kind}`,
        message: `Editor log line ${hazard.lineNumber}: ${hazard.line}`,
      });
    }
    return {
      status: "crashed",
      firstFailingPhase: "automation",
      diagnostics,
    };
  }

  if (!editor || editor.spawnError) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "editor_start_failed",
        message: editor?.spawnError?.message ?? "Unreal Editor did not start.",
      }],
    };
  }

  if (editor.terminationFailed) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "editor_termination_failed",
        message: `The owned Editor process ${editor.pid} did not terminate.`,
      }],
    };
  }

  if (editor.signalCode || editor.exitCode !== 0) {
    const exitDescription = editor.signalCode
      ? `signal ${editor.signalCode}`
      : `exit code ${editor.exitCode}`;
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "editor_exit_nonzero",
        message: `Unreal Editor exited with ${exitDescription}.`,
      }],
    };
  }

  if (!automationReport?.present) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "automation_report_missing",
        message: "Unreal did not create automation-report/index.json.",
      }],
    };
  }

  if (!automationReport.valid) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "automation_report_invalid",
        message: automationReport.error,
      }],
    };
  }

  if (automationReport.total === 0) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "automation_zero_tests",
        message: `Automation category ${AUTOMATION_FILTER} executed zero tests.`,
      }],
    };
  }

  if (automationReport.categoryTests === 0) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "automation_category_missing",
        message: `Automation report contains no tests in category ${AUTOMATION_FILTER}.`,
      }],
    };
  }

  if (automationReport.failed > 0
      || automationReport.notRun > 0
      || automationReport.inProcess > 0
      || automationReport.passed < automationReport.total) {
    return {
      status: "failed",
      firstFailingPhase: "automation",
      diagnostics: [{
        code: "automation_tests_failed",
        message: [
          `${automationReport.failed} failed`,
          `${automationReport.notRun} not run`,
          `${automationReport.inProcess} still in process`,
          `${automationReport.passed}/${automationReport.total} passed`,
        ].join(", "),
      }],
    };
  }

  if (collectErrors.length > 0) {
    return {
      status: "failed",
      firstFailingPhase: "collect",
      diagnostics: collectErrors.map((error) => ({
        code: "collect_failed",
        message: error.message,
      })),
    };
  }

  if (cleanupError) {
    return {
      status: "failed",
      firstFailingPhase: "cleanup",
      diagnostics: [{
        code: "cleanup_failed",
        message: cleanupError.message,
      }],
    };
  }

  return {
    status: "passed",
    firstFailingPhase: null,
    diagnostics: [],
  };
}

export function decideWorkspaceCleanup(temporaryRoot, editor) {
  if (!temporaryRoot) {
    return {
      action: "not_created",
      reason: null,
    };
  }
  if (editor?.terminationFailed) {
    return {
      action: "retain",
      reason: "editor_termination_failed",
    };
  }
  return {
    action: "remove",
    reason: null,
  };
}

export function buildResultDocument({
  startedAt,
  finishedAt,
  metadata,
  phases,
  outcome,
  editor,
  automationReport,
  logHazards,
  crashReports,
  workspace,
}) {
  return {
    schemaVersion: 1,
    profile: "automation",
    status: outcome.status,
    firstFailingPhase: outcome.firstFailingPhase,
    startedAt: startedAt.toISOString(),
    finishedAt: finishedAt.toISOString(),
    durationMs: Math.max(0, finishedAt.getTime() - startedAt.getTime()),
    productVersion: metadata.productVersion,
    protocolVersion: metadata.protocolVersion,
    commit: metadata.commit,
    target: metadata.target,
    archiveSha256: metadata.archiveSha256,
    candidate: metadata.candidate,
    phases,
    editor: editor ? {
      executable: editor.executable,
      pid: editor.pid ?? null,
      processGroupId: editor.processGroupId ?? null,
      exitCode: editor.exitCode ?? null,
      signalCode: editor.signalCode ?? null,
      timedOut: Boolean(editor.timedOut),
      interrupted: Boolean(editor.interrupted),
      terminationFailed: Boolean(editor.terminationFailed),
      deadlineMs: editor.deadlineMs ?? null,
    } : null,
    automation: automationReport,
    logHazards,
    crashes: {
      newReports: crashReports,
    },
    workspace,
    diagnostics: outcome.diagnostics,
    artifacts: {
      automationReport: "automation-report",
      editorLog: "editor.log",
      clientStderrLog: "client.stderr.log",
      runtimeState: "runtime-state",
      crashes: "crashes",
    },
  };
}

export function classifyPackagedE2EOutcome({
  prepareError = null,
  prepareTimedOut = false,
  smokeError = null,
  smokeTimedOut = false,
  editor = null,
  smokeCleanupUnsettled = false,
  logHazards = [],
  newCrashReports = [],
  collectErrors = [],
  cleanupError = null,
}) {
  if (prepareError) {
    return {
      status: prepareTimedOut ? "timed_out" : "failed",
      firstFailingPhase: "prepare",
      diagnostics: [{
        code: prepareTimedOut ? "phase_timeout" : "prepare_failed",
        message: prepareError.message,
      }],
    };
  }

  if (newCrashReports.length > 0 || logHazards.length > 0) {
    return {
      status: "crashed",
      firstFailingPhase: "packaged_smoke",
      diagnostics: [
        ...newCrashReports.map((crash) => ({
          code: "crash_report_created",
          message: `Unreal created a new crash report: ${crash.sourcePath}`,
        })),
        ...logHazards.map((hazard) => ({
          code: `editor_log_${hazard.kind}`,
          message: `Editor log line ${hazard.lineNumber}: ${hazard.line}`,
        })),
      ],
    };
  }

  if (editor?.terminationFailed) {
    return {
      status: "failed",
      firstFailingPhase: "packaged_smoke",
      diagnostics: [{
        code: "editor_termination_failed",
        message: `The owned Editor process ${editor.pid} did not terminate.`,
      }],
    };
  }

  if (smokeCleanupUnsettled) {
    return {
      status: "failed",
      firstFailingPhase: "packaged_smoke",
      diagnostics: [{
        code: "smoke_cleanup_unsettled",
        message: "Packaged smoke cleanup did not settle before its bounded deadline.",
      }],
    };
  }

  if (editor?.exitedBeforeStop) {
    const exitDescription = editor.signalCode
      ? `signal ${editor.signalCode}`
      : `exit code ${editor.exitCode}`;
    return {
      status: "failed",
      firstFailingPhase: "packaged_smoke",
      diagnostics: [{
        code: "editor_exited_unexpectedly",
        message: `The owned Editor exited with ${exitDescription} before the test requested shutdown.`,
      }],
    };
  }

  if (smokeError) {
    return {
      status: smokeTimedOut ? "timed_out" : "failed",
      firstFailingPhase: "packaged_smoke",
      diagnostics: [{
        code: smokeTimedOut ? "phase_timeout" : "packaged_smoke_failed",
        message: smokeError.message,
      }],
    };
  }

  if (collectErrors.length > 0) {
    return {
      status: "failed",
      firstFailingPhase: "collect",
      diagnostics: collectErrors.map((error) => ({
        code: "collect_failed",
        message: error.message,
      })),
    };
  }

  if (cleanupError) {
    return {
      status: "failed",
      firstFailingPhase: "cleanup",
      diagnostics: [{
        code: "cleanup_failed",
        message: cleanupError.message,
      }],
    };
  }

  return {
    status: "passed",
    firstFailingPhase: null,
    diagnostics: [],
  };
}

export function buildPackagedE2EResultDocument({
  startedAt,
  finishedAt,
  metadata,
  phases,
  outcome,
  editor,
  packagedE2e,
  logHazards,
  crashReports,
  workspace,
}) {
  return {
    schemaVersion: 1,
    profile: "packaged_e2e",
    status: outcome.status,
    firstFailingPhase: outcome.firstFailingPhase,
    startedAt: startedAt.toISOString(),
    finishedAt: finishedAt.toISOString(),
    durationMs: Math.max(0, finishedAt.getTime() - startedAt.getTime()),
    productVersion: metadata.productVersion,
    protocolVersion: metadata.protocolVersion,
    commit: metadata.commit,
    target: metadata.target,
    archiveSha256: metadata.archiveSha256,
    candidate: metadata.candidate,
    phases,
    editor: editor ? {
      executable: editor.executable,
      pid: editor.pid ?? null,
      processGroupId: editor.processGroupId ?? null,
      exitCode: editor.exitCode ?? null,
      signalCode: editor.signalCode ?? null,
      spawnError: editor.spawnError ?? null,
      stopRequested: Boolean(editor.stopRequested),
      exitedBeforeStop: Boolean(editor.exitedBeforeStop),
      terminationFailed: Boolean(editor.terminationFailed),
    } : null,
    packagedE2e,
    logHazards,
    crashes: {
      newReports: crashReports,
    },
    workspace,
    diagnostics: outcome.diagnostics,
    artifacts: {
      editorLog: "editor.log",
      clientStderrLog: "client.stderr.log",
      runtimeState: "runtime-state",
      crashes: "crashes",
    },
  };
}

export async function runAutomation(options, {
  repoRoot = DEFAULT_REPO_ROOT,
  signal,
  platform = process.platform,
  environment = process.env,
  now = () => new Date(),
} = {}) {
  validateRunOptions(options, platform);
  if (options.profile !== "automation") {
    throw new RunnerUsageError('profile must be "automation"');
  }
  await assertSafeOutputDirectory(options);
  await createFreshOutputDirectory(options.outputDir);

  const paths = {
    automationReport: join(options.outputDir, "automation-report"),
    editorLog: join(options.outputDir, "editor.log"),
    clientStderrLog: join(options.outputDir, "client.stderr.log"),
    runtimeState: join(options.outputDir, "runtime-state"),
    crashes: join(options.outputDir, "crashes"),
    result: join(options.outputDir, "result.json"),
  };
  await Promise.all([
    mkdir(paths.automationReport, { recursive: true }),
    mkdir(paths.runtimeState, { recursive: true }),
    mkdir(join(paths.runtimeState, "tmp"), { recursive: true }),
    mkdir(paths.crashes, { recursive: true }),
    writeFile(paths.editorLog, ""),
    writeFile(paths.clientStderrLog, ""),
  ]);

  const startedAt = now();
  const phases = {};
  let prepared = null;
  let prepareError = null;
  let editor = null;
  let automationReport = {
    present: false,
    valid: false,
    error: "Automation did not run.",
    total: 0,
    passed: 0,
    failed: 0,
    notRun: 0,
    inProcess: 0,
    categoryTests: 0,
  };
  let logHazards = [];
  let crashReports = [];
  const collectErrors = [];
  let cleanupError = null;
  const workspace = {
    path: null,
    retained: false,
    cleanup: "not_created",
    cleanupSkippedReason: null,
  };

  const metadata = await collectRunMetadata({
    repoRoot,
    target: options.target,
    pluginDir: options.pluginDir,
    pluginArchive: options.pluginArchive,
  });

  const preparePhase = await recordPhase(
    phases,
    "prepare",
    options.phaseDeadlineMs,
    signal,
    async (phaseSignal) => prepareCandidateRun({
      ...options,
      paths,
      platform,
      phaseSignal,
    }),
  );
  if (preparePhase.ok) {
    prepared = preparePhase.value;
    metadata.archiveSha256 = prepared.archiveSha256;
    metadata.candidate = prepared.candidate;
    workspace.path = prepared.temporaryRoot;
  } else {
    prepareError = preparePhase.error;
  }

  if (prepared) {
    const phaseStartedAt = Date.now();
    try {
      editor = await runEditorProcess({
        executable: prepared.executable,
        arguments: buildAutomationArguments({
          projectPath: prepared.projectPath,
          reportDirectory: paths.automationReport,
        }),
        workingDirectory: prepared.projectDirectory,
        environment: buildRuntimeEnvironment({
          environment,
          runtimeStateDirectory:
            prepared.runtimeStateDirectory ?? paths.runtimeState,
          projectDirectory: prepared.projectDirectory,
          platform,
        }),
        logPath: paths.editorLog,
        deadlineMs: options.phaseDeadlineMs,
        shutdownGraceMs: options.shutdownGraceMs,
        signal,
        platform,
      });
      phases.automation = {
        status: editor.timedOut ? "timed_out" : "passed",
        durationMs: Date.now() - phaseStartedAt,
      };
    } catch (error) {
      editor = {
        executable: prepared.executable,
        spawnError: serializeError(error),
      };
      phases.automation = {
        status: "failed",
        durationMs: Date.now() - phaseStartedAt,
        error: serializeError(error),
      };
    }
  } else {
    phases.automation = {
      status: "skipped",
      durationMs: 0,
    };
  }

  const collectPhase = await recordPhase(
    phases,
    "collect",
    options.phaseDeadlineMs,
    null,
    async () => {
      try {
        automationReport = await readAutomationReport(paths.automationReport);
      } catch (error) {
        collectErrors.push(error);
      }
      try {
        logHazards = await classifyEditorLogFile(paths.editorLog);
      } catch (error) {
        collectErrors.push(error);
      }
      if (prepared?.crashSnapshot) {
        try {
          const crashAfter = await snapshotCrashLocations(prepared.crashRoots);
          const newEntries = diffCrashSnapshots(
            prepared.crashSnapshot,
            crashAfter,
          );
          crashReports = await archiveCrashEntries(newEntries, paths.crashes);
        } catch (error) {
          collectErrors.push(error);
        }
      }
    },
  );
  if (!collectPhase.ok) collectErrors.push(collectPhase.error);

  const cleanupDecision = decideWorkspaceCleanup(
    prepared?.temporaryRoot,
    editor,
  );
  if (cleanupDecision.action === "retain") {
    workspace.retained = true;
    workspace.cleanup = "skipped";
    workspace.cleanupSkippedReason = cleanupDecision.reason;
    phases.cleanup = {
      status: "skipped",
      durationMs: 0,
      reason: workspace.cleanupSkippedReason,
    };
  } else if (cleanupDecision.action === "remove") {
    const cleanupPhase = await recordPhase(
      phases,
      "cleanup",
      options.phaseDeadlineMs,
      null,
      () => removePreparedWorkspace(prepared),
    );
    if (cleanupPhase.ok) {
      workspace.cleanup = "removed";
    } else {
      cleanupError = cleanupPhase.error;
      workspace.retained = true;
      workspace.cleanup = "failed";
    }
  } else {
    phases.cleanup = {
      status: "skipped",
      durationMs: 0,
    };
  }

  const outcome = classifyRunOutcome({
    prepareError,
    prepareTimedOut: prepareError instanceof PhaseTimeoutError,
    editor,
    automationReport,
    logHazards,
    newCrashReports: crashReports,
    collectErrors,
    cleanupError,
  });
  if (outcome.firstFailingPhase && phases[outcome.firstFailingPhase]) {
    phases[outcome.firstFailingPhase].status = outcome.status;
  }

  const result = buildResultDocument({
    startedAt,
    finishedAt: now(),
    metadata,
    phases,
    outcome,
    editor,
    automationReport,
    logHazards,
    crashReports,
    workspace,
  });
  await writeFile(paths.result, `${JSON.stringify(result, null, 2)}\n`);
  return result;
}

export async function runPackagedE2E(options, {
  repoRoot = DEFAULT_REPO_ROOT,
  signal,
  platform = process.platform,
  environment = process.env,
  now = () => new Date(),
  prepareCandidate = prepareCandidateRun,
  smokeRunner = defaultPackagedMcpSmoke,
  editorControllerFactory = createOwnedEditorController,
} = {}) {
  validateRunOptions(options, platform);
  if (options.profile !== "packaged_e2e") {
    throw new RunnerUsageError('profile must be "packaged_e2e"');
  }
  await assertSafeOutputDirectory(options);
  await createFreshOutputDirectory(options.outputDir);

  const paths = {
    editorLog: join(options.outputDir, "editor.log"),
    clientStderrLog: join(options.outputDir, "client.stderr.log"),
    runtimeState: join(options.outputDir, "runtime-state"),
    crashes: join(options.outputDir, "crashes"),
    result: join(options.outputDir, "result.json"),
  };
  const runtimeTemporaryDirectory = join(paths.runtimeState, "tmp");
  await Promise.all([
    mkdir(runtimeTemporaryDirectory, { recursive: true }),
    mkdir(paths.crashes, { recursive: true }),
    writeFile(paths.editorLog, ""),
    writeFile(paths.clientStderrLog, ""),
  ]);

  const startedAt = now();
  const phases = {};
  const metadata = await collectRunMetadata({
    repoRoot,
    target: options.target,
    pluginDir: options.pluginDir,
    pluginArchive: options.pluginArchive,
  });
  const workspace = {
    path: null,
    retained: false,
    cleanup: "not_created",
    cleanupSkippedReason: null,
  };
  let prepared = null;
  let prepareError = null;
  let smokeError = null;
  let editorController = null;
  let editor = null;
  let packagedE2e = { steps: [] };
  let logHazards = [];
  let crashReports = [];
  let clientStderrWriteError = null;
  let smokeCleanupUnsettled = false;
  let unsafeCleanupReason = null;
  const collectErrors = [];
  let cleanupError = null;

  const preparePhase = await recordPhase(
    phases,
    "prepare",
    options.phaseDeadlineMs,
    signal,
    async (phaseSignal) => prepareCandidate({
      ...options,
      paths,
      platform,
      phaseSignal,
      requirePackagedClient: true,
    }),
  );
  if (preparePhase.ok) {
    prepared = preparePhase.value;
    workspace.path = prepared.temporaryRoot;
    metadata.archiveSha256 = prepared.archiveSha256;
    metadata.productVersion = prepared.versionName;
    metadata.candidate = prepared.candidate;
  } else {
    prepareError = preparePhase.error;
  }

  if (prepared) {
    const observedSteps = [];
    let smokeSettlement = null;
    const smokeSetupStartedAt = Date.now();
    try {
      const editorEnvironment = buildRuntimeEnvironment({
        environment,
        runtimeStateDirectory:
          prepared.runtimeStateDirectory ?? paths.runtimeState,
        projectDirectory: prepared.projectDirectory,
        platform,
      });
      editorController = editorControllerFactory({
        executable: prepared.executable,
        arguments: buildPackagedEditorArguments({
          projectPath: prepared.projectPath,
        }),
        workingDirectory: prepared.projectDirectory,
        environment: editorEnvironment,
        logPath: paths.editorLog,
        shutdownGraceMs: options.shutdownGraceMs,
        platform,
      });
      const smokePhase = await recordPhase(
        phases,
        "packaged_smoke",
        options.phaseDeadlineMs,
        signal,
        (phaseSignal) => {
          const operation = Promise.resolve().then(() => smokeRunner({
            projectRoot: prepared.projectDirectory,
            fixture: prepared.fixture,
            expectedServerVersion: prepared.versionName,
            clientExecutable: prepared.clientExecutable,
            clientWorkingDirectory: prepared.projectDirectory,
            clientEnvironment: editorEnvironment,
            runtimeLifecycleStateRoot: join(
              paths.runtimeState,
              "packaged-client-lifecycle",
            ),
            platform,
            signal: phaseSignal,
            cleanupTimeoutMs: options.shutdownGraceMs,
            stderrSink: (text) => {
              try {
                appendFileSync(paths.clientStderrLog, String(text), "utf8");
              } catch (error) {
                clientStderrWriteError ??= error instanceof Error
                  ? error
                  : new Error(String(error));
              }
            },
            startEditor: async () => {
              const state = await editorController.start();
              if (state.spawnError) {
                throw new Error(
                  `Unreal Editor failed to start: ${state.spawnError.message}`,
                );
              }
            },
            stopEditor: async () => {
              const state = await editorController.stop();
              if (state.terminationFailed) {
                throw new Error(
                  `owned Unreal Editor process ${state.pid} did not terminate`,
                );
              }
            },
            onStep: (step) => {
              if (step.status !== "started") observedSteps.push(step);
            },
          }));
          smokeSettlement = operation.then(
            (value) => ({ ok: true, value }),
            (error) => ({ ok: false, error }),
          );
          return operation;
        },
      );
      if (smokePhase.ok) {
        const { clientStderr: _clientStderr, ...smokeResult } = smokePhase.value;
        packagedE2e = smokeResult;
      } else {
        smokeError = smokePhase.error;
        packagedE2e = { steps: observedSteps };
      }
    } catch (error) {
      smokeError ??= error instanceof Error ? error : new Error(String(error));
      phases.packaged_smoke ??= {
        status: "failed",
        durationMs: Date.now() - smokeSetupStartedAt,
        error: serializeError(smokeError),
      };
      packagedE2e = { steps: observedSteps };
    } finally {
      if (smokeSettlement) {
        const settlement = await settleWithin(
          smokeSettlement,
          options.shutdownGraceMs,
        );
        if (!settlement) {
          smokeCleanupUnsettled = true;
          unsafeCleanupReason = "smoke_cleanup_unsettled";
        }
      }
      if (editorController) {
        try {
          editor = await editorController.stop();
        } catch (error) {
          smokeError ??= error instanceof Error
            ? error
            : new Error(String(error));
          editor = editorController.state();
        }
        editor = editorController.state();
      }
    }
  } else {
    phases.packaged_smoke = {
      status: "skipped",
      durationMs: 0,
    };
  }

  const collectPhase = await recordPhase(
    phases,
    "collect",
    options.phaseDeadlineMs,
    null,
    async () => {
      if (clientStderrWriteError) collectErrors.push(clientStderrWriteError);
      try {
        logHazards = await classifyEditorLogFile(paths.editorLog);
      } catch (error) {
        collectErrors.push(error);
      }
      if (prepared?.crashSnapshot) {
        try {
          const shouldWaitForStableCrashArtifacts = Boolean(
            editor?.exitedBeforeStop
              || editor?.terminationFailed
              || smokeError
              || logHazards.length > 0,
          );
          const crashObservation = shouldWaitForStableCrashArtifacts
            ? await waitForStableCrashSnapshot(prepared.crashRoots, {
              deadlineMs: options.shutdownGraceMs,
            })
            : {
              stable: true,
              snapshot: await snapshotCrashLocations(prepared.crashRoots),
            };
          if (!crashObservation.stable) {
            unsafeCleanupReason ??= "crash_artifacts_unstable";
            throw new Error(
              "crash locations did not become stable before collection deadline",
            );
          }
          const crashAfter = crashObservation.snapshot;
          const newEntries = diffCrashSnapshots(
            prepared.crashSnapshot,
            crashAfter,
          );
          crashReports = await archiveCrashEntries(newEntries, paths.crashes);
          const crashAfterArchive = await snapshotCrashLocations(
            prepared.crashRoots,
          );
          if (!sameCrashSnapshot(crashAfter, crashAfterArchive)) {
            unsafeCleanupReason ??= "crash_artifacts_changed_during_copy";
            throw new Error(
              "crash locations changed while artifacts were being copied",
            );
          }
        } catch (error) {
          collectErrors.push(error);
        }
      }
    },
  );
  if (!collectPhase.ok) collectErrors.push(collectPhase.error);

  const cleanupDecision = unsafeCleanupReason && prepared?.temporaryRoot
    ? {
      action: "retain",
      reason: unsafeCleanupReason,
    }
    : decideWorkspaceCleanup(prepared?.temporaryRoot, editor);
  if (cleanupDecision.action === "retain") {
    workspace.retained = true;
    workspace.cleanup = "skipped";
    workspace.cleanupSkippedReason = cleanupDecision.reason;
    phases.cleanup = {
      status: "skipped",
      durationMs: 0,
      reason: cleanupDecision.reason,
    };
  } else if (cleanupDecision.action === "remove") {
    const cleanupPhase = await recordPhase(
      phases,
      "cleanup",
      options.phaseDeadlineMs,
      null,
      () => removePreparedWorkspace(prepared),
    );
    if (cleanupPhase.ok) {
      workspace.cleanup = "removed";
    } else {
      cleanupError = cleanupPhase.error;
      workspace.retained = true;
      workspace.cleanup = "failed";
    }
  } else {
    phases.cleanup = {
      status: "skipped",
      durationMs: 0,
    };
  }

  const outcome = classifyPackagedE2EOutcome({
    prepareError,
    prepareTimedOut: prepareError instanceof PhaseTimeoutError,
    smokeError,
    smokeTimedOut: smokeError instanceof PhaseTimeoutError,
    editor,
    smokeCleanupUnsettled,
    logHazards,
    newCrashReports: crashReports,
    collectErrors,
    cleanupError,
  });
  if (outcome.firstFailingPhase && phases[outcome.firstFailingPhase]) {
    phases[outcome.firstFailingPhase].status = outcome.status;
  }
  const result = buildPackagedE2EResultDocument({
    startedAt,
    finishedAt: now(),
    metadata,
    phases,
    outcome,
    editor,
    packagedE2e,
    logHazards,
    crashReports,
    workspace,
  });
  await writeFile(paths.result, `${JSON.stringify(result, null, 2)}\n`);
  return result;
}

function validateRunOptions(options, platform) {
  if (!options || !SUPPORTED_PROFILES.has(options.profile)) {
    throw new RunnerUsageError(
      `profile must be one of ${[...SUPPORTED_PROFILES].join(", ")}`,
    );
  }
  const targetSpec = TARGETS.get(options.target);
  if (!targetSpec) {
    throw new RunnerUsageError(`unsupported target: ${options.target}`);
  }
  if (targetSpec.hostPlatform !== platform) {
    throw new RunnerUsageError(
      `target ${options.target} requires host platform ${targetSpec.hostPlatform}; current platform is ${platform}`,
    );
  }
  if (!options.ueRoot || !options.projectTemplate || !options.outputDir) {
    throw new RunnerUsageError(
      "ueRoot, projectTemplate, and outputDir are required",
    );
  }
  if (Boolean(options.pluginDir) === Boolean(options.pluginArchive)) {
    throw new RunnerUsageError(
      "provide exactly one pluginDir or pluginArchive",
    );
  }
  if (!Number.isFinite(options.phaseDeadlineMs) || options.phaseDeadlineMs <= 0) {
    throw new RunnerUsageError("phaseDeadlineMs must be positive");
  }
  if (!Number.isFinite(options.shutdownGraceMs) || options.shutdownGraceMs <= 0) {
    throw new RunnerUsageError("shutdownGraceMs must be positive");
  }
}

async function assertSafeOutputDirectory(options) {
  const outputDirectory = await canonicalizeForContainment(options.outputDir);
  if (dirname(outputDirectory) === outputDirectory) {
    throw new RunnerUsageError("output directory must not be a filesystem root");
  }
  for (const input of [
    options.ueRoot,
    options.projectTemplate,
    options.pluginDir,
    options.pluginArchive,
  ].filter(Boolean)) {
    const canonicalInput = await canonicalizeForContainment(input);
    if (pathsOverlap(outputDirectory, canonicalInput)) {
      throw new RunnerUsageError(
        `output directory must not overlap an input: ${input}`,
      );
    }
  }
}

async function canonicalizeForContainment(path) {
  let existingAncestor = resolve(path);
  const missingSegments = [];
  while (true) {
    try {
      const canonicalAncestor = await realpath(existingAncestor);
      return resolve(canonicalAncestor, ...missingSegments);
    } catch (error) {
      if (error?.code !== "ENOENT" && error?.code !== "ENOTDIR") throw error;
      const parent = dirname(existingAncestor);
      if (parent === existingAncestor) throw error;
      missingSegments.unshift(basename(existingAncestor));
      existingAncestor = parent;
    }
  }
}

function pathsOverlap(first, second) {
  return isWithin(first, second) || isWithin(second, first);
}

function isWithin(candidate, parent) {
  const pathFromParent = relative(parent, candidate);
  return pathFromParent === ""
    || (pathFromParent !== ".."
      && !pathFromParent.startsWith(`..${sep}`)
      && !isAbsolute(pathFromParent));
}

async function createFreshOutputDirectory(outputDirectory) {
  try {
    await lstat(outputDirectory);
    throw new RunnerUsageError(
      `output directory must not already exist: ${outputDirectory}`,
    );
  } catch (error) {
    if (error instanceof RunnerUsageError) throw error;
    if (error?.code !== "ENOENT") throw error;
  }

  await mkdir(dirname(outputDirectory), { recursive: true });
  try {
    await mkdir(outputDirectory);
  } catch (error) {
    if (error?.code === "EEXIST") {
      throw new RunnerUsageError(
        `output directory must not already exist: ${outputDirectory}`,
      );
    }
    throw error;
  }
}

export async function collectRunMetadata({
  repoRoot,
  target,
  pluginDir,
  pluginArchive,
}) {
  let productVersion = "unknown";
  let protocolVersion = null;
  try {
    const packageJson = JSON.parse(
      await readFile(join(repoRoot, "package.json"), "utf8"),
    );
    productVersion = packageJson.version ?? productVersion;
    protocolVersion = packageJson.loomle?.protocolVersion ?? null;
  } catch {
    // A result is still useful when the runner is copied outside the repository.
  }

  return {
    productVersion,
    protocolVersion,
    commit: await readGitCommit(repoRoot),
    target,
    archiveSha256: null,
    candidate: pluginArchive
      ? { kind: "archive", path: pluginArchive }
      : { kind: "directory", path: pluginDir },
  };
}

async function readGitCommit(repoRoot) {
  try {
    const result = await runCapturedCommand(
      "git",
      ["rev-parse", "HEAD"],
      {
        workingDirectory: repoRoot,
        deadlineMs: 5000,
      },
    );
    return result.exitCode === 0 ? result.stdout.trim() || null : null;
  } catch {
    return null;
  }
}

async function prepareCandidateRun({
  ueRoot,
  projectTemplate,
  pluginDir,
  pluginArchive,
  target,
  paths,
  platform,
  phaseDeadlineMs,
  shutdownGraceMs,
  phaseSignal,
  requirePackagedClient = false,
}) {
  throwIfAborted(phaseSignal);
  await assertDirectory(ueRoot, "UE root");
  await assertDirectory(projectTemplate, "project template");
  let pluginDescriptorPath = null;
  if (pluginDir) {
    pluginDescriptorPath = await validatePluginDirectoryCandidate(
      pluginDir,
      target,
    );
  } else {
    await assertFile(pluginArchive, "plugin archive");
  }
  throwIfAborted(phaseSignal);

  const executable = await resolveUnrealExecutable(ueRoot, target);
  throwIfAborted(phaseSignal);
  const temporaryRoot = await mkdtemp(join(
    tmpdir(),
    requirePackagedClient ? "loomle-ue-packaged-e2e-" : "loomle-ue-automation-",
  ));
  const projectDirectory = join(temporaryRoot, "project");
  let runtimeHome = null;

  try {
    runtimeHome = await createRuntimeStateHome({
      runtimeStateDirectory: paths.runtimeState,
      platform,
    });
    await cp(projectTemplate, projectDirectory, {
      recursive: true,
      errorOnExist: true,
      force: false,
    });
    throwIfAborted(phaseSignal);
    const projectPath = await findSingleRootFile(
      projectDirectory,
      ".uproject",
      "project template",
    );

    const stagedPlugin = pluginDir
      ? await stagePluginDirectory(pluginDescriptorPath, projectDirectory)
      : await stagePluginArchive({
        archivePath: pluginArchive,
        temporaryRoot,
        projectDirectory,
        target,
        platform,
        deadlineMs: phaseDeadlineMs,
        shutdownGraceMs,
        signal: phaseSignal,
      });
    throwIfAborted(phaseSignal);
    await enableProjectPlugin(projectPath, stagedPlugin.name);
    throwIfAborted(phaseSignal);

    const descriptor = await readPluginDescriptor(stagedPlugin.descriptorPath);
    const packagedCandidate = requirePackagedClient
      ? await validatePackagedPluginDirectoryCandidate(
        stagedPlugin.directory,
        target,
      )
      : null;
    const versionName = packagedCandidate?.versionName
      ?? (typeof descriptor.VersionName === "string"
        ? descriptor.VersionName.trim() || null
        : null);
    const clientExecutable = packagedCandidate?.clientExecutable ?? null;
    const fixture = requirePackagedClient
      ? await loadFixtureManifest(projectDirectory)
      : null;
    throwIfAborted(phaseSignal);

    const engineVersion = await readEngineVersion(ueRoot);
    const crashRoots = await buildCrashRoots({
      projectDirectory,
      engineVersion,
      runtimeStateDirectory: runtimeHome.home,
      platform,
    });
    const crashSnapshot = await snapshotCrashLocations(crashRoots);
    throwIfAborted(phaseSignal);
    const archiveSha256 = await hashCandidateArchive(
      pluginArchive,
      phaseSignal,
    );
    throwIfAborted(phaseSignal);

    return {
      executable,
      temporaryRoot,
      runtimeStateDirectory: runtimeHome.home,
      runtimeAliasRoot: runtimeHome.aliasRoot,
      projectDirectory,
      projectPath,
      crashRoots,
      crashSnapshot,
      archiveSha256,
      versionName,
      clientExecutable,
      fixture,
      candidate: pluginArchive
        ? {
          kind: "archive",
          path: pluginArchive,
          pluginName: stagedPlugin.name,
          ...(versionName ? { versionName } : {}),
        }
        : {
          kind: "directory",
          path: pluginDir,
          pluginName: stagedPlugin.name,
          ...(versionName ? { versionName } : {}),
        },
    };
  } catch (error) {
    await Promise.allSettled([
      rm(temporaryRoot, { recursive: true, force: true }),
      runtimeHome?.aliasRoot
        ? rm(runtimeHome.aliasRoot, { recursive: true, force: true })
        : Promise.resolve(),
    ]);
    throw error;
  }
}

export async function createRuntimeStateHome({
  runtimeStateDirectory,
  platform,
  aliasParent = "/tmp",
}) {
  const durableHome = resolve(runtimeStateDirectory);
  await mkdir(durableHome, { recursive: true });
  if (platform !== "darwin") {
    return {
      home: durableHome,
      aliasRoot: null,
    };
  }

  const aliasRoot = await mkdtemp(join(aliasParent, "lm-"));
  const home = join(aliasRoot, "h");
  try {
    await symlink(durableHome, home, "dir");
    return {
      home,
      aliasRoot,
    };
  } catch (error) {
    await rm(aliasRoot, { recursive: true, force: true });
    throw error;
  }
}

async function removePreparedWorkspace(prepared) {
  const paths = [
    prepared?.temporaryRoot,
    prepared?.runtimeAliasRoot,
  ].filter(Boolean);
  const results = await Promise.allSettled(paths.map(
    (path) => rm(path, { recursive: true, force: true }),
  ));
  const failures = results
    .map((result, index) => ({ result, path: paths[index] }))
    .filter(({ result }) => result.status === "rejected");
  if (failures.length > 0) {
    throw new AggregateError(
      failures.map(({ result }) => result.reason),
      `could not remove runner workspace paths: ${
        failures.map(({ path }) => path).join(", ")
      }`,
    );
  }
}

function throwIfAborted(signal) {
  if (!signal?.aborted) return;
  throw signal.reason instanceof Error
    ? signal.reason
    : new Error("runner interrupted");
}

async function resolveUnrealExecutable(ueRoot, target) {
  const targetSpec = TARGETS.get(target);
  const binariesRoot = join(
    ueRoot,
    "Engine",
    "Binaries",
    targetSpec.binariesDirectory,
  );
  for (const commandPath of targetSpec.commandPaths) {
    const candidate = join(binariesRoot, commandPath);
    try {
      await access(
        candidate,
        targetSpec.hostPlatform === "win32"
          ? fsConstants.F_OK
          : fsConstants.X_OK,
      );
      return candidate;
    } catch {
      // Try the non-commandlet executable before failing.
    }
  }
  throw new Error(
    `Unreal Editor executable not found below ${binariesRoot}; tried ${targetSpec.commandPaths.join(", ")}`,
  );
}

async function stagePluginDirectory(descriptorPath, projectDirectory) {
  return copyPluginIntoProject({
    pluginRoot: dirname(descriptorPath),
    descriptorPath,
    projectDirectory,
  });
}

async function stagePluginArchive({
  archivePath,
  temporaryRoot,
  projectDirectory,
  target,
  platform,
  deadlineMs,
  shutdownGraceMs,
  signal,
}) {
  const extractionDirectory = join(temporaryRoot, "plugin-archive");
  await mkdir(extractionDirectory, { recursive: true });
  const command = archiveExtractionCommand(
    archivePath,
    extractionDirectory,
    platform,
  );
  const result = await runCapturedCommand(command.executable, command.arguments, {
    workingDirectory: extractionDirectory,
    deadlineMs,
    shutdownGraceMs,
    signal,
  });
  if (result.timedOut) {
    throw new PhaseTimeoutError("prepare", deadlineMs);
  }
  if (result.exitCode !== 0) {
    throw new Error(
      `plugin archive extraction failed with exit code ${result.exitCode}: ${result.stderr.trim()}`,
    );
  }

  const descriptors = await findFilesRecursively(
    extractionDirectory,
    (path) => extname(path).toLowerCase() === ".uplugin",
  );
  if (descriptors.length !== 1) {
    throw new Error(
      `plugin archive must contain exactly one .uplugin descriptor; found ${descriptors.length}`,
    );
  }
  await validatePluginDescriptorCandidate(descriptors[0], target);
  return copyPluginIntoProject({
    pluginRoot: dirname(descriptors[0]),
    descriptorPath: descriptors[0],
    projectDirectory,
  });
}

export async function validatePluginDirectoryCandidate(pluginDirectory, target) {
  await assertDirectory(pluginDirectory, "compiled plugin directory");
  const descriptorPath = await findSingleRootFile(
    pluginDirectory,
    ".uplugin",
    "compiled plugin directory",
  );
  await validatePluginDescriptorCandidate(descriptorPath, target);
  return descriptorPath;
}

export async function validatePackagedPluginDirectoryCandidate(
  pluginDirectory,
  target,
) {
  const descriptorPath = await validatePluginDirectoryCandidate(
    pluginDirectory,
    target,
  );
  const descriptor = await readPluginDescriptor(descriptorPath);
  return {
    descriptorPath,
    versionName: requirePluginVersionName(descriptor, descriptorPath),
    clientExecutable: await validateBundledClient(pluginDirectory, target),
  };
}

async function validatePluginDescriptorCandidate(descriptorPath, target) {
  const targetSpec = TARGETS.get(target);
  if (!targetSpec) {
    throw new RunnerUsageError(`unsupported target: ${target}`);
  }

  const descriptor = await readPluginDescriptor(descriptorPath);

  if (!Array.isArray(descriptor.Modules)) {
    throw new RunnerUsageError(
      `plugin descriptor must declare a Modules array: ${descriptorPath}`,
    );
  }

  const modules = descriptor.Modules.filter(
    (module) => module && typeof module === "object",
  );
  const loomleModule = modules.find((module) => module.Name === "LoomleBridge");
  if (!loomleModule) {
    throw new RunnerUsageError(
      `compiled Loomle candidate must declare the LoomleBridge module: ${descriptorPath}`,
    );
  }

  const requiredModules = new Map();
  for (const module of modules) {
    const moduleName = typeof module.Name === "string" ? module.Name.trim() : "";
    const moduleType = typeof module.Type === "string" ? module.Type : "";
    if (!moduleName) continue;
    if (moduleName === "LoomleBridge"
        || (moduleType.toLowerCase().startsWith("editor")
          && moduleSupportsPlatform(module, targetSpec.unrealPlatform))) {
      requiredModules.set(moduleName, module);
    }
  }

  const pluginRoot = dirname(descriptorPath);
  for (const moduleName of requiredModules.keys()) {
    const relativeBinaryPath = join(
      "Binaries",
      targetSpec.binariesDirectory,
      targetSpec.moduleBinaryName(moduleName),
    );
    try {
      await assertFile(
        join(pluginRoot, relativeBinaryPath),
        `compiled ${moduleName} module binary`,
      );
    } catch {
      throw new RunnerUsageError([
        `plugin candidate is not compiled for ${target}; missing ${relativeBinaryPath}.`,
        "Pass BuildPlugin output or a compiled HostProject plugin directory, not raw source.",
      ].join(" "));
    }
  }
}

async function readPluginDescriptor(descriptorPath) {
  let descriptor;
  try {
    descriptor = JSON.parse(await readFile(descriptorPath, "utf8"));
  } catch (error) {
    throw new RunnerUsageError(
      `invalid plugin descriptor ${descriptorPath}: ${error.message}`,
    );
  }
  if (!descriptor || typeof descriptor !== "object" || Array.isArray(descriptor)) {
    throw new RunnerUsageError(
      `plugin descriptor root must be an object: ${descriptorPath}`,
    );
  }
  return descriptor;
}

function requirePluginVersionName(descriptor, descriptorPath) {
  const versionName = typeof descriptor.VersionName === "string"
    ? descriptor.VersionName.trim()
    : "";
  if (!versionName) {
    throw new RunnerUsageError(
      `packaged candidate must declare a non-empty VersionName: ${descriptorPath}`,
    );
  }
  return versionName;
}

export async function validateBundledClient(pluginDirectory, target) {
  const targetSpec = TARGETS.get(target);
  if (!targetSpec) {
    throw new RunnerUsageError(`unsupported target: ${target}`);
  }
  const clientPath = join(pluginDirectory, targetSpec.bundledClientPath);
  try {
    await access(
      clientPath,
      targetSpec.hostPlatform === "win32"
        ? fsConstants.F_OK
        : fsConstants.X_OK,
    );
  } catch {
    throw new RunnerUsageError(
      `packaged candidate is missing executable bundled Client ${targetSpec.bundledClientPath}`,
    );
  }
  return clientPath;
}

export async function loadFixtureManifest(projectDirectory) {
  const manifestPath = join(projectDirectory, FIXTURE_MANIFEST_NAME);
  let manifest;
  try {
    manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  } catch (error) {
    throw new RunnerUsageError(
      `invalid or missing packaged E2E fixture manifest ${manifestPath}: ${error.message}`,
    );
  }
  if (!manifest || typeof manifest !== "object" || Array.isArray(manifest)) {
    throw new RunnerUsageError(
      `packaged E2E fixture manifest root must be an object: ${manifestPath}`,
    );
  }
  if (manifest.schemaVersion !== 1) {
    throw new RunnerUsageError(
      `unsupported packaged E2E fixture schemaVersion ${JSON.stringify(manifest.schemaVersion)}; expected 1`,
    );
  }
  return {
    schemaVersion: 1,
    ...normalizeFixtureManifest(manifest),
  };
}

function normalizeFixtureManifest(manifest) {
  const required = (name) => {
    const value = manifest[name];
    if (typeof value !== "string" || !value.trim()) {
      throw new RunnerUsageError(
        `packaged E2E fixture ${name} must be a non-empty string`,
      );
    }
    return value;
  };
  const blueprintAssetPath = required("blueprintAssetPath");
  const objectSeparator = blueprintAssetPath.lastIndexOf(".");
  if (!blueprintAssetPath.startsWith("/Game/") || objectSeparator === -1) {
    throw new RunnerUsageError(
      "packaged E2E fixture blueprintAssetPath must be an exact /Game package object path",
    );
  }
  return {
    blueprintAssetPath,
    blueprintDescription: required("blueprintDescription"),
    assetType: manifest.assetType === undefined
      ? "/Script/Engine.Blueprint"
      : required("assetType"),
    assetSearchText: manifest.assetSearchText === undefined
      ? blueprintAssetPath.slice(objectSeparator + 1)
      : required("assetSearchText"),
    assetRoot: manifest.assetRoot === undefined
      ? "/Game"
      : required("assetRoot"),
  };
}

async function defaultPackagedMcpSmoke(options) {
  const { runPackagedMcpSmoke } = await import(
    "../integration/packaged-mcp-smoke.mjs"
  );
  const smoke = await runPackagedMcpSmoke(options);
  const { runPackagedRuntimeLifecycle } = await import(
    "../integration/packaged-runtime-lifecycle.mjs"
  );
  const runtimeLifecycle = await runPackagedRuntimeLifecycle({
    projectRoot: options.projectRoot,
    fixture: options.fixture,
    stateRoot: options.runtimeLifecycleStateRoot,
    clientExecutable: options.clientExecutable,
    clientWorkingDirectory: options.clientWorkingDirectory,
    clientEnvironment: options.clientEnvironment,
    platform: options.platform,
    signal: options.signal,
    connectTimeoutMs: options.connectTimeoutMs,
    requestTimeoutMs: options.requestTimeoutMs,
    cleanupTimeoutMs: options.cleanupTimeoutMs,
    stderrSink: options.stderrSink,
    onScenario: options.onStep,
  });
  return {
    ...smoke,
    runtimeLifecycle,
  };
}

function moduleSupportsPlatform(module, unrealPlatform) {
  const allowList = Array.isArray(module.PlatformAllowList)
    ? module.PlatformAllowList
    : (Array.isArray(module.WhitelistPlatforms)
      ? module.WhitelistPlatforms
      : null);
  if (allowList && !allowList.includes(unrealPlatform)) return false;

  const denyList = Array.isArray(module.PlatformDenyList)
    ? module.PlatformDenyList
    : (Array.isArray(module.BlacklistPlatforms)
      ? module.BlacklistPlatforms
      : []);
  return !denyList.includes(unrealPlatform);
}

function archiveExtractionCommand(archivePath, destination, platform) {
  const lowerName = archivePath.toLowerCase();
  if (lowerName.endsWith(".zip")) {
    if (platform === "darwin") {
      return {
        executable: "/usr/bin/ditto",
        arguments: ["-x", "-k", archivePath, destination],
      };
    }
    if (platform === "win32") {
      return {
        executable: "tar.exe",
        arguments: ["-xf", archivePath, "-C", destination],
      };
    }
    throw new RunnerUsageError(
      `host platform ${platform} is not supported by this runner`,
    );
  }
  if (lowerName.endsWith(".tar")
      || lowerName.endsWith(".tar.gz")
      || lowerName.endsWith(".tgz")) {
    return {
      executable: platform === "win32" ? "tar.exe" : "tar",
      arguments: ["-xf", archivePath, "-C", destination],
    };
  }
  throw new RunnerUsageError(
    `unsupported plugin archive format: ${archivePath}; expected .zip, .tar, .tar.gz, or .tgz`,
  );
}

async function copyPluginIntoProject({
  pluginRoot,
  descriptorPath,
  projectDirectory,
}) {
  const pluginName = basename(descriptorPath, extname(descriptorPath));
  const pluginDestination = join(
    projectDirectory,
    "Plugins",
    pluginName,
  );
  await mkdir(dirname(pluginDestination), { recursive: true });
  await cp(pluginRoot, pluginDestination, {
    recursive: true,
    filter: (path) => !PLUGIN_COPY_IGNORES.has(basename(path)),
  });
  return {
    name: pluginName,
    directory: pluginDestination,
    descriptorPath: join(pluginDestination, basename(descriptorPath)),
  };
}

async function enableProjectPlugin(projectPath, pluginName) {
  const project = JSON.parse(await readFile(projectPath, "utf8"));
  const plugins = Array.isArray(project.Plugins) ? project.Plugins : [];
  const existing = plugins.find((plugin) => plugin?.Name === pluginName);
  if (existing) {
    existing.Enabled = true;
  } else {
    plugins.push({
      Name: pluginName,
      Enabled: true,
      TargetAllowList: ["Editor"],
    });
  }
  project.Plugins = plugins;
  await writeFile(projectPath, `${JSON.stringify(project, null, 2)}\n`);
}

async function findSingleRootFile(directory, extension, label) {
  const entries = await readdir(directory, { withFileTypes: true });
  const matches = entries
    .filter((entry) => entry.isFile()
      && extname(entry.name).toLowerCase() === extension)
    .map((entry) => join(directory, entry.name));
  if (matches.length !== 1) {
    throw new Error(
      `${label} must contain exactly one root ${extension} file; found ${matches.length}`,
    );
  }
  return matches[0];
}

async function findFilesRecursively(directory, predicate) {
  const results = [];
  const pending = [directory];
  while (pending.length > 0) {
    const current = pending.pop();
    const entries = await readdir(current, { withFileTypes: true });
    for (const entry of entries) {
      const path = join(current, entry.name);
      if (entry.isDirectory()) pending.push(path);
      else if (entry.isFile() && predicate(path)) results.push(path);
    }
  }
  return results;
}

async function assertDirectory(path, label) {
  let pathStat;
  try {
    pathStat = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") throw new Error(`${label} not found: ${path}`);
    throw error;
  }
  if (!pathStat.isDirectory()) throw new Error(`${label} is not a directory: ${path}`);
}

async function assertFile(path, label) {
  let pathStat;
  try {
    pathStat = await stat(path);
  } catch (error) {
    if (error?.code === "ENOENT") throw new Error(`${label} not found: ${path}`);
    throw error;
  }
  if (!pathStat.isFile()) throw new Error(`${label} is not a file: ${path}`);
}

async function readEngineVersion(ueRoot) {
  try {
    const version = JSON.parse(
      await readFile(
        join(ueRoot, "Engine", "Build", "Build.version"),
        "utf8",
      ),
    );
    if (Number.isInteger(version.MajorVersion)
        && Number.isInteger(version.MinorVersion)) {
      return `${version.MajorVersion}.${version.MinorVersion}`;
    }
  } catch {
    // Project-local crash detection remains active without a build version.
  }
  return null;
}

export async function buildCrashRoots({
  projectDirectory,
  engineVersion,
  runtimeStateDirectory,
  platform,
}) {
  const roots = [join(projectDirectory, "Saved", "Crashes")];

  if (platform === "darwin") {
    if (engineVersion) {
      roots.push(join(
        runtimeStateDirectory,
        "Library",
        "Application Support",
        "Epic",
        "UnrealEngine",
        engineVersion,
        "Saved",
        "Crashes",
      ));
    }
    roots.push(join(
      runtimeStateDirectory,
      "Library",
      "Application Support",
      "Epic",
      "CrashReportClient",
      "Saved",
      "Crashes",
    ));
  }
  return [...new Set(roots.map((path) => resolve(path)))];
}

async function snapshotCrashLocations(crashRoots) {
  const entries = new Map();
  for (const root of crashRoots) {
    let children;
    try {
      children = await readdir(root, { withFileTypes: true });
    } catch (error) {
      if (error?.code === "ENOENT") continue;
      throw new Error(`could not inspect crash location ${root}: ${error.message}`);
    }
    for (const child of children) {
      const sourcePath = join(root, child.name);
      const childStat = await lstat(sourcePath);
      entries.set(sourcePath, {
        sourcePath,
        root,
        name: child.name,
        signature: await filesystemTreeSignature(sourcePath, childStat),
      });
    }
  }
  return entries;
}

async function filesystemTreeSignature(root, rootStat) {
  const records = [];
  const pending = [{
    path: root,
    relativePath: ".",
    pathStat: rootStat,
  }];
  while (pending.length > 0) {
    const current = pending.pop();
    records.push([
      current.relativePath,
      current.pathStat.isDirectory() ? "directory" : "file",
      current.pathStat.size,
      current.pathStat.mtimeMs,
    ].join(":"));
    if (!current.pathStat.isDirectory()) continue;
    const children = await readdir(current.path, { withFileTypes: true });
    children.sort((left, right) => left.name.localeCompare(right.name));
    for (let index = children.length - 1; index >= 0; index -= 1) {
      const childPath = join(current.path, children[index].name);
      pending.push({
        path: childPath,
        relativePath: join(current.relativePath, children[index].name),
        pathStat: await lstat(childPath),
      });
    }
  }
  return createHash("sha256").update(records.join("\n")).digest("hex");
}

function sameCrashSnapshot(left, right) {
  if (left.size !== right.size) return false;
  for (const [path, entry] of left) {
    if (right.get(path)?.signature !== entry.signature) return false;
  }
  return true;
}

export async function waitForStableCrashSnapshot(crashRoots, {
  deadlineMs,
  quietMs = Math.min(1000, Math.max(25, deadlineMs / 4)),
  pollIntervalMs = Math.min(100, quietMs),
  now = Date.now,
  sleep = delay,
  snapshot = snapshotCrashLocations,
} = {}) {
  if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
    throw new Error("crash stability deadlineMs must be positive");
  }
  const deadline = now() + deadlineMs;
  let current = await snapshot(crashRoots);
  let stableSince = now();
  while (now() < deadline) {
    await sleep(Math.min(pollIntervalMs, Math.max(0, deadline - now())));
    const next = await snapshot(crashRoots);
    const observedAt = now();
    if (sameCrashSnapshot(current, next)) {
      if (observedAt - stableSince >= quietMs) {
        return {
          stable: true,
          snapshot: next,
        };
      }
    } else {
      current = next;
      stableSince = observedAt;
    }
  }
  return {
    stable: false,
    snapshot: current,
  };
}

function diffCrashSnapshots(before, after) {
  const changed = [];
  for (const [path, entry] of after) {
    const previous = before.get(path);
    if (!previous || previous.signature !== entry.signature) changed.push(entry);
  }
  return changed;
}

async function archiveCrashEntries(entries, crashOutputDirectory) {
  const archived = [];
  for (let index = 0; index < entries.length; index += 1) {
    const entry = entries[index];
    const safeName = entry.name.replace(/[^a-zA-Z0-9._-]+/g, "_");
    const destinationName = `${String(index + 1).padStart(3, "0")}-${safeName}`;
    const destination = join(crashOutputDirectory, destinationName);
    await cp(entry.sourcePath, destination, {
      recursive: true,
      force: false,
      errorOnExist: true,
    });
    archived.push({
      sourcePath: entry.sourcePath,
      artifactPath: join("crashes", destinationName),
    });
  }
  return archived;
}

export function buildRuntimeEnvironment({
  environment,
  runtimeStateDirectory,
  projectDirectory,
  platform,
}) {
  const appDataDirectory = join(runtimeStateDirectory, "AppData", "Roaming");
  const localAppDataDirectory = join(
    runtimeStateDirectory,
    "AppData",
    "Local",
  );
  const editorEnvironment = {
    ...environment,
    HOME: runtimeStateDirectory,
    USERPROFILE: runtimeStateDirectory,
    CODEX_HOME: join(runtimeStateDirectory, "codex"),
    XDG_CONFIG_HOME: join(runtimeStateDirectory, "xdg", "config"),
    XDG_CACHE_HOME: join(runtimeStateDirectory, "xdg", "cache"),
    XDG_DATA_HOME: join(runtimeStateDirectory, "xdg", "data"),
    XDG_STATE_HOME: join(runtimeStateDirectory, "xdg", "state"),
    APPDATA: appDataDirectory,
    LOCALAPPDATA: localAppDataDirectory,
    TMPDIR: join(runtimeStateDirectory, "tmp"),
    TMP: join(runtimeStateDirectory, "tmp"),
    TEMP: join(runtimeStateDirectory, "tmp"),
    LOOMLE_PROJECT_ROOT: projectDirectory,
  };
  if (platform === "win32") {
    const root = runtimeStateDirectory.slice(0, 2);
    editorEnvironment.HOMEDRIVE = root;
    editorEnvironment.HOMEPATH = runtimeStateDirectory.slice(root.length);
  }
  return editorEnvironment;
}

export function createOwnedEditorController({
  executable,
  arguments: editorArguments,
  workingDirectory,
  environment,
  logPath,
  shutdownGraceMs,
  platform,
  spawnProcess = spawn,
  terminateProcess = terminateOwnedProcess,
  drainExitedProcessGroup = drainResidualOwnedProcessGroup,
}) {
  let child = null;
  let completion = null;
  let completionResult = null;
  let startPromise = null;
  let stopPromise = null;
  let stopRequested = false;
  let shutdownSignalDelivered = false;
  let exitedBeforeStop = false;
  let terminationFailed = false;
  let spawnError = null;

  const state = () => ({
    executable,
    pid: child?.pid ?? null,
    processGroupId: child && platform !== "win32" ? child.pid : null,
    exitCode: completionResult?.exitCode ?? child?.exitCode ?? null,
    signalCode: completionResult?.signalCode ?? child?.signalCode ?? null,
    spawnError: completionResult?.spawnError ?? spawnError,
    stopRequested,
    exitedBeforeStop,
    terminationFailed,
  });

  const start = async () => {
    if (stopRequested) {
      throw new Error("owned Editor controller cannot start after stop");
    }
    if (startPromise) return startPromise;
    startPromise = (async () => {
      const logHandle = await open(logPath, "a");
      try {
        child = spawnProcess(executable, editorArguments, {
          cwd: workingDirectory,
          env: environment,
          detached: platform !== "win32",
          shell: false,
          windowsHide: true,
          stdio: ["ignore", logHandle.fd, logHandle.fd],
        });
        completion = childCompletion(child);
        completion.then((result) => {
          completionResult = result;
          if (!shutdownSignalDelivered) exitedBeforeStop = true;
        });
        await childSpawned(child);
      } catch (error) {
        spawnError = serializeError(error);
      } finally {
        await logHandle.close();
      }
      return state();
    })();
    return startPromise;
  };

  const stop = async () => {
    stopRequested = true;
    if (stopPromise) return stopPromise;
    stopPromise = (async () => {
      if (startPromise) {
        try {
          await startPromise;
        } catch (error) {
          if (child) terminationFailed = true;
          throw error;
        }
      }
      if (!child || !completion) return state();
      if (completionResult
          || child.exitCode !== null
          || child.signalCode !== null) {
        exitedBeforeStop = true;
        completionResult ??= await completion;
        const drained = await drainExitedProcessGroup({
          child,
          ownedPid: child.pid,
          processGroupId: platform === "win32" ? null : child.pid,
          shutdownGraceMs,
        });
        terminationFailed = !drained.stopped;
        return state();
      }
      let terminated;
      try {
        terminated = await terminateProcess({
          child,
          ownedPid: child.pid,
          processGroupId: platform === "win32" ? null : child.pid,
          completion,
          shutdownGraceMs,
          onSignalDelivered: () => {
            shutdownSignalDelivered = true;
          },
        });
      } catch (error) {
        terminationFailed = true;
        throw error;
      }
      completionResult = terminated.completionResult;
      terminationFailed = !terminated.stopped;
      return state();
    })();
    return stopPromise;
  };

  const wait = async () => {
    if (startPromise) await startPromise;
    if (completion) completionResult ??= await completion;
    return state();
  };

  return {
    start,
    stop,
    wait,
    state,
  };
}

function childSpawned(child) {
  if (child.pid && child.exitCode === null && child.signalCode === null) {
    return new Promise((resolveSpawn, rejectSpawn) => {
      const onSpawn = () => {
        child.removeListener("error", onError);
        resolveSpawn();
      };
      const onError = (error) => {
        child.removeListener("spawn", onSpawn);
        rejectSpawn(error);
      };
      child.once("spawn", onSpawn);
      child.once("error", onError);
    });
  }
  return Promise.reject(new Error("Unreal Editor process did not start"));
}

async function runEditorProcess({
  executable,
  arguments: editorArguments,
  workingDirectory,
  environment,
  logPath,
  deadlineMs,
  shutdownGraceMs,
  signal,
  platform,
}) {
  const logHandle = await open(logPath, "w");
  let child;
  try {
    child = spawn(executable, editorArguments, {
      cwd: workingDirectory,
      env: environment,
      detached: platform !== "win32",
      shell: false,
      windowsHide: true,
      stdio: ["ignore", logHandle.fd, logHandle.fd],
    });
  } catch (error) {
    await logHandle.close();
    return {
      executable,
      spawnError: serializeError(error),
      timedOut: false,
      interrupted: false,
    };
  }

  const ownedPid = child.pid;
  const completion = childCompletion(child);
  let timeout;
  let abortHandler;
  const deadline = new Promise((resolveDeadline) => {
    timeout = setTimeout(
      () => resolveDeadline({ trigger: "timeout" }),
      deadlineMs,
    );
  });
  const interruption = signal
    ? new Promise((resolveInterruption) => {
      abortHandler = () => resolveInterruption({ trigger: "interrupted" });
      if (signal.aborted) abortHandler();
      else signal.addEventListener("abort", abortHandler, { once: true });
    })
    : new Promise(() => {});

  let completionResult;
  let timedOut = false;
  let interrupted = false;
  let terminationFailed = false;
  try {
    const first = await Promise.race([
      completion.then((result) => ({ trigger: "completed", result })),
      deadline,
      interruption,
    ]);
    if (first.trigger === "completed") {
      completionResult = first.result;
    } else {
      timedOut = first.trigger === "timeout";
      interrupted = first.trigger === "interrupted";
      const terminated = await terminateOwnedProcess({
        child,
        ownedPid,
        processGroupId: platform === "win32" ? null : ownedPid,
        completion,
        shutdownGraceMs,
      });
      completionResult = terminated.completionResult;
      terminationFailed = !terminated.stopped;
    }
  } finally {
    clearTimeout(timeout);
    if (abortHandler) signal.removeEventListener("abort", abortHandler);
    await logHandle.close();
  }

  return {
    executable,
    pid: ownedPid,
    processGroupId: platform === "win32" ? null : ownedPid,
    exitCode: completionResult?.exitCode ?? child.exitCode ?? null,
    signalCode: completionResult?.signalCode ?? child.signalCode ?? null,
    spawnError: completionResult?.spawnError ?? null,
    timedOut,
    interrupted,
    terminationFailed,
    deadlineMs,
  };
}

function childCompletion(child) {
  return new Promise((resolveCompletion) => {
    let spawnError = null;
    child.once("error", (error) => {
      spawnError = serializeError(error);
    });
    child.once("close", (exitCode, signalCode) => {
      resolveCompletion({
        exitCode,
        signalCode,
        spawnError,
      });
    });
  });
}

export async function terminateOwnedProcess({
  child,
  ownedPid,
  processGroupId = null,
  completion,
  shutdownGraceMs,
  onSignalDelivered,
  signalProcess = signalOwnedProcess,
  groupExists = ownedProcessGroupExists,
  sleep = delay,
  now = Date.now,
}) {
  if (!ownedPid || child.pid !== ownedPid) {
    throw new Error("refusing to terminate a process not owned by this runner");
  }
  if (processGroupId) {
    const terminated = await terminateOwnedProcessGroup({
      child,
      ownedPid,
      processGroupId,
      shutdownGraceMs,
      onSignalDelivered,
      signalProcess,
      groupExists,
      sleep,
      now,
    });
    const completionResult = await settleWithin(
      completion,
      Math.min(shutdownGraceMs, 1000),
    );
    return {
      ...terminated,
      completionResult,
    };
  }
  if (child.exitCode !== null || child.signalCode !== null) {
    return {
      stopped: true,
      completionResult: await completion,
    };
  }

  const termSignalDelivered = signalProcess({
    child,
    ownedPid,
    processGroupId,
    signalName: "SIGTERM",
  });
  if (termSignalDelivered) onSignalDelivered?.("SIGTERM");
  if (!termSignalDelivered) {
    const completionResult = await settleWithin(completion, shutdownGraceMs);
    return {
      stopped: Boolean(completionResult),
      completionResult,
      signalDelivered: false,
    };
  }
  let completionResult = await settleWithin(completion, shutdownGraceMs);
  if (completionResult) {
    return {
      stopped: true,
      completionResult,
      signalDelivered: true,
    };
  }

  const killSignalDelivered = signalProcess({
    child,
    ownedPid,
    processGroupId,
    signalName: "SIGKILL",
  });
  if (killSignalDelivered) onSignalDelivered?.("SIGKILL");
  if (!killSignalDelivered) {
    completionResult = await settleWithin(completion, shutdownGraceMs);
    return {
      stopped: Boolean(completionResult),
      completionResult,
      signalDelivered: true,
    };
  }
  completionResult = await settleWithin(completion, shutdownGraceMs);
  if (completionResult) {
    return {
      stopped: true,
      completionResult,
      signalDelivered: true,
    };
  }
  return {
    stopped: false,
    completionResult: null,
    signalDelivered: true,
  };
}

async function terminateOwnedProcessGroup({
  child,
  ownedPid,
  processGroupId,
  shutdownGraceMs,
  onSignalDelivered,
  signalProcess,
  groupExists,
  sleep,
  now,
}) {
  if (processGroupId !== ownedPid || child.pid !== ownedPid) {
    throw new Error(
      "refusing to terminate a process group not created by this runner",
    );
  }
  if (!groupExists(processGroupId)) {
    return {
      stopped: true,
      signalDelivered: false,
    };
  }

  let signalDelivered = signalProcess({
    child,
    ownedPid,
    processGroupId,
    signalName: "SIGTERM",
  });
  if (signalDelivered) onSignalDelivered?.("SIGTERM");
  if (await waitForOwnedProcessGroupExit(processGroupId, {
    deadlineMs: shutdownGraceMs,
    groupExists,
    sleep,
    now,
  })) {
    return {
      stopped: true,
      signalDelivered,
    };
  }

  const killSignalDelivered = signalProcess({
    child,
    ownedPid,
    processGroupId,
    signalName: "SIGKILL",
  });
  signalDelivered ||= killSignalDelivered;
  if (killSignalDelivered) onSignalDelivered?.("SIGKILL");
  return {
    stopped: await waitForOwnedProcessGroupExit(processGroupId, {
      deadlineMs: shutdownGraceMs,
      groupExists,
      sleep,
      now,
    }),
    signalDelivered,
  };
}

async function waitForOwnedProcessGroupExit(processGroupId, {
  deadlineMs,
  groupExists,
  sleep,
  now,
}) {
  if (!groupExists(processGroupId)) return true;
  const deadline = now() + deadlineMs;
  while (now() < deadline) {
    await sleep(Math.min(100, Math.max(1, deadline - now())));
    if (!groupExists(processGroupId)) return true;
  }
  return !groupExists(processGroupId);
}

function signalOwnedProcess({
  child,
  ownedPid,
  processGroupId,
  signalName,
}) {
  if (!ownedPid || child.pid !== ownedPid) {
    throw new Error("refusing to signal a process not owned by this runner");
  }
  try {
    const delivered = processGroupId
      ? process.kill(-processGroupId, signalName)
      : child.kill(signalName);
    return delivered !== false;
  } catch (error) {
    if (error?.code !== "ESRCH") throw error;
    return false;
  }
}

async function drainResidualOwnedProcessGroup({
  child,
  ownedPid,
  processGroupId,
  shutdownGraceMs,
  signalProcess = signalOwnedProcess,
  groupExists = ownedProcessGroupExists,
  sleep = delay,
}) {
  if (!processGroupId) return { stopped: true };
  if (!ownedPid || child.pid !== ownedPid || processGroupId !== ownedPid) {
    throw new Error(
      "refusing to drain a process group not created by this runner",
    );
  }
  return terminateOwnedProcessGroup({
    child,
    ownedPid,
    processGroupId,
    shutdownGraceMs,
    signalProcess,
    groupExists,
    sleep,
    now: Date.now,
  });
}

function ownedProcessGroupExists(processGroupId) {
  try {
    process.kill(-processGroupId, 0);
    return true;
  } catch (error) {
    if (error?.code === "ESRCH") return false;
    if (error?.code === "EPERM") return true;
    throw error;
  }
}

async function settleWithin(promise, durationMs) {
  const marker = Symbol("timeout");
  const result = await Promise.race([
    promise,
    delay(durationMs).then(() => marker),
  ]);
  return result === marker ? null : result;
}

function delay(durationMs) {
  return new Promise((resolveDelay) => setTimeout(resolveDelay, durationMs));
}

async function runCapturedCommand(executable, argumentsList, {
  workingDirectory,
  deadlineMs,
  shutdownGraceMs = 1000,
  signal,
} = {}) {
  const child = spawn(executable, argumentsList, {
    cwd: workingDirectory,
    detached: false,
    shell: false,
    windowsHide: true,
    stdio: ["ignore", "pipe", "pipe"],
  });
  const stdout = [];
  const stderr = [];
  child.stdout.on("data", (chunk) => stdout.push(chunk));
  child.stderr.on("data", (chunk) => stderr.push(chunk));

  const completion = childCompletion(child);
  const ownedPid = child.pid;
  let timeout;
  let abortHandler;
  const deadline = new Promise((resolveDeadline) => {
    timeout = setTimeout(
      () => resolveDeadline({ trigger: "timeout" }),
      deadlineMs,
    );
  });
  const interruption = signal
    ? new Promise((resolveInterruption) => {
      abortHandler = () => resolveInterruption({ trigger: "interrupted" });
      if (signal.aborted) abortHandler();
      else signal.addEventListener("abort", abortHandler, { once: true });
    })
    : new Promise(() => {});

  try {
    const first = await Promise.race([
      completion.then((result) => ({ trigger: "completed", result })),
      deadline,
      interruption,
    ]);
    if (first.trigger === "completed") {
      if (first.result.spawnError) {
        const error = new Error(first.result.spawnError.message);
        error.name = first.result.spawnError.name;
        throw error;
      }
      return {
        ...first.result,
        timedOut: false,
        interrupted: false,
        stdout: Buffer.concat(stdout).toString("utf8"),
        stderr: Buffer.concat(stderr).toString("utf8"),
      };
    }

    const terminated = await terminateOwnedProcess({
      child,
      ownedPid,
      processGroupId: null,
      completion,
      shutdownGraceMs,
    });
    return {
      ...(terminated.completionResult ?? {
        exitCode: child.exitCode,
        signalCode: child.signalCode,
      }),
      timedOut: first.trigger === "timeout",
      interrupted: first.trigger === "interrupted",
      terminationFailed: !terminated.stopped,
      stdout: Buffer.concat(stdout).toString("utf8"),
      stderr: Buffer.concat(stderr).toString("utf8"),
    };
  } finally {
    clearTimeout(timeout);
    if (abortHandler) signal.removeEventListener("abort", abortHandler);
  }
}

async function readAutomationReport(reportDirectory) {
  let entries;
  try {
    entries = await readdir(reportDirectory, { withFileTypes: true });
  } catch (error) {
    if (error?.code === "ENOENT") {
      return {
        present: false,
        valid: false,
        error: "Automation report directory is missing.",
        total: 0,
        passed: 0,
        failed: 0,
        notRun: 0,
        inProcess: 0,
        categoryTests: 0,
      };
    }
    throw error;
  }

  const indexEntry = entries.find(
    (entry) => entry.isFile() && entry.name.toLowerCase() === "index.json",
  );
  if (!indexEntry) {
    return {
      present: false,
      valid: false,
      error: "Automation report index.json is missing.",
      total: 0,
      passed: 0,
      failed: 0,
      notRun: 0,
      inProcess: 0,
      categoryTests: 0,
    };
  }

  let report;
  try {
    report = parseAutomationReportText(
      await readFile(join(reportDirectory, indexEntry.name), "utf8"),
    );
  } catch (error) {
    return {
      present: true,
      valid: false,
      error: `Automation report index.json is invalid: ${error.message}`,
      total: 0,
      passed: 0,
      failed: 0,
      notRun: 0,
      inProcess: 0,
      categoryTests: 0,
    };
  }
  return {
    present: true,
    ...summarizeAutomationReportObject(report),
  };
}

async function classifyEditorLogFile(logPath) {
  const hazards = [];
  const lines = createInterface({
    input: createReadStream(logPath),
    crlfDelay: Infinity,
  });
  let lineNumber = 0;
  for await (const line of lines) {
    lineNumber += 1;
    const kind = classifyLogLine(line);
    if (!kind || hazards.length >= MAX_LOG_HAZARDS) continue;
    hazards.push({
      kind,
      line,
      lineNumber,
    });
  }
  return hazards;
}

async function recordPhase(
  phases,
  phaseName,
  deadlineMs,
  parentSignal,
  operation,
) {
  const startedAt = Date.now();
  const controller = new AbortController();
  let parentAbortHandler;
  let rejectParentAbort;
  const parentAbortPromise = new Promise((_resolve, reject) => {
    rejectParentAbort = reject;
  });
  if (parentSignal) {
    parentAbortHandler = () => {
      const reason = parentSignal.reason instanceof Error
        ? parentSignal.reason
        : new Error("runner interrupted");
      controller.abort(reason);
      rejectParentAbort(reason);
    };
    if (parentSignal.aborted) parentAbortHandler();
    else parentSignal.addEventListener("abort", parentAbortHandler, { once: true });
  }

  let timeout;
  const timeoutPromise = new Promise((_resolve, reject) => {
    timeout = setTimeout(() => {
      const error = new PhaseTimeoutError(phaseName, deadlineMs);
      controller.abort(error);
      reject(error);
    }, deadlineMs);
  });
  try {
    const value = await Promise.race([
      Promise.resolve().then(() => operation(controller.signal)),
      timeoutPromise,
      parentAbortPromise,
    ]);
    phases[phaseName] = {
      status: "passed",
      durationMs: Date.now() - startedAt,
    };
    return { ok: true, value };
  } catch (error) {
    const phaseError = error instanceof Error ? error : new Error(String(error));
    phases[phaseName] = {
      status: phaseError instanceof PhaseTimeoutError ? "timed_out" : "failed",
      durationMs: Date.now() - startedAt,
      error: serializeError(phaseError),
    };
    return { ok: false, error: phaseError };
  } finally {
    clearTimeout(timeout);
    if (parentAbortHandler) {
      parentSignal.removeEventListener("abort", parentAbortHandler);
    }
  }
}

async function sha256(path, signal) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(path, { signal })) {
    hash.update(chunk);
  }
  return hash.digest("hex");
}

export async function hashCandidateArchive(
  pluginArchive,
  signal,
  hashFile = sha256,
) {
  if (!pluginArchive) return null;
  throwIfAborted(signal);
  const digest = await hashFile(pluginArchive, signal);
  throwIfAborted(signal);
  return digest;
}

function serializeError(error) {
  return {
    name: error?.name ?? "Error",
    message: error?.message ?? String(error),
  };
}

export function createGracefulInterruptHandler(
  controller,
  {
    onInterrupt,
    onRepeatedInterrupt,
  } = {},
) {
  let firstSignal = null;
  return (signalName) => {
    if (controller.signal.aborted) {
      onRepeatedInterrupt?.(signalName, firstSignal);
      return false;
    }
    firstSignal = signalName;
    controller.abort(new Error(`received ${signalName}`));
    onInterrupt?.(signalName);
    return true;
  };
}

export function installGracefulSignalHandlers({
  emitter,
  controller,
  onInterrupt,
  onRepeatedInterrupt,
}) {
  const interrupt = createGracefulInterruptHandler(controller, {
    onInterrupt,
    onRepeatedInterrupt,
  });
  const onSigint = () => interrupt("SIGINT");
  const onSigterm = () => interrupt("SIGTERM");
  emitter.on("SIGINT", onSigint);
  emitter.on("SIGTERM", onSigterm);
  return () => {
    emitter.removeListener("SIGINT", onSigint);
    emitter.removeListener("SIGTERM", onSigterm);
  };
}

async function runCli() {
  let options;
  try {
    options = parseRunnerArgs(process.argv.slice(2));
  } catch (error) {
    process.stderr.write(`${error.message}\n\n${runnerUsage()}\n`);
    process.exitCode = 2;
    return;
  }
  if (options.help) {
    process.stdout.write(`${runnerUsage()}\n`);
    return;
  }

  const controller = new AbortController();
  const disposeSignalHandlers = installGracefulSignalHandlers({
    emitter: process,
    controller,
    onInterrupt: (signalName) => {
      process.stderr.write(
        `UE test runner received ${signalName}; cleaning up owned processes...\n`,
      );
    },
    onRepeatedInterrupt: () => {
      process.stderr.write(
        "UE test runner is still cleaning up owned processes.\n",
      );
    },
  });

  try {
    const result = options.profile === "automation"
      ? await runAutomation(options, { signal: controller.signal })
      : await runPackagedE2E(options, { signal: controller.signal });
    process.stdout.write(
      `${options.profile} ${result.status}: ${join(options.outputDir, "result.json")}\n`,
    );
    if (result.status !== "passed") process.exitCode = 1;
  } catch (error) {
    process.stderr.write(`UE test runner failed: ${error.message}\n`);
    process.exitCode = 1;
  } finally {
    disposeSignalHandlers();
  }
}

if (process.argv[1]
    && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await runCli();
}
